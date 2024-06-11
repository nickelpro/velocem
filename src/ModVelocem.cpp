#include <array>
#include <chrono>
#include <csignal>
#include <exception>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <asio.hpp>

#include "HTTPParser.hpp"
#include "Request.hpp"
#include "WSGI.hpp"

using asio::awaitable;
using asio::deferred;
using asio::detached;
using asio::ip::tcp;
namespace this_coro = asio::this_coro;

namespace velocem {

asio::awaitable<void> timer_header_update(asio::steady_timer& timer) {

  static std::chrono::seconds interval {1};

  for(;;) {
    timer.expires_from_now(interval);
    co_await timer.async_wait(deferred);
    gRequiredHeaders = std::format(gRequiredHeadersFormat,
        std::chrono::floor<std::chrono::seconds>(
            std::chrono::system_clock::now()));
  }
}

asio::awaitable<void> client(tcp::socket s, PythonApp& app) {
  Request* req {new Request};
  Request* next_req {nullptr};
  HTTPParser http {req};

  try {
    for(;;) {
      std::size_t offset {0};

      if(next_req) {
        std::swap(req, next_req);
        std::size_t n {req->buf.size()};
        http.resume(req, req->get_parse_buf(0, n));
        offset += n;
      }

      while(!http.done()) {
        size_t n {
            co_await s.async_read_some(req->get_read_buf(offset), deferred)};
        http.parse(req->get_parse_buf(offset, n));
        offset += n;
      }

      if(http.keep_alive()) {
        next_req = new Request;
        auto rm {http.get_rem(offset)};
        next_req->buf.resize(rm.size());
        std::memcpy(next_req->buf.data(), rm.data(), rm.size());
      }

      auto appret {
          app.run(req, http.http_minor, http.method, http.keep_alive())};
      req = nullptr;

      if(!appret) {
        co_await s.async_send(
            asio::buffer("HTTP/1.1 500 Internal Server Error\r\n\r\n", 38),
            deferred);
      } else {
        co_await s.async_send(asio::buffer(*appret), deferred);
      }

      if(!http.keep_alive() || !appret) {
        asio::error_code ec;
        s.shutdown(s.shutdown_both, ec);
        s.close(ec);
        break;
      }
    }
  } catch(...) {
    asio::error_code ec;
    s.shutdown(s.shutdown_both, ec);
    s.close(ec);
  }

  if(req)
    delete req;

  if(next_req)
    delete next_req;
}

asio::awaitable<void> listener(tcp::endpoint ep, auto& app) {
  auto executor {co_await asio::this_coro::executor};
  tcp::acceptor acceptor {executor, ep};

  for(;;) {
    tcp::socket socket {co_await acceptor.async_accept(deferred)};
    asio::co_spawn(executor, client(std::move(socket), app), detached);
  }
}

void accept(asio::execution::executor auto ex, std::string_view host,
    std::string_view port, auto& app) {
  for(auto re : tcp::resolver {ex}.resolve(host, port)) {
    auto ep {re.endpoint()};
    PySys_WriteStdout("Listening on: http://%s:%s\n",
        ep.address().to_string().c_str(), std::to_string(ep.port()).c_str());
    asio::co_spawn(ex, listener(std::move(ep), app), detached);
  }
}

PyObject* run(PyObject*, PyObject* const* args, Py_ssize_t nargs,
    PyObject* kwnames) {
  static const char* keywords[] {"app", "host", "port", nullptr};
  static _PyArg_Parser parser {.format = "O|ss:run", .keywords = keywords};

  PyObject* appObj;
  const char* host {"localhost"};
  const char* port {"8000"};

  if(!_PyArg_ParseStackAndKeywords(args, nargs, kwnames, &parser, &appObj,
         &host, &port))
    return nullptr;

  Py_IncRef(appObj);

  auto old_sigint {std::signal(SIGINT, SIG_DFL)};

  asio::io_context io {1};

  asio::signal_set signals {io, SIGINT};
  signals.async_wait([&](auto, auto) {
    PyErr_SetInterrupt();
    io.stop();
  });

  asio::steady_timer systimer {io};
  asio::co_spawn(io, timer_header_update(systimer), detached);

  PythonApp app {appObj, host, port};
  accept(io.get_executor(), host, port, app);

  io.run();

  signals.cancel();
  signals.clear();
  std::signal(SIGINT, old_sigint);

  Py_DecRef(appObj);
  Py_RETURN_NONE;
}

} // namespace velocem

static PyMethodDef VelocemMethods[] {
    {"run", (PyCFunction) velocem::run, METH_FASTCALL | METH_KEYWORDS},
    {0},
};

static PyModuleDef VelocemModule {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "velocem",
    .m_doc = "Hyperspeed Python Web Framework",
    .m_size = -1,
    .m_methods = VelocemMethods,
};

PyMODINIT_FUNC PyInit_velocem(void) {
  auto mod {PyModule_Create(&VelocemModule)};
  if(!mod)
    return nullptr;
  if(PyModule_AddStringConstant(mod, "__version__", "0.0.5") == -1)
    return nullptr;
  return mod;
}
