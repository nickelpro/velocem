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
    "Server: Velocem/0.0.10\r\nDate: {:%a, %d %b %Y %T} GMT\r\n"};

inline std::string gRequiredHeaders {std::format(gRequiredHeadersFormat,
    std::chrono::floor<std::chrono::seconds>(
        std::chrono::system_clock::now()))};

struct {
  PyObject* empty;
  PyObject* query;
  PyObject* path;
  PyObject* proto;
  PyObject* http;
  PyObject* http10;
  PyObject* http11;
  PyObject* http_conlen;
  PyObject* conlen;
  PyObject* http_contype;
  PyObject* contype;
  PyObject* meth;
  PyObject* wsgi_ver;
  std::array<PyObject*, 46> methods;
} gPO;

inline void init_gPO() {
  gPO.empty = PyUnicode_New(0, 0);
  gPO.query = PyUnicode_FromString("QUERY_STRING");
  gPO.path = PyUnicode_FromString("PATH_INFO");
  gPO.proto = PyUnicode_FromString("SERVER_PROTOCOL");
  gPO.http = PyUnicode_FromString("http");
  gPO.http10 = PyUnicode_FromString("HTTP/1.0");
  gPO.http11 = PyUnicode_FromString("HTTP/1.1");
  gPO.http_conlen = PyUnicode_FromString("HTTP_CONTENT_LENGTH");
  gPO.conlen = PyUnicode_FromString("CONTENT_LENGTH");
  gPO.http_contype = PyUnicode_FromString("HTTP_CONTENT_TYPE");
  gPO.contype = PyUnicode_FromString("CONTENT_TYPE");
  gPO.meth = PyUnicode_FromString("REQUEST_METHOD");
  gPO.wsgi_ver = PyTuple_Pack(2, PyLong_FromLong(1), PyLong_FromLong(0));
#define HTTP_METHOD(c, n) PyUnicode_FromString(#n),
  gPO.methods = {
#include "defs/http_method.def"
  };
}

} // namespace velocem

#endif // VELOCEM_CONSTANTS_HPP
