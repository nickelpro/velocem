#include "WSGIApp.hpp"

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <vector>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#ifdef _MSC_VER
#include <string.h>
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#include "Constants.hpp"
#include "Request.hpp"
#include "Util.hpp"

namespace velocem {

namespace {

enum InsertFieldResult : int {
  NO_INSERT = 0,
  INSERTED,
  CONLEN,
};

InsertFieldResult insert_field(std::vector<char>& buf, PyObject* str) {
  const char* base;
  Py_ssize_t len;
  unpack_unicode(str, &base, &len, "Header fields must be str objects");

  if(len == 14 && !strncasecmp("Content-Length", base, 14)) [[unlikely]] {
    buf.insert(buf.end(), base, base + len);
    return CONLEN;
  }

  if((len == 4 && !strncasecmp("Date", base, 4)) ||
      (len == 6 && !strncasecmp("Server", base, 6)) ||
      (len == 10 && !strncasecmp("Connection", base, 10))) [[unlikely]] {
    return NO_INSERT;
  }

  buf.insert(buf.end(), base, base + len);
  return INSERTED;
}

std::optional<Py_ssize_t> insert_header(std::vector<char>& buf,
    PyObject* tuple) {
  if(!PyTuple_Check(tuple)) [[unlikely]] {
    PyErr_SetString(PyExc_TypeError, "Headers must be size two tuples");
    throw std::runtime_error {"Python tuple error"};
  }

  if(PyTuple_GET_SIZE(tuple) != 2) [[unlikely]] {
    PyErr_SetString(PyExc_ValueError, "Header tuple must be size two");
    throw std::runtime_error {"Python tuple error"};
  }

  PyObject* field {PyTuple_GET_ITEM(tuple, 0)};
  auto result {insert_field(buf, field)};

  if(result == NO_INSERT) [[unlikely]]
    return {};

  insert_literal(buf, ": ");

  PyObject* value {PyTuple_GET_ITEM(tuple, 1)};

  if(result == CONLEN) [[unlikely]] {
    const char* base;
    Py_ssize_t len;
    unpack_unicode(value, &base, &len, "Header value must be str objects");

    Py_ssize_t conlen;
    auto fc {std::from_chars(base, base + len, conlen)};
    if(fc.ec != std::errc {}) {
      PyErr_SetString(PyExc_ValueError, "Invalid Content-Length header");
      throw std::runtime_error {"Python header error"};
    }
    insert_chars(buf, base, len);
    insert_literal(buf, "\r\n");
    return conlen;
  }

  insert_pystr(buf, value, "Header values must be str objects");
  insert_literal(buf, "\r\n");
  return {};
}

void insert_body_pybytes(std::vector<char>& buf, std::vector<char>& wb,
    PyObject* iter) {
  auto sz {PyBytes_GET_SIZE(iter)};
  insert_str(buf, std::format("Content-Length: {}\r\n\r\n", sz + wb.size()));
  buf.insert(buf.end(), wb.begin(), wb.end());
  insert_chars(buf, PyBytes_AS_STRING(iter), sz);
}

void insert_body_pybytes(std::vector<char>& buf, PyObject* iter,
    Py_ssize_t sz) {
  if(PyBytes_GET_SIZE(iter) < sz) {
    PyErr_SetString(PyExc_ValueError,
        "Response is shorter than provided Content-Length header");
    throw std::runtime_error {"Python header error"};
  }
  insert_chars(buf, PyBytes_AS_STRING(iter), sz);
}

void insert_body_pylist(std::vector<char>& buf, PyObject* iter, Py_ssize_t sz) {
  for(Py_ssize_t i {0}, end {PyList_GET_SIZE(iter)}; i < end && sz; ++i)
    sz -= insert_pybytes_unchecked(buf, PyList_GET_ITEM(iter, i), sz);

  if(sz) {
    PyErr_SetString(PyExc_ValueError,
        "Response is shorter than provided Content-Length header");
    throw std::runtime_error {"Python header error"};
  }
}

void insert_body_pylist(std::vector<char>& buf, std::vector<char>& wb,
    PyObject* iter) {
  auto sz {get_body_list_size(iter)};
  insert_str(buf, std::format("Content-Length: {}\r\n\r\n", sz + wb.size()));
  buf.insert(buf.end(), wb.begin(), wb.end());
  for(Py_ssize_t i {0}, end {PyList_GET_SIZE(iter)}; i < end; ++i)
    insert_pybytes_unchecked(buf, PyList_GET_ITEM(iter, i));
}

void insert_body_pytuple(std::vector<char>& buf, PyObject* iter,
    Py_ssize_t sz) {
  for(Py_ssize_t i {0}, end {PyTuple_GET_SIZE(iter)}; i < end && sz; ++i)
    sz -= insert_pybytes_unchecked(buf, PyTuple_GET_ITEM(iter, i), sz);

  if(sz) {
    PyErr_SetString(PyExc_ValueError,
        "Response is shorter than provided Content-Length header");
    throw std::runtime_error {"Python header error"};
  }
}

void insert_body_pytuple(std::vector<char>& buf, std::vector<char>& wb,
    PyObject* iter) {
  auto sz {get_body_tuple_size(iter)};
  insert_str(buf, std::format("Content-Length: {}\r\n\r\n", sz + wb.size()));
  buf.insert(buf.end(), wb.begin(), wb.end());
  for(Py_ssize_t i {0}, end {PyTuple_GET_SIZE(iter)}; i < end; ++i)
    insert_pybytes_unchecked(buf, PyTuple_GET_ITEM(iter, i));
}

void insert_body_pyseq(std::vector<char>& buf, std::vector<char>& wb,
    PyObject* iter) {
  PyObject* seq {PySequence_Tuple(iter)};
  insert_body_pytuple(buf, wb, seq);
  Py_DECREF(seq);
}

void insert_body_pyseq(std::vector<char>& buf, PyObject* iter, Py_ssize_t sz) {
  PyObject* seq {PySequence_Tuple(iter)};
  insert_body_pytuple(buf, seq, sz);
  Py_DECREF(seq);
}

PyObject* insert_body_iter_common(std::vector<char>& buf, std::vector<char>& wb,
    PyObject* iter, PyObject* first) {
  PyObject* second {PyIter_Next(iter)};
  if(!second) {
    close_iterator(iter);
    if(PyErr_Occurred())
      throw std::runtime_error {"Python iterator error"};

    const char* bytes;
    Py_ssize_t len;
    unpack_pybytes(first, &bytes, &len, "Response iterator must yield bytes");

    insert_str(buf, std::format("Content-Length: {}\r\n\r\n", len + wb.size()));
    buf.insert(buf.end(), wb.begin(), wb.end());
    insert_chars(buf, bytes, len);
    Py_DECREF(first);
    return nullptr;
  }

  insert_literal(buf, "Transfer-Encoding: chunked\r\n\r\n");

  const char* bytes;
  Py_ssize_t len;
  unpack_pybytes(first, &bytes, &len, "Response iterator must yield bytes");

  const char* bytes2;
  Py_ssize_t len2;
  unpack_pybytes(second, &bytes2, &len2, "Response iterator must yield bytes");

  insert_str(buf, std::format("{:x}\r\n", len + len2 + wb.size()));
  buf.insert(buf.end(), wb.begin(), wb.end());
  insert_chars(buf, bytes, len);
  insert_chars(buf, bytes2, len2);
  insert_literal(buf, "\r\n");
  Py_DECREF(first);
  Py_DECREF(second);
  return iter;
}

PyObject* insert_body_iter(std::vector<char>& buf, std::vector<char>& wb,
    PyObject* iter) {
  PyObject* first {PyIter_Next(iter)};
  if(!first) {
    close_iterator(iter);
    if(PyErr_Occurred())
      throw std::runtime_error {"Python iterator error"};
    insert_literal(buf, "Content-Length: 0\r\n\r\n");
    return nullptr;
  }

  return insert_body_iter_common(buf, wb, iter, first);
}

void insert_body_iter(std::vector<char>& buf, PyObject* iter, Py_ssize_t sz) {
  PyObject* next {nullptr};
  try {
    while((next = PyIter_Next(iter)) && sz) {
      sz -= insert_pybytes(buf, next, sz, "Iterator must yield bytes object");
      Py_DECREF(next);
    }
  } catch(...) {
    Py_DECREF(next);
    close_iterator(iter);
    throw;
  }

  close_iterator(iter);

  if(PyErr_Occurred())
    throw std::runtime_error {"Python iterator error"};

  if(sz) {
    PyErr_SetString(PyExc_ValueError,
        "Response is shorter than provided Content-Length header");
    throw std::runtime_error {"Python header error"};
  }
}

PyObject* prime_generator(PyObject* iter) {
  PyObject* next {PyIter_Next(iter)};
  if(!next) {
    close_iterator(iter);
    if(PyErr_Occurred())
      throw std::runtime_error {"Python iterator error"};
  }
  return next;
}

PyObject* insert_body_generator(std::vector<char>& buf, std::vector<char>& wb,
    PyObject* iter, PyObject* first) {
  if(!first) {
    insert_literal(buf, "Content-Length: 0\r\n\r\n");
    return nullptr;
  }
  return insert_body_iter_common(buf, wb, iter, first);
}

void insert_body_generator(std::vector<char>& buf, std::vector<char>& wb,
    PyObject* iter, PyObject* first, Py_ssize_t sz) {

  if(wb.size() >= static_cast<std::size_t>(sz)) {
    buf.insert(buf.end(), wb.begin(), wb.begin() + sz);
    if(first) {
      Py_DECREF(first);
      close_iterator(iter);
    }
    return;
  } else {
    buf.insert(buf.end(), wb.begin(), wb.end());
    sz -= wb.size();
  }

  if(!first) {
    PyErr_SetString(PyExc_ValueError,
        "Response is shorter than provided Content-Length header");
    throw std::runtime_error {"Python header error"};
  }

  try {
    sz -= insert_pybytes(buf, first, sz, "Iterator must yield bytes object");
  } catch(...) {
    Py_DECREF(first);
    throw;
  }
  Py_DECREF(first);

  insert_body_iter(buf, iter, sz);
}


void build_body(std::vector<char>& buf, std::vector<char>& wb, PyObject* iter,
    Py_ssize_t conlen) {
  insert_literal(buf, "\r\n");

  if(!wb.empty()) [[unlikely]] {
    if(wb.size() >= static_cast<std::size_t>(conlen)) {
      buf.insert(buf.end(), wb.begin(), wb.begin() + conlen);
      return;
    }

    buf.insert(buf.end(), wb.begin(), wb.end());
    conlen -= wb.size();
  }

  if(PyBytes_Check(iter)) {
    insert_body_pybytes(buf, iter, conlen);
  } else if(PyList_Check(iter)) {
    insert_body_pylist(buf, iter, conlen);
  } else if(PyTuple_Check(iter)) {
    insert_body_pytuple(buf, iter, conlen);
  } else if(PySequence_Check(iter)) {
    insert_body_pyseq(buf, iter, conlen);
  } else if(PyIter_Check(iter)) {
    insert_body_iter(buf, iter, conlen);
  } else {
    PyErr_SetString(PyExc_TypeError, "WSGI App must return iterable");
    throw std::runtime_error {"Python iter error"};
  }
}

PyObject* build_body(std::vector<char>& buf, std::vector<char>& wb,
    PyObject* iter) {
  if(PyBytes_Check(iter)) {
    insert_body_pybytes(buf, wb, iter);
  } else if(PyList_Check(iter)) {
    insert_body_pylist(buf, wb, iter);
  } else if(PyTuple_Check(iter)) {
    insert_body_pytuple(buf, wb, iter);
  } else if(PySequence_Check(iter)) {
    insert_body_pyseq(buf, wb, iter);
  } else if(PyIter_Check(iter)) {
    return insert_body_iter(buf, wb, iter);
  } else {
    PyErr_SetString(PyExc_TypeError, "WSGI App must return iterable");
    throw std::runtime_error {"Python iter error"};
  }
  return nullptr;
}

struct {
  std::queue<WSGIAppRet*> q;

