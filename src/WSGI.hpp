#ifndef VELOCEM_WSGI_HPP
#define VELOCEM_WSGI_HPP

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

static bool insert_field(std::vector<char>& buf, PyObject* str) {
  const char* base;
  Py_ssize_t len;
  unpack_unicode(str, &base, &len, "Header fields must be str objects");

  if((len == 4 && !strncasecmp("Date", base, 4)) ||
      (len == 6 && !strncasecmp("Server", base, 6)) ||
      (len == 10 && !strncasecmp("Connection", base, 10)) ||
      (len == 14 && !strncasecmp("Content-Length", base, 14))) [[unlikely]] {
    return false;
  }

  buf.insert(buf.end(), base, base + len);
  return true;
}

static void insert_header(std::vector<char>& buf, PyObject* tuple) {
  if(!PyTuple_Check(tuple)) [[unlikely]] {
    PyErr_SetString(PyExc_TypeError, "Header must be size two tuples");
    throw std::runtime_error {"Python tuple error"};
  }

  if(PyTuple_GET_SIZE(tuple) != 2) [[unlikely]] {
    PyErr_SetString(PyExc_ValueError, "Header tuple must be size two");
    throw std::runtime_error {"Python tuple error"};
  }

  PyObject* field {PyTuple_GET_ITEM(tuple, 0)};
  if(!insert_field(buf, field)) [[unlikely]]
    return;
  insert_literal(buf, ": ");

  PyObject* value {PyTuple_GET_ITEM(tuple, 1)};
  insert_pystr(buf, value, "Header values must be str objects");
  insert_literal(buf, "\r\n");
}

static void build_body(std::vector<char>& buf, PyObject* iter) {
  std::size_t bodysize;
  if(PyBytes_Check(iter))
    bodysize = (std::size_t) PyBytes_GET_SIZE(iter);
  else if(PyList_Check(iter))
    bodysize = get_body_list_size(iter);
  else if(PyTuple_Check(iter))
    bodysize = get_body_tuple_size(iter);
  else
    bodysize = 0; // ToDO

  insert_literal(buf, "Content-Length: ");
  insert_str(buf, std::to_string(bodysize));
  insert_literal(buf, "\r\n\r\n");

  if(PyBytes_Check(iter)) {
    insert_chars(buf, PyBytes_AS_STRING(iter), bodysize);
  } else if(PyList_Check(iter)) {
    Py_ssize_t listlen {PyList_GET_SIZE(iter)};
    for(Py_ssize_t i {0}; i < listlen; ++i)
      insert_pybytes_unchecked(buf, PyList_GET_ITEM(iter, i));
  } else if(PyTuple_Check(iter)) {
    Py_ssize_t listlen {PyTuple_GET_SIZE(iter)};
    for(Py_ssize_t i {0}; i < listlen; ++i)
      insert_pybytes_unchecked(buf, PyTuple_GET_ITEM(iter, i));
  } else {
    // TODO: iterable obj
  }
}


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

  std::optional<std::vector<char>> run(Request* req, int http_minor, int meth,
      bool keepalive) {
    std::vector<char> buf;
    buf.reserve(1024); // Nice round numbers

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

      if(!iter)
        throw std::runtime_error {"Python function call error"};

      build_headers(buf, keepalive);
      build_body(buf, iter);

    } catch(...) {
      PyErr_Print();
      PyErr_Clear();

      Py_XDECREF(status_);
      Py_XDECREF(headers_);
      Py_XDECREF(iter);
      return {};
    }

    Py_DECREF(status_);
    Py_DECREF(headers_);
    Py_DECREF(iter);
    return buf;
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

  void build_headers(std::vector<char>& buf, bool keep_alive) {
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

    for(Py_ssize_t i {0}, end {PyList_GET_SIZE(headers_)}; i < end; ++i)
      insert_header(buf, PyList_GET_ITEM(headers_, i));
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
