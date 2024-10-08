#include "Constants.hpp"

#include <array>
#include <chrono>
#include <format>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"

#include "BalmStringView.hpp"
#include "Router.hpp"
#include "wsgi/Input.hpp"

using std::operator""sv;

namespace velocem {

std::string gRequiredHeaders {std::format(gRequiredHeadersFormat,
    std::chrono::floor<std::chrono::seconds>(
        std::chrono::system_clock::now()))};

namespace {
#define viewify(n) #n##sv
#define HTTP_METHOD(c, n) {#n##sv, static_cast<HTTPMethod>(c)},
static const absl::flat_hash_map<std::string_view, HTTPMethod> methmap {
#include "defs/http_method.def"
};
#undef HTTP_METHOD
} // namespace

HTTPMethod str2meth(std::string_view str) {
  if(auto it = methmap.find(str); it != methmap.end())
    return it->second;
  return HTTPMethod::INVALID;
}

GlobalPythonObjects gPO;

void init_gPO() {
  gPO.empty = PyUnicode_InternFromString("");
  gPO.empty_bytes = PyBytes_FromStringAndSize(nullptr, 0);
  gPO.query = PyUnicode_InternFromString("QUERY_STRING");
  gPO.path = PyUnicode_InternFromString("PATH_INFO");
  gPO.proto = PyUnicode_InternFromString("SERVER_PROTOCOL");
  gPO.http = PyUnicode_InternFromString("http");
  gPO.http10 = PyUnicode_InternFromString("HTTP/1.0");
  gPO.http11 = PyUnicode_InternFromString("HTTP/1.1");
  gPO.http_conlen = PyUnicode_InternFromString("HTTP_CONTENT_LENGTH");
  gPO.conlen = PyUnicode_InternFromString("CONTENT_LENGTH");
  gPO.http_contype = PyUnicode_InternFromString("HTTP_CONTENT_TYPE");
  gPO.contype = PyUnicode_InternFromString("CONTENT_TYPE");
  gPO.meth = PyUnicode_InternFromString("REQUEST_METHOD");
  gPO.wsgi_ver = PyTuple_Pack(2, PyLong_FromLong(1), PyLong_FromLong(0));
  gPO.wsgi_input = PyUnicode_InternFromString("wsgi.input");
  gPO.close = PyUnicode_InternFromString("close");
  gPO.velocem_caps = PyUnicode_InternFromString("velocem.captures");
#define HTTP_METHOD(c, n) PyUnicode_InternFromString(#n),
  gPO.methods = {
#include "defs/http_method.def"
  };
#undef HTTP_METHOD
}

GlobalVelocemTypes gVT;

void init_gVT(PyObject* mod) {
  BalmStringView::init_type(&gVT.BalmStringViewType);
  WSGIInput::init_type(&gVT.WSGIInputType);

  Router::init_type(&gVT.RouterType);
  PyModule_AddType(mod, &gVT.RouterType);
}

void init_globals(PyObject* mod) {
  init_gPO();
  init_gVT(mod);
}

} // namespace velocem
