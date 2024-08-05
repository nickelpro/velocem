#ifndef VELOCEM_WSGIAPP_HPP
#define VELOCEM_WSGIAPP_HPP

#include <optional>
#include <vector>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "Request.hpp"

namespace velocem {

struct WSGIAppRet {
  void reset();

  std::vector<char> buf;
  PyObject* iter {nullptr};
  std::optional<Py_ssize_t> conlen;
};

void push_WSGIAppRet(WSGIAppRet* appret);

struct WSGIApp {
  WSGIApp(PyObject* app, const char* host, const char* port);

  WSGIApp(WSGIApp&) = delete;
  WSGIApp(WSGIApp&&) = delete;

  ~WSGIApp();

  WSGIAppRet* run(Request* req, int http_minor, int meth, bool keepalive);

private:
  PyObject* make_env(Request* req, int http_minor, int meth);

  std::optional<Py_ssize_t> build_headers(std::vector<char>& buf,
      bool keep_alive);

  PyObject* start_response(PyObject* const* args, Py_ssize_t nargs,
      PyObject* kwnames);

  static PyObject* start_response_tr(PyObject* self, PyObject* const* args,
      Py_ssize_t nargs, PyObject* kwnames);

  PyObject* write_cb(PyObject* const* args, Py_ssize_t nargs);

  static PyObject* write_cb_tr(PyObject* self, PyObject* const* args,
      Py_ssize_t nargs);

  bool in_handle;
  std::vector<char> writebuf_;

  PyObject* app_;
  PyObject* baseEnv_;
  vectorcallfunc vecCall_;
  PyObject* cap_;
  PyObject* sr_;
  PyObject* wcb_;

  PyObject* status_;
  PyObject* headers_;
};

} // namespace velocem

#endif // VELOCEM_WSGIAPP_HPP
