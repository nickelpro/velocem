#include "Input.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <string_view>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "util/BalmStringView.hpp"
#include "util/Constants.hpp"

namespace velocem {

WSGIInput::WSGIInput(std::function<void(WSGIInput*)> f_dealloc)
    : f_dealloc_ {f_dealloc} {
  static std::array<PyMethodDef, 4> meths {
      PyMethodDef {"read", (PyCFunction) read},
      {"readline", (PyCFunction) readline},
      {"readlines", (PyCFunction) readlines},
      {nullptr, nullptr},
  };

  static PyTypeObject type {
      .tp_name = "VelocemWSGIInput",
      .tp_dealloc = (destructor) dealloc,
      .tp_iter = PyObject_SelfIter,
      .tp_iternext = (iternextfunc) iternext,
      .tp_methods = meths.data(),
  };

  ob_refcnt = 0;
  ob_type = &type;
}

void WSGIInput::set_body(char* begin, std::size_t len) {
  it_ = begin;
  end_ = begin + len;
}

void WSGIInput::extend_body(std::size_t len) {
  end_ += len;
}

void WSGIInput::dealloc(WSGIInput* self) {
  self->f_dealloc_(self);
}

PyObject* WSGIInput::read(WSGIInput* self, PyObject* const* args,
    Py_ssize_t nargs) {
  Py_ssize_t size = -1;
  if(!_PyArg_ParseStack(args, nargs, "|n:readline", &size))
    return nullptr;

  if(self->it_ == self->end_)
    return gPO.empty_bytes;

  Py_ssize_t len = self->end_ - self->it_;

  if(size >= 0 && size < len)
    len = size;

  self->it_ += len;
  return PyBytes_FromStringAndSize(self->it_, len);
}

PyObject* WSGIInput::iternext(WSGIInput* self) {
  if(self->it_ == self->end_)
    return nullptr;

  Py_ssize_t len = self->end_ - self->it_;
  const char* cur = self->it_;
  const char* nl = static_cast<const char*>(std::memchr(cur, '\n', len));

  if(nl)
    len = (nl - cur) + 1;

  self->it_ += len;
  return PyBytes_FromStringAndSize(cur, len);
}

PyObject* WSGIInput::readline(WSGIInput* self, PyObject* const* args,
    Py_ssize_t nargs) {
  Py_ssize_t size = -1;
  if(!_PyArg_ParseStack(args, nargs, "|n:readline", &size))
    return nullptr;

  if(self->it_ == self->end_)
    return gPO.empty_bytes;

  Py_ssize_t len = self->end_ - self->it_;
  const char* cur = self->it_;
  const char* nl = static_cast<const char*>(std::memchr(cur, '\n', len));

  if(nl)
    len = (nl - cur) + 1;

  if(size >= 0 && len > size)
    len = size;

  self->it_ += len;
  return PyBytes_FromStringAndSize(cur, len);
}

PyObject* WSGIInput::readlines(WSGIInput* self, PyObject* const* args,
    Py_ssize_t nargs) {
  Py_ssize_t hint = -1;
  if(!_PyArg_ParseStack(args, nargs, "|n:readlines", &hint))
    return nullptr;

  Py_ssize_t total = 0;
  PyObject* list = PyList_New(0);

  for(PyObject* line; (line = iternext(self));) {
    PyList_Append(list, line);
    Py_DECREF(line);
    if(hint > 0) {
      total += PyBytes_GET_SIZE(line);
      if(total >= hint)
        break;
    }
  }
  return list;
}

} // namespace velocem
