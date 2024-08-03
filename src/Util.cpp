#include "Util.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

namespace velocem {

void unpack_unicode(PyObject* str, const char** base, Py_ssize_t* len,
    const char* err) {
  if(!PyUnicode_Check(str)) [[unlikely]] {
    PyErr_SetString(PyExc_TypeError, err);
    throw std::runtime_error {"Python str object error"};
  }

  *base = (char*) PyUnicode_DATA(str);
  *len = PyUnicode_GET_LENGTH(str);
}

void unpack_pybytes(PyObject* bytes, const char** base, Py_ssize_t* len,
    const char* err) {
  if(!PyBytes_Check(bytes)) [[unlikely]] {
    PyErr_SetString(PyExc_TypeError, err);
    throw std::runtime_error {"Python bytes object error"};
  }

  *base = PyBytes_AS_STRING(bytes);
  *len = PyBytes_GET_SIZE(bytes);
}

void insert_chars(std::vector<char>& vec, const char* str, std::size_t len) {
  vec.insert(vec.end(), str, str + len);
}

void insert_str(std::vector<char>& vec, const std::string& str) {
  vec.insert(vec.end(), str.begin(), str.end());
}

void insert_pybytes_unchecked(std::vector<char>& vec, PyObject* bytes) {
  char* base {PyBytes_AS_STRING(bytes)};
  Py_ssize_t len {PyBytes_GET_SIZE(bytes)};
  vec.insert(vec.end(), base, base + len);
}

Py_ssize_t insert_pybytes_unchecked(std::vector<char>& vec, PyObject* bytes,
    Py_ssize_t max) {
  char* base {PyBytes_AS_STRING(bytes)};
  Py_ssize_t len {PyBytes_GET_SIZE(bytes)};
  if(len > max)
    len = max;
  vec.insert(vec.end(), base, base + len);
  return len;
}


void insert_pybytes(std::vector<char>& vec, PyObject* bytes, const char* err) {
  if(!PyBytes_Check(bytes)) [[unlikely]] {
    PyErr_SetString(PyExc_TypeError, err);
    throw std::runtime_error {"Python bytes object error"};
  }
  insert_pybytes_unchecked(vec, bytes);
}

Py_ssize_t insert_pybytes(std::vector<char>& vec, PyObject* bytes,
    Py_ssize_t max, const char* err) {
  if(!PyBytes_Check(bytes)) [[unlikely]] {
    PyErr_SetString(PyExc_TypeError, err);
    throw std::runtime_error {"Python bytes object error"};
  }
  return insert_pybytes_unchecked(vec, bytes, max);
}

void insert_pystr(std::vector<char>& vec, PyObject* str, const char* err) {
  const char* base;
  Py_ssize_t len;
  unpack_unicode(str, &base, &len, err);
  vec.insert(vec.end(), base, base + len);
}

std::size_t get_body_list_size(PyObject* list) {
  std::size_t sz {0};
  Py_ssize_t listlen {PyList_GET_SIZE(list)};
  for(Py_ssize_t i {0}; i < listlen; ++i) {
    PyObject* obj {PyList_GET_ITEM(list, i)};
    if(!PyBytes_Check(obj)) [[unlikely]] {
      PyErr_SetString(PyExc_TypeError, "Response must be Bytes object");
      throw std::runtime_error {"Python bytes object error"};
    }
    sz += PyBytes_GET_SIZE(obj);
  }
  return sz;
}

std::size_t get_body_tuple_size(PyObject* tuple) {
  std::size_t sz {0};
  Py_ssize_t tuplelen {PyTuple_GET_SIZE(tuple)};
  for(Py_ssize_t i {0}; i < tuplelen; ++i) {
    PyObject* obj {PyTuple_GET_ITEM(tuple, i)};
    if(!PyBytes_Check(obj)) [[unlikely]] {
      PyErr_SetString(PyExc_TypeError, "Response must be Bytes object");
      throw std::runtime_error {"Python bytes object error"};
    }
    sz += PyBytes_GET_SIZE(obj);
  }
  return sz;
}

void replace_key(PyObject* dict, PyObject* oldK, PyObject* newK) {
  PyObject* value {PyDict_GetItem(dict, oldK)};
  if(value) {
    Py_INCREF(value);
    PyDict_DelItem(dict, oldK);
    PyDict_SetItem(dict, newK, value);
    Py_DECREF(value);
  }
}

void close_iterator(PyObject* iter) {
  PyObject* close {PyObject_GetAttrString(iter, "close")};
  if(close) {
    PyObject* ret {PyObject_CallNoArgs(close)};
    Py_XDECREF(ret);
    Py_DECREF(close);
  }
}

} // namespace velocem
