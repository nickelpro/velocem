#ifndef VELOCEM_UTIL_HPP
#define VELOCEM_UTIL_HPP

#include <string>
#include <vector>

#include <asio.hpp>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

namespace velocem {

template <std::size_t N>
void insert_literal(std::vector<char>& vec, const char (&str)[N]) {
  vec.insert(vec.end(), str, str + N - 1);
}

template <std::size_t N> auto buffer_literal(const char (&str)[N]) {
  return asio::buffer(str, N - 1);
}

void unpack_unicode(PyObject* str, const char** base, Py_ssize_t* len,
    const char* err);

void unpack_pybytes(PyObject* bytes, const char** base, Py_ssize_t* len,
    const char* err);

void insert_chars(std::vector<char>& vec, const char* str, std::size_t len);

void insert_str(std::vector<char>& vec, const std::string& str);

void insert_pybytes_unchecked(std::vector<char>& vec, PyObject* bytes);

Py_ssize_t insert_pybytes_unchecked(std::vector<char>& vec, PyObject* bytes,
    Py_ssize_t max);


void insert_pybytes(std::vector<char>& vec, PyObject* bytes, const char* err);

Py_ssize_t insert_pybytes(std::vector<char>& vec, PyObject* bytes,
    Py_ssize_t max, const char* err);

void insert_pystr(std::vector<char>& vec, PyObject* str, const char* err);

std::size_t get_body_list_size(PyObject* list);

std::size_t get_body_tuple_size(PyObject* tuple);

void replace_key(PyObject* dict, PyObject* oldK, PyObject* newK);

void close_iterator(PyObject* iter);

} // namespace velocem

#endif // VELOCEM_UTIL_HPP
