#ifndef VELOCEM_CONSTANTS_HPP
#define VELOCEM_CONSTANTS_HPP

#include <array>
#include <chrono>
#include <format>
#include <string>
#include <string_view>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

namespace velocem {

enum class HTTPMethod {
  INVALID = -1,
  Delete,
  Get,
  Head,
  Post,
  Put,
  Connect,
  Options,
  Trace,
  Copy,
  Lock,
  MkCol,
  Move,
  PropFind,
  PropPatch,
  Search,
  Unlock,
  Bind,
  Rebind,
  Unbind,
  ACL,
  Report,
  MkActivity,
  Checkout,
  Merge,
  MSearch,
  Notify,
  Subscribe,
  Unsubscribe,
  Patch,
  Purge,
  MkCalendar,
  Link,
  Unlink,
  Source,
  Pri,
  Describe,
  Announce,
  Setup,
  Play,
  Pause,
  Teardown,
  Get_Parameter,
  Set_Parameter,
  Redirect,
  Record,
  Flush,
  Query,
  MAX,
};

HTTPMethod str2meth(std::string_view str);

constexpr char gRequiredHeadersFormat[] {
    "Server: Velocem/0.0.12\r\nDate: {:%a, %d %b %Y %T} GMT\r\n"};

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
  std::array<PyObject*, 47> methods;
};

extern GlobalPythonObjects gPO;

void init_gPO();

struct GlobalVelocemTypes {
  PyTypeObject BalmStringViewType;
  PyTypeObject RouterType;
  PyTypeObject WSGIInputType;
};

extern GlobalVelocemTypes gVT;

void init_gVT(PyObject* mod);

void init_globals(PyObject* mod);

} // namespace velocem

#endif // VELOCEM_CONSTANTS_HPP
