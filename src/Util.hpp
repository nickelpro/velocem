#ifndef VELOCEM_UTIL_HPP
#define VELOCEM_UTIL_HPP

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#ifdef _MSC_VER
#include <string.h>
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

namespace velocem {

template <std::size_t N>
void insert_literal(std::vector<char>& vec, const char (&str)[N]) {
  vec.insert(vec.end(), str, str + N - 1);
}

inline void unpack_unicode(PyObject* str, const char** base, Py_ssize_t* len,
    const char* err) {
  if(!PyUnicode_Check(str)) [[unlikely]] {
    PyErr_SetString(PyExc_TypeError, err);
    throw std::runtime_error {"Python str object error"};
  }

  *base = (char*) PyUnicode_DATA(str);
  *len = PyUnicode_GET_LENGTH(str);
}

inline void insert_chars(std::vector<char>& vec, const char* str,
    std::size_t len) {
  vec.insert(vec.end(), str, str + len);
}

inline void insert_str(std::vector<char>& vec, const std::string& str) {
  vec.insert(vec.end(), str.begin(), str.end());
}

inline void insert_pystr(std::vector<char>& vec, PyObject* str,
    const char* err) {
  const char* base;
  Py_ssize_t len;
  unpack_unicode(str, &base, &len, err);
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
    if(!PyBytes_Check(obj)) [[unlikely]] {
      PyErr_SetString(PyExc_TypeError, "Response must be Bytes object");
      throw std::runtime_error {"Python bytes object error"};
    }
    sz += PyBytes_GET_SIZE(obj);
  }
  return sz;
}

inline std::size_t get_body_tuple_size(PyObject* tuple) {
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
