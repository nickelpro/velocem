#ifndef VELOCEM_WSGI_HPP
#define VELOCEM_WSGI_HPP

#include <chrono>
#include <cstdlib>
#include <exception>
#include <optional>
#include <vector>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "Constants.hpp"
#include "Request.hpp"
#include "Util.hpp"

namespace velocem {

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
    buf.reserve(1024); // Nice round numbersgmtime

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

    // Switch this when we get manylinux_2_34
    // for(const auto& [hdr, val] : std::views::zip(req->headers, req->values))
    //   PyDict_SetItem(env, (PyObject*) &hdr.bsv, (PyObject*) &val);

    for(size_t i {0}; i < req->headers.size(); ++i)
      PyDict_SetItem(env, (PyObject*) &req->headers[i].bsv,
          (PyObject*) &req->values[i]);


    replace_key(env, gPO.http_conlen, gPO.conlen);
    replace_key(env, gPO.http_contype, gPO.contype);

    return env;
  }

  void build_headers(std::vector<char>& buf, bool keep_alive) {
    insert_literal(buf, "HTTP/1.1 ");
    insert_pystr(buf, status_);
    insert_literal(buf, "\r\n");
    insert_str(buf, gRequiredHeaders);
    if(keep_alive)
      insert_literal(buf, "Connection: keep-alive\r\n");
    else
      insert_literal(buf, "Connection: close\r\n");

    Py_ssize_t listlen {PyList_Size(headers_)};
    if(listlen < 0)
      throw std::runtime_error {"Python list error"};

    for(Py_ssize_t i {0}; i < listlen; ++i) {
      PyObject* tuple {PyList_GET_ITEM(headers_, i)};

      PyObject* field {PyTuple_GET_ITEM(tuple, 0)};
      insert_pystr(buf, field);
      insert_literal(buf, ": ");


      PyObject* value {PyTuple_GET_ITEM(tuple, 1)};
      insert_pystr(buf, value);
      insert_literal(buf, "\r\n");
    }
  }

  void build_body(std::vector<char>& buf, PyObject* iter) {
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
