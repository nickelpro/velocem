#ifndef VELOCEM_CONSTANTS_HPP
#define VELOCEM_CONSTANTS_HPP

#include <array>
#include <chrono>
#include <format>
#include <string>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

namespace velocem {

constexpr char gRequiredHeadersFormat[] {
    "Server: Velocem/0.0.8\r\nDate: {:%a, %d %b %Y %T} GMT\r\n"};

inline std::string gRequiredHeaders {std::format(gRequiredHeadersFormat,
    std::chrono::floor<std::chrono::seconds>(
        std::chrono::system_clock::now()))};

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

} // namespace velocem

#endif // VELOCEM_CONSTANTS_HPP
