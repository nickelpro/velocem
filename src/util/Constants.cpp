#include "Constants.hpp"

#include <array>
#include <chrono>
#include <format>
#include <string>

#include "BalmStringView.hpp"

namespace velocem {

std::string gRequiredHeaders {std::format(gRequiredHeadersFormat,
    std::chrono::floor<std::chrono::seconds>(
        std::chrono::system_clock::now()))};


GlobalPythonObjects gPO;

void init_gPO() {
  gPO.empty = PyUnicode_New(0, 0);
  gPO.empty_bytes = PyBytes_FromStringAndSize(nullptr, 0);
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
  gPO.wsgi_input = PyUnicode_FromString("wsgi.input");
#define HTTP_METHOD(c, n) PyUnicode_FromString(#n),
  gPO.methods = {
#include "defs/http_method.def"
  };
}

GlobalVelocemTypes gVT;

void init_gVT() {
  gVT.BalmStringViewType = PyUnicode_Type;
  gVT.BalmStringViewType.tp_new = nullptr;
  gVT.BalmStringViewType.tp_free = nullptr;
  gVT.BalmStringViewType.tp_dealloc = BalmStringView::dealloc;
}

void init_globals() {
  init_gPO();
  init_gVT();
}

} // namespace velocem
