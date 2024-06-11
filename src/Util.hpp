#ifndef VELOCEM_UTIL_HPP
#define VELOCEM_UTIL_HPP

#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

namespace velocem {

template <std::size_t N>
void insert_literal(std::vector<char>& vec, const char (&str)[N]) {
  vec.insert(vec.end(), str, str + N - 1);
}

inline void insert_chars(std::vector<char>& vec, const char* str,
    std::size_t len) {
  vec.insert(vec.end(), str, str + len);
}

inline void insert_str(std::vector<char>& vec, const std::string& str) {
  vec.insert(vec.end(), str.begin(), str.end());
}

inline void insert_pystr(std::vector<char>& vec, PyObject* str) {
  Py_ssize_t len;
  const char* base {PyUnicode_AsUTF8AndSize(str, &len)};
  if(!base)
    throw std::runtime_error {"Python str object error"};
  vec.insert(vec.end(), base, base + len);
}

inline void insert_pybytes(std::vector<char>& vec, PyObject* bytes) {
  char* base;
  Py_ssize_t len;
  if(PyBytes_AsStringAndSize(bytes, &base, &len))
    throw std::runtime_error {"Python bytes object error"};
  vec.insert(vec.end(), base, base + len);
}

inline void insert_pybytes_unchecked(std::vector<char>& vec, PyObject* bytes) {
  char* base {PyBytes_AS_STRING(bytes)};
  Py_ssize_t len {PyBytes_GET_SIZE(bytes)};
  vec.insert(vec.end(), base, base + len);
}

inline std::size_t get_body_list_size(PyObject* list) {
  std::size_t sz {0};
  Py_ssize_t listlen {PyList_GET_SIZE(list)};
  for(Py_ssize_t i {0}; i < listlen; ++i) {
    PyObject* obj {PyList_GET_ITEM(list, i)};
    Py_ssize_t obj_sz {PyBytes_Size(obj)};
    if(obj_sz < 0)
      throw std::runtime_error {"Python bytes object error"};
    sz += obj_sz;
  }
  return sz;
}

inline std::size_t get_body_tuple_size(PyObject* tuple) {
  std::size_t sz {0};
  Py_ssize_t tuplelen {PyTuple_GET_SIZE(tuple)};
  for(Py_ssize_t i {0}; i < tuplelen; ++i) {
    PyObject* obj {PyTuple_GET_ITEM(tuple, i)};
    Py_ssize_t obj_sz {PyBytes_Size(obj)};
    if(obj_sz < 0)
      throw std::runtime_error {"Python bytes object error"};
    sz += obj_sz;
  }
  return sz;
}

inline void replace_key(PyObject* dict, PyObject* oldK, PyObject* newK) {
  PyObject* value {PyDict_GetItem(dict, oldK)};
  if(value) {
    Py_INCREF(value);
    PyDict_DelItem(dict, oldK);
    PyDict_SetItem(dict, newK, value);
    Py_DECREF(value);
  }
}

} // namespace velocem

#endif
