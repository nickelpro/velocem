#include <array>
#include <csignal>
#include <exception>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <asio.hpp>

#include "HTTPParser.hpp"
#include "Request.hpp"

using asio::awaitable;
using asio::deferred;
using asio::detached;
using asio::ip::tcp;
namespace this_coro = asio::this_coro;

namespace velocem {

struct PyResult {
  PyObject* status_obj;
  std::string_view status;

  PyObject* headers_obj;
  std::vector<std::pair<std::string_view, std::string_view>> headers;

  std::vector<PyObject*> body_objs;
  std::vector<std::string_view> body;
  std::size_t total_size;
};


struct {
  PyObject* empty {PyUnicode_New(0, 0)};
  PyObject* query {PyUnicode_FromString("QUERY_STRING")};
  PyObject* path {PyUnicode_FromString("PATH_INFO")};
  PyObject* proto {PyUnicode_FromString("SERVER_PROTOCOL")};
  PyObject* http10 {PyUnicode_FromString("HTTP/1.0")};
  PyObject* http11 {PyUnicode_FromString("HTTP/1.1")};
  PyObject* http_conlen {PyUnicode_FromString("HTTP_CONTENT_LENGTH")};
  PyObject* conlen {PyUnicode_FromString("CONTENT_LENGTH")};
  PyObject* http_contype {PyUnicode_FromString("HTTP_CONTENT_TYPE")};
  PyObject* contype {PyUnicode_FromString("CONTENT_TYPE")};
  PyObject* meth {PyUnicode_FromString("REQUEST_METHOD")};
#define HTTP_METHOD(c, n) PyUnicode_FromString(#n),
  std::array<PyObject*, 46> methods {
#include "defs/http_method.def"
  };
#undef HTTP_METHOD

} gPO;

template <asio::execution::executor Ex> struct PythonApp {
  PythonApp(Ex ex, PyObject* app, const char* host, const char* port)
      : ex_ {std::move(ex)}, app_ {app}, vecCall_ {PyVectorcall_Function(app)} {

    static PyMethodDef srdef {
        .ml_name = "start_response",
        .ml_meth = (PyCFunction) start_response_tr,
        .ml_flags = METH_FASTCALL | METH_KEYWORDS,
    };

    cap_ = PyCapsule_New(this, NULL, NULL);
    sr_ = PyCFunction_New(&srdef, cap_);
    baseEnv_ = _PyDict_NewPresized(50);

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

  PyResult run(Request* req, int http_minor, int meth, PyResult& pr) {
    if(pr.status_obj)
      decref_pyresult(pr);

    PyResult ret {};

    auto env {make_env(req, http_minor, meth)};

    status_ = nullptr;

    PyObject* iter;

    if(vecCall_) {
      std::array args {env, sr_};
      iter = vecCall_(app_, args.data(), args.size(), NULL);
    } else {
      iter = PyObject_CallFunctionObjArgs(app_, env, sr_, nullptr);
    }

    Py_DECREF(env);

    ret.headers_obj = headers_;
    ret.status_obj = status_;

    char* base = (char*) PyUnicode_DATA(status_);
    std::size_t len = (std::size_t) PyUnicode_GET_LENGTH(status_);
    ret.status = {base, len};


    Py_ssize_t listlen = PyList_GET_SIZE(headers_);
    ret.headers.reserve(listlen);
    for(Py_ssize_t i {0}; i < listlen; ++i) {
      PyObject* tuple {PyList_GET_ITEM(headers_, i)};

      PyObject* field {PyTuple_GET_ITEM(tuple, 0)};
      char* f_data {(char*) PyUnicode_DATA(field)};
      std::size_t f_len {(std::size_t) PyUnicode_GET_LENGTH(field)};

      PyObject* value {PyTuple_GET_ITEM(tuple, 1)};
      char* v_data {(char*) PyUnicode_DATA(value)};
      std::size_t v_len {(std::size_t) PyUnicode_GET_LENGTH(value)};

      ret.headers.emplace_back(std::string_view {f_data, f_len},
          std::string_view {v_data, v_len});
    }

    if(PyBytes_Check(iter)) {
      ret.body_objs.push_back(iter);
      base = PyBytes_AS_STRING(iter);
      len = (std::size_t) PyBytes_GET_SIZE(iter);
      ret.body.emplace_back(base, len);
      ret.total_size = len;
    } else if(PyList_Check(iter)) {
      listlen = PyList_GET_SIZE(iter);
      ret.body_objs.reserve(listlen);
      for(Py_ssize_t i {0}; i < listlen; ++i) {
        PyObject* obj {PyList_GET_ITEM(iter, i)};
        Py_INCREF(obj);
        ret.body_objs.push_back(obj);
        base = PyBytes_AS_STRING(obj);
        len = (std::size_t) PyBytes_GET_SIZE(obj);
        ret.body.emplace_back(base, len);
        ret.total_size += len;
      }
      Py_DecRef(iter);
    } else if(PyTuple_Check(iter)) {
      listlen = PyTuple_GET_SIZE(iter);
      ret.body_objs.reserve(listlen);
      for(Py_ssize_t i {0}; i < listlen; ++i) {
        PyObject* obj {PyTuple_GET_ITEM(iter, i)};
        Py_INCREF(obj);
        ret.body_objs.push_back(obj);
        base = PyBytes_AS_STRING(obj);
        len = (std::size_t) PyBytes_GET_SIZE(obj);
        ret.body.emplace_back(base, len);
        ret.total_size += len;
      }
      Py_DecRef(iter);
    } else {
      // TODO: iterable obj
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

  void decref_pyresult(PyResult& pr) {
    Py_DECREF(pr.status_obj);
    Py_DECREF(pr.headers_obj);
    for(auto pyobj : pr.body_objs)
      Py_DECREF(pyobj);
  }

  static void replace_key(PyObject* dict, PyObject* oldK, PyObject* newK) {
    PyObject* value {PyDict_GetItem(dict, oldK)};
    if(value) {
      Py_INCREF(value);
      PyDict_DelItem(dict, oldK);
      PyDict_SetItem(dict, newK, value);
      Py_DECREF(value);
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
    auto pyapp {PyCapsule_GetPointer(self, NULL)};
    return ((PythonApp*) pyapp)->start_response(args, nargs, kwnames);
  }

  Ex ex_;
  PyObject* app_;
  PyObject* baseEnv_;
  vectorcallfunc vecCall_;
  PyObject* cap_;
  PyObject* sr_;

  PyObject* status_;
  PyObject* headers_;
};

asio::awaitable<void> client(tcp::socket s, auto& app) {


  PyResult pr {};
  Request* req {new Request};
  Request* next_req {nullptr};
  HTTPParser http {req};

  try {
    for(;;) {
      size_t offset {0};

      if(next_req) {
        req = next_req;
        next_req = nullptr;

        std::size_t n {req->buf.size()};
        http.resume(req, req->get_parse_buf(0, n));
        offset += n;
      }

      while(!http.done()) {
        size_t n {
            co_await s.async_read_some(req->get_read_buf(offset), deferred)};
        http.parse(req->get_parse_buf(offset, n));
        offset += n;
      }

      if(http.keep_alive()) {
        next_req = new Request;
        auto rm {http.get_rem(offset)};
        next_req->buf.resize(rm.size());
        std::memcpy(next_req->buf.data(), rm.data(), rm.size());
      }

      pr = app.run(req, http.http_minor, http.method, pr);
      req = nullptr;

      // THIS IS 7us FASTER THAN VECTORED I/O
      //     !!! RETVRN TO TRADITION !!!
      std::vector<char> send_buf;
      send_buf.reserve(1000);

      static char ht[] = "HTTP/1.1 ";
      send_buf.insert(send_buf.end(), ht, ht + 9);
      send_buf.insert(send_buf.end(), pr.status.begin(), pr.status.end());

      static char rn[] = "\r\n";
      send_buf.insert(send_buf.end(), rn, rn + 2);


      static char cl[] = ": ";
      for(const auto& hdr : pr.headers) {
        send_buf.insert(send_buf.end(), hdr.first.begin(), hdr.first.end());
        send_buf.insert(send_buf.end(), cl, cl + 2);
        send_buf.insert(send_buf.end(), hdr.second.begin(), hdr.second.end());
        send_buf.insert(send_buf.end(), rn, rn + 2);
      }
      static char col[] = "Content-Length: ";
      send_buf.insert(send_buf.end(), col, col + 16);
      std::string conlen {std::to_string(pr.total_size)};
      send_buf.insert(send_buf.end(), conlen.begin(), conlen.end());

      static char rnrn[] = "\r\n\r\n";
      send_buf.insert(send_buf.end(), rnrn, rnrn + 4);

      for(const auto& part : pr.body)
        send_buf.insert(send_buf.end(), part.begin(), part.end());

      co_await s.async_send(asio::buffer(send_buf), deferred);

      if(!http.keep_alive()) {
        asio::error_code ec;
        s.shutdown(s.shutdown_both, ec);
        s.close(ec);
        break;
      }
    }
  } catch(std::exception& e) {
    asio::error_code ec;
    s.shutdown(s.shutdown_both, ec);
    s.close(ec);
  }

  if(req)
    delete req;

  if(next_req)
    delete next_req;

  if(pr.status_obj)
    app.decref_pyresult(pr);
}

asio::awaitable<void> listener(tcp::endpoint ep, auto& app) {
  auto executor {co_await asio::this_coro::executor};
  tcp::acceptor acceptor {executor, ep};

  for(;;) {
    tcp::socket socket {co_await acceptor.async_accept(deferred)};
    asio::co_spawn(executor, client(std::move(socket), app), detached);
  }
}

void accept(asio::execution::executor auto ex, std::string_view host,
    std::string_view port, auto& app) {
  for(auto re : tcp::resolver {ex}.resolve(host, port)) {
    auto ep {re.endpoint()};
    PySys_WriteStdout("Listening on: http://%s:%s\n",
        ep.address().to_string().c_str(), std::to_string(ep.port()).c_str());
    asio::co_spawn(ex, listener(std::move(ep), app), detached);
  }
}

PyObject* run(PyObject*, PyObject* const* args, Py_ssize_t nargs,
    PyObject* kwnames) {
  static const char* keywords[] {"app", "host", "port", nullptr};
  static _PyArg_Parser parser {.format = "O|ss:run", .keywords = keywords};

  PyObject* appObj;
  const char* host = "localhost";
  const char* port = "8000";

  if(!_PyArg_ParseStackAndKeywords(args, nargs, kwnames, &parser, &appObj,
         &host, &port))
    return nullptr;

  Py_IncRef(appObj);

  auto old_sigint {std::signal(SIGINT, SIG_DFL)};

  asio::io_context io {1};
  asio::signal_set signals {io, SIGINT};
  signals.async_wait([&](auto, auto) {
    PyErr_SetInterrupt();
    io.stop();
  });

  PythonApp app {asio::make_strand(io), appObj, host, port};
  accept(io.get_executor(), host, port, app);

  io.run();

  signals.cancel();
  signals.clear();
  std::signal(SIGINT, old_sigint);

  Py_DecRef(appObj);
  Py_RETURN_NONE;
}

} // namespace velocem

static PyMethodDef VelocemMethods[] {
    {"run", (PyCFunction) velocem::run, METH_FASTCALL | METH_KEYWORDS},
    {0},
};

static PyModuleDef VelocemModule {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "velocem",
    .m_doc = "Hyperspeed Python Web Framework",
    .m_size = -1,
    .m_methods = VelocemMethods,
};

PyMODINIT_FUNC PyInit_velocem(void) {
  auto mod {PyModule_Create(&VelocemModule)};
  if(!mod)
    return nullptr;
  if(PyModule_AddStringConstant(mod, "__version__", "0.0.5") == -1)
    return nullptr;
  return mod;
}
