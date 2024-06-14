#ifndef VELOCEM_WSGI_HPP
#define VELOCEM_WSGI_HPP

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <vector>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "Constants.hpp"
#include "Request.hpp"
#include "Util.hpp"

namespace velocem {

enum InsertFieldResult : int {
  NO_INSERT = 0,
  INSERTED,
  CONLEN,
};

static InsertFieldResult insert_field(std::vector<char>& buf, PyObject* str) {
  const char* base;
  Py_ssize_t len;
  unpack_unicode(str, &base, &len, "Header fields must be str objects");

  if(len == 14 && !strncasecmp("Content-Length", base, 17)) [[unlikely]] {
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

  PyObject* value {PyTuple_GET_ITEM(tuple, 1)};

  if(result == CONLEN) [[unlikely]] {
    const char* base;
    Py_ssize_t len;
    unpack_unicode(value, &base, &len, "Header value must be str objects");

    Py_ssize_t conlen;
    auto result {std::from_chars(base, base + len, conlen)};
    if(result.ec != std::errc {}) {
      PyErr_SetString(PyExc_ValueError, "Invalid Content-Length header");
      throw std::runtime_error {"Python header error"};
    }
    return conlen;
  }

  insert_literal(buf, ": ");
  insert_pystr(buf, value, "Header values must be str objects");
  insert_literal(buf, "\r\n");
  return {};
}

static void insert_body_pybytes(std::vector<char>& buf, PyObject* iter,
    Py_ssize_t sz) {
  insert_str(buf, std::format("Content-Length: {}\r\n\r\n", sz));
  if(PyBytes_GET_SIZE(iter) > sz) {
    PyErr_SetString(PyExc_ValueError,
        "Response is shorter than provided Content-Length header");
    throw std::runtime_error("Python header error");
  }
  insert_chars(buf, PyBytes_AS_STRING(iter), sz);
}

static void insert_body_pybytes(std::vector<char>& buf, PyObject* iter) {
  auto sz {PyBytes_GET_SIZE(iter)};
  insert_str(buf, std::format("Content-Length: {}\r\n\r\n", sz));
  insert_chars(buf, PyBytes_AS_STRING(iter), sz);
}

static void insert_body_pylist(std::vector<char>& buf, PyObject* iter,
    Py_ssize_t sz) {
  insert_str(buf, std::format("Content-Length: {}\r\n\r\n", sz));
  for(Py_ssize_t i {0}, end {PyList_GET_SIZE(iter)}; i < end && sz; ++i)
    sz -= insert_pybytes_unchecked(buf, PyList_GET_ITEM(iter, i), sz);

  if(sz) {
    PyErr_SetString(PyExc_ValueError,
        "Response is shorter than provided Content-Length header");
    throw std::runtime_error("Python header error");
  }
}

static void insert_body_pylist(std::vector<char>& buf, PyObject* iter) {
  auto sz {get_body_list_size(iter)};
  insert_str(buf, std::format("Content-Length: {}\r\n\r\n", sz));
  for(Py_ssize_t i {0}, end {PyList_GET_SIZE(iter)}; i < end; ++i)
    insert_pybytes_unchecked(buf, PyList_GET_ITEM(iter, i));
}

static void insert_body_pytuple(std::vector<char>& buf, PyObject* iter,
    Py_ssize_t sz) {
  insert_str(buf, std::format("Content-Length: {}\r\n\r\n", sz));
  for(Py_ssize_t i {0}, end {PyTuple_GET_SIZE(iter)}; i < end && sz; ++i)
    sz -= insert_pybytes_unchecked(buf, PyTuple_GET_ITEM(iter, i), sz);

  if(sz) {
    PyErr_SetString(PyExc_ValueError,
        "Response is shorter than provided Content-Length header");
    throw std::runtime_error("Python header error");
  }
}

static void insert_body_pytuple(std::vector<char>& buf, PyObject* iter) {
  auto sz {get_body_tuple_size(iter)};
  insert_str(buf, std::format("Content-Length: {}\r\n\r\n", sz));
  for(Py_ssize_t i {0}, end {PyTuple_GET_SIZE(iter)}; i < end; ++i)
    insert_pybytes_unchecked(buf, PyTuple_GET_ITEM(iter, i));
}

static PyObject* build_body(std::vector<char>& buf, PyObject* iter,
    Py_ssize_t conlen) {
  if(PyBytes_Check(iter)) {
    insert_body_pybytes(buf, iter, conlen);
  } else if(PyList_Check(iter)) {
    insert_body_pylist(buf, iter, conlen);
  } else if(PyTuple_Check(iter)) {
    insert_body_pytuple(buf, iter, conlen);
  } else if(PyIter_Check(iter)) {
    return iter;
  } else {
    PyErr_SetString(PyExc_TypeError, "WSGI App must return iterable");
    throw std::runtime_error {"Python iter error"};
  }
  return nullptr;
}

static PyObject* build_body(std::vector<char>& buf, PyObject* iter) {
  if(PyBytes_Check(iter)) {
    insert_body_pybytes(buf, iter);
  } else if(PyList_Check(iter)) {
    insert_body_pylist(buf, iter);
  } else if(PyTuple_Check(iter)) {
    insert_body_pytuple(buf, iter);
  } else if(PyIter_Check(iter)) {
    return iter;
  } else {
    PyErr_SetString(PyExc_TypeError, "WSGI App must return iterable");
    throw std::runtime_error {"Python iter error"};
  }
  return nullptr;
}

struct PyAppRet {
  std::vector<char> buf;
  PyObject* iter {nullptr};
  PyObject* status;
  PyObject* headers;
  std::optional<Py_ssize_t> conlen;
};

struct PythonApp {
  PythonApp(PyObject* app, const char* host, const char* port)
      : app_ {app}, vecCall_ {PyVectorcall_Function(app)} {

    static PyMethodDef srdef {
        .ml_name = "start_response",
        .ml_meth = (PyCFunction) start_response_tr,
        .ml_flags = METH_FASTCALL | METH_KEYWORDS,
    };

    cap_ = PyCapsule_New(this, NULL, NULL);
    sr_ = PyCFunction_New(&srdef, cap_);
    baseEnv_ = _PyDict_NewPresized(64);

    PyObject* ver {PyTuple_Pack(2, PyLong_FromLong(1), PyLong_FromLong(0))};
    PyDict_SetItemString(baseEnv_, "wsgi.version", ver);
    Py_DECREF(ver);

    PyObject* http {PyUnicode_FromString("http")};
    PyDict_SetItemString(baseEnv_, "wsgi.url_scheme", http);
    Py_DECREF(http);

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

  PythonApp(PythonApp&) = delete;
  PythonApp(PythonApp&&) = delete;

  ~PythonApp() {
    Py_DECREF(baseEnv_);
    Py_DECREF(sr_);
    Py_DECREF(cap_);
  }

  std::optional<PyAppRet> run(Request* req, int http_minor, int meth,
      bool keepalive) {
    PyAppRet ret {};
    ret.buf.reserve(1024); // Nice round numbers

    auto env {make_env(req, http_minor, meth)};
    PyObject* iter {nullptr};

    status_ = nullptr;
    headers_ = nullptr;

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

      ret.conlen = build_headers(ret.buf, keepalive);
      if(!ret.conlen) [[likely]]
        ret.iter = build_body(ret.buf, iter);
      else
        ret.iter = build_body(ret.buf, iter, *ret.conlen);

    } catch(...) {
      PyErr_Print();
      PyErr_Clear();

      Py_XDECREF(status_);
      Py_XDECREF(headers_);
      Py_XDECREF(iter);
      return {};
    }

    if(!ret.iter) {
      Py_DECREF(iter);
      Py_DECREF(status_);
      Py_DECREF(headers_);
    } else {
      ret.status = status_;
      ret.headers = headers_;
    }
    return ret;
  }

  PyObject* make_env(Request* req, int http_minor, int meth) {
    auto env {PyDict_Copy(baseEnv_)};

    PyDict_SetItem(env, gPO.meth, gPO.methods[meth]);
    PyDict_SetItem(env, gPO.path, (PyObject*) &req->url());


    if(req->has_query())
      PyDict_SetItem(env, gPO.query, (PyObject*) &req->query());
    else
      PyDict_SetItem(env, gPO.query, gPO.empty);

    PyDict_SetItem(env, gPO.proto, http_minor ? gPO.http11 : gPO.http10);

    for(const auto& [hdr, val] : std::views::zip(req->headers, req->values))
      PyDict_SetItem(env, (PyObject*) &hdr.bsv, (PyObject*) &val);

    replace_key(env, gPO.http_conlen, gPO.conlen);
    replace_key(env, gPO.http_contype, gPO.contype);

    return env;
  }

  std::optional<Py_ssize_t> build_headers(std::vector<char>& buf,
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
        break;
      }
    }

    for(; i < end; ++i)
      insert_header(buf, PyList_GET_ITEM(headers_, i));

    return ret;
  }

  PyObject* start_response(PyObject* const* args, Py_ssize_t nargs,
      PyObject* kwnames) {

    PyObject* exc_info {nullptr};
    static const char* _keywords[] = {"status", "response_headers", "exc_info",
        nullptr};
    static _PyArg_Parser _parser = {
        .format = "OO|O:start_response",
        .keywords = _keywords,
    };

    if(!_PyArg_ParseStackAndKeywords(args, nargs, kwnames, &_parser, &status_,
           &headers_, &exc_info))
      return nullptr;

    Py_INCREF(status_);
    Py_INCREF(headers_);

    Py_RETURN_NONE;
  }

  static PyObject* start_response_tr(PyObject* self, PyObject* const* args,
      Py_ssize_t nargs, PyObject* kwnames) {
    auto pyapp {PyCapsule_GetPointer(self, nullptr)};
    return ((PythonApp*) pyapp)->start_response(args, nargs, kwnames);
  }

  PyObject* app_;
  PyObject* baseEnv_;
  vectorcallfunc vecCall_;
  PyObject* cap_;
  PyObject* sr_;

  PyObject* status_;
  PyObject* headers_;
};

} // namespace velocem

#endif // VELOCEM_WSGI_APP
