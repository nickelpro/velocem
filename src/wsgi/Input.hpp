#ifndef VELOCEM_WSGI_INPUT_HPP
#define VELOCEM_WSGI_INPUT_HPP

#include <cstddef>
#include <functional>
#include <string_view>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

namespace velocem {

struct WSGIInput : PyObject {
  WSGIInput(std::function<void(WSGIInput*)> f_dealloc);
  WSGIInput(std::function<void(WSGIInput*)> f_dealloc, std::string_view body);

  void set_body(char* begin, std::size_t len);
  void extend_body(std::size_t len);

  void reset();

private:
  friend void init_gVT(PyObject* mod);
  static void init_type(PyTypeObject* WSGIINputType);

  static void dealloc(WSGIInput* self);

  static PyObject* read(WSGIInput* self, PyObject* const* args,
      Py_ssize_t nargs);

  static PyObject* iternext(WSGIInput* self);

  static PyObject* readline(WSGIInput* self, PyObject* const* args,
      Py_ssize_t nargs);

  static PyObject* readlines(WSGIInput* self, PyObject* const* args,
      Py_ssize_t nargs);

  std::function<void(WSGIInput*)> f_dealloc_;
  char* it_ {nullptr};
  char* end_ {nullptr};
};

} // namespace velocem

#endif // VELOCEM_WSGI_INPUT_HPP
