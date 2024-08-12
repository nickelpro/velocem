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
    "Server: Velocem/0.0.11\r\nDate: {:%a, %d %b %Y %T} GMT\r\n"};

extern std::string gRequiredHeaders;

struct GlobalPythonObjects {
  PyObject* empty;
  PyObject* empty_bytes;
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
  PyObject* wsgi_input;
  std::array<PyObject*, 46> methods;
};

extern GlobalPythonObjects gPO;

void init_gPO();

struct GlobalVelocemTypes {
  PyTypeObject BalmStringViewType;
};

extern GlobalVelocemTypes gVT;

void init_gVT();

void init_globals();

} // namespace velocem

#endif // VELOCEM_CONSTANTS_HPP