  WSGIAppRet* pop() {
    if(q.empty()) {
      auto ptr = new WSGIAppRet;
      ptr->buf.reserve(1024);
      return ptr;
    }
    auto ptr = q.front();
    q.pop();
    return ptr;
  }

  void push(WSGIAppRet* ptr) {
    ptr->reset();
    q.push(ptr);
  }

} AppRetQ;

} // namespace

void WSGIAppRet::reset() {
  buf.clear();
  conlen.reset();
  iter = nullptr;
}

void push_WSGIAppRet(WSGIAppRet* appret) {
  AppRetQ.push(appret);
}

WSGIApp::WSGIApp(PyObject* app, const char* host, const char* port)
    : app_ {app}, vecCall_ {PyVectorcall_Function(app)} {

  static PyMethodDef srdef {
      .ml_name = "start_response",
      .ml_meth = (PyCFunction) start_response_tr,
      .ml_flags = METH_FASTCALL | METH_KEYWORDS,
  };

  static PyMethodDef wcbdef {
      .ml_name = "write",
      .ml_meth = (PyCFunction) write_cb_tr,
      .ml_flags = METH_FASTCALL,
  };

  cap_ = PyCapsule_New(this, NULL, NULL);
  sr_ = PyCFunction_New(&srdef, cap_);
  wcb_ = PyCFunction_New(&wcbdef, cap_);
  baseEnv_ = _PyDict_NewPresized(64);

  PyDict_SetItemString(baseEnv_, "wsgi.version", gPO.wsgi_ver);
  PyDict_SetItemString(baseEnv_, "wsgi.url_scheme", gPO.http);

  PyObject* phost {PyUnicode_FromString(host)};
  PyDict_SetItemString(baseEnv_, "SERVER_NAME", phost);
  Py_DECREF(phost);

  PyObject* pport {PyUnicode_FromString(port)};
  PyDict_SetItemString(baseEnv_, "SERVER_PORT", pport);
  Py_DECREF(pport);

  PyDict_SetItemString(baseEnv_, "SCRIPT_NAME", gPO.empty);
  PyDict_SetItemString(baseEnv_, "wsgi.input_terminated", Py_True);
  PyDict_SetItemString(baseEnv_, "wsgi.errors", PySys_GetObject("stderr"));
  PyDict_SetItemString(baseEnv_, "wsgi.multithread", Py_False);
  PyDict_SetItemString(baseEnv_, "wsgi.multiprocess", Py_True);
  PyDict_SetItemString(baseEnv_, "wsgi.run_once", Py_False);
}

WSGIApp::~WSGIApp() {
  Py_DECREF(baseEnv_);
  Py_DECREF(sr_);
  Py_DECREF(wcb_);
  Py_DECREF(cap_);
}

WSGIAppRet* WSGIApp::run(Request* req, int http_minor, int meth,
    bool keepalive) {
  WSGIAppRet* ret {AppRetQ.pop()};

  auto env {make_env(req, http_minor, meth)};
  PyObject* iter {nullptr};

  in_handle = true;
  status_ = nullptr;
  headers_ = nullptr;
  writebuf_.clear();

  try {
    if(vecCall_) {
      std::array args {env, sr_};
      iter = vecCall_(app_, args.data(), args.size(), nullptr);
    } else {
      iter = PyObject_CallFunctionObjArgs(app_, env, sr_, nullptr);
    }

    Py_DECREF(env);

    if(!iter) [[unlikely]]
      throw std::runtime_error {"Python function call error"};

    if(PyGen_Check(iter)) {
      PyObject* first {prime_generator(iter)};
      ret->conlen = build_headers(ret->buf, keepalive);

      // Once we've built the headers you don't get to change them anymore
      in_handle = false;

      if(!ret->conlen)
        ret->iter = insert_body_generator(ret->buf, writebuf_, iter, first);
      else
        insert_body_generator(ret->buf, writebuf_, iter, first, *ret->conlen);

    } else {
      ret->conlen = build_headers(ret->buf, keepalive);
      in_handle = false;

      if(!ret->conlen)
        ret->iter = build_body(ret->buf, writebuf_, iter);
      else
        build_body(ret->buf, writebuf_, iter, *ret->conlen);
    }


  } catch(...) {
    PyErr_Print();
    PyErr_Clear();

    Py_XDECREF(iter);
    Py_XDECREF(status_);
    Py_XDECREF(headers_);
    AppRetQ.push(ret);

    in_handle = false;
    return nullptr;
  }

  if(!ret->iter)
    Py_DECREF(iter);
  Py_DECREF(status_);
  Py_DECREF(headers_);
  return ret;
}

PyObject* WSGIApp::make_env(Request* req, int http_minor, int meth) {
  auto env {PyDict_Copy(baseEnv_)};

  PyDict_SetItem(env, gPO.meth, gPO.methods[meth]);
  PyDict_SetItem(env, gPO.path, (PyObject*) &req->url());


  if(req->has_query())
    PyDict_SetItem(env, gPO.query, (PyObject*) &req->query());
  else
    PyDict_SetItem(env, gPO.query, gPO.empty);

  PyDict_SetItem(env, gPO.proto, http_minor ? gPO.http11 : gPO.http10);

  for(const auto& [hdr, val] : std::views::zip(req->headers_, req->values_))
    PyDict_SetItem(env, (PyObject*) &hdr.bsv, (PyObject*) &val);

  replace_key(env, gPO.http_conlen, gPO.conlen);
  replace_key(env, gPO.http_contype, gPO.contype);

  return env;
}

std::optional<Py_ssize_t> WSGIApp::build_headers(std::vector<char>& buf,
    bool keep_alive) {
  std::optional<Py_ssize_t> ret;
  insert_literal(buf, "HTTP/1.1 ");
  insert_pystr(buf, status_, "Status must be str object");
  insert_literal(buf, "\r\n");
  insert_str(buf, gRequiredHeaders);
  if(keep_alive)
    insert_literal(buf, "Connection: keep-alive\r\n");
  else
    insert_literal(buf, "Connection: close\r\n");

  if(!PyList_Check(headers_)) [[unlikely]] {
    PyErr_SetString(PyExc_TypeError, "Headers must be list");
    throw std::runtime_error {"Python list error"};
  }

  Py_ssize_t i {0}, end {PyList_GET_SIZE(headers_)};

  for(; i < end; ++i) {
    auto result {insert_header(buf, PyList_GET_ITEM(headers_, i))};
    if(result) {
      ret = result;
      ++i;
      break;
    }
  }

  for(; i < end; ++i)
    insert_header(buf, PyList_GET_ITEM(headers_, i));

  return ret;
}

PyObject* WSGIApp::start_response(PyObject* const* args, Py_ssize_t nargs,
    PyObject* kwnames) {

  PyObject* exc_info {nullptr};
  PyObject* status;
  PyObject* headers;

  static const char* _keywords[] {"status", "response_headers", "exc_info",
      nullptr};
  static _PyArg_Parser _parser {
      .format = "OO|O:start_response",
      .keywords = _keywords,
  };

  if(!_PyArg_ParseStackAndKeywords(args, nargs, kwnames, &_parser, &status,
         &headers, &exc_info))
    return nullptr;

  if(status_) [[unlikely]] {
    if(!exc_info) {
      PyErr_SetString(PyExc_TypeError,
          "'start_response' called twice without passing 'exc_info' the second "
          "time");
      return nullptr;
    }

    if(!(PyTuple_Check(exc_info) || PyTuple_GET_SIZE(exc_info) != 3)) {
      PyErr_SetString(PyExc_TypeError, "'exc_info' must be a 3-tuple");
      return nullptr;
    }

    if(!in_handle) {
      Py_INCREF(exc_info);
      PyErr_SetRaisedException(exc_info);
      return nullptr;
    }

    Py_DECREF(status_);
    Py_DECREF(headers_);
  }

  Py_INCREF(status_ = status);
  Py_INCREF(headers_ = headers);

  return wcb_;
}

PyObject* WSGIApp::start_response_tr(PyObject* self, PyObject* const* args,
    Py_ssize_t nargs, PyObject* kwnames) {
  auto pyapp {PyCapsule_GetPointer(self, nullptr)};
  return (static_cast<WSGIApp*>(pyapp))->start_response(args, nargs, kwnames);
}

PyObject* WSGIApp::write_cb(PyObject* const* args, Py_ssize_t nargs) {
  PyObject* bytes;

  if(!_PyArg_ParseStack(args, nargs, "S", &bytes))
    return nullptr;

  insert_pybytes_unchecked(writebuf_, bytes);
  Py_RETURN_NONE;
}

PyObject* WSGIApp::write_cb_tr(PyObject* self, PyObject* const* args,
    Py_ssize_t nargs) {
  auto pyapp {PyCapsule_GetPointer(self, nullptr)};
  return (static_cast<WSGIApp*>(pyapp))->write_cb(args, nargs);
}

} // namespace velocem
