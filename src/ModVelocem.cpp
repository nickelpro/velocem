#include <array>
#include <chrono>
#include <csignal>
#include <exception>
#include <format>
#include <optional>
#include <queue>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <asio.hpp>

#include "HTTPParser.hpp"
#include "plat/plat.hpp"
#include "Request.hpp"
#include "WSGI.hpp"

using asio::awaitable;
using asio::deferred;
using asio::detached;
using asio::ip::tcp;
namespace this_coro = asio::this_coro;

namespace velocem {

struct {
  std::queue<QueuedRequest*> q;

  Request* pop() {
    if(q.empty())
      return static_cast<Request*>(new QueuedRequest(q));
    auto ptr = q.front();
    q.pop();
    return static_cast<Request*>(ptr);
  }
} ReqQ;

asio::awaitable<void> handle_iter(tcp::socket& s, PyAppRet& app) {
  co_await s.async_send(asio::buffer(app.buf), deferred);

  for(PyObject* next; (next = PyIter_Next(app.iter));) {
    try {
      app.buf.clear();

      const char* base;
      Py_ssize_t len;
      unpack_pybytes(next, &base, &len,
          "Body iterator must produce bytes objects");

      if(len) [[likely]] {
        insert_str(app.buf, std::format("{:x}\r\n", len));
        insert_chars(app.buf, base, len);
        insert_literal(app.buf, "\r\n");
        co_await s.async_send(asio::buffer(app.buf), deferred);
      }

      Py_DECREF(next);
    } catch(...) {
      close_iterator(app.iter);
      Py_DECREF(next);
      Py_DECREF(app.iter);
      throw;
    }
  }

  close_iterator(app.iter);
  Py_DECREF(app.iter);

  if(PyErr_Occurred()) {
    PyErr_Print();
    PyErr_Clear();
    throw std::runtime_error {"Python iterator error"};
  }

  co_await s.async_send(buffer_literal("0\r\n\r\n"), deferred);
}

asio::awaitable<void> client(tcp::socket s, PythonApp& app) {
  Request* req {ReqQ.pop()};
  Request* next_req {nullptr};
  HTTPParser http {req};

  try {
    for(;;) {
      std::size_t off {0};

      if(next_req) {
        std::swap(req, next_req);
        std::size_t n {req->buf_.size()};
        http.resume(req, req->get_parse_buf(0, n));
        off += n;
      }

      while(!http.done()) {
        size_t n {co_await s.async_read_some(req->get_read_buf(off), deferred)};
        http.parse(req->get_parse_buf(off, n));
        off += n;
      }

      if(http.keep_alive()) {
        next_req = ReqQ.pop();
        auto rm {http.get_rem(off)};
        next_req->buf_.resize(rm.size());
        std::memcpy(next_req->buf_.data(), rm.data(), rm.size());
      }

      auto res {app.run(req, http.http_minor, http.method, http.keep_alive())};
      req = nullptr;

      if(res) [[likely]] {
        if(!res->iter) {
          co_await s.async_send(asio::buffer(res->buf), deferred);
        } else {
          co_await handle_iter(s, *res);
        }
      } else {
        co_await s.async_send(
            buffer_literal("HTTP/1.1 500 Internal Server Error\r\n\r\n"),
            deferred);
      }

      if(!http.keep_alive() || !res) {
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

asio::awaitable<void> listener(tcp::endpoint ep, int reuseport, auto& app) {
  auto executor {co_await asio::this_coro::executor};
  tcp::acceptor acceptor {executor};
  acceptor.open(ep.protocol());
  if(reuseport)
    set_reuse_port(acceptor);

  acceptor.set_option(tcp::acceptor::reuse_address {true});
  acceptor.bind(ep);
  acceptor.listen();

  for(;;) {
    tcp::socket socket {co_await acceptor.async_accept(deferred)};
    asio::co_spawn(executor, client(std::move(socket), app), detached);
  }
}

void accept(asio::execution::executor auto ex, std::string_view host,
    std::string_view port, int reuseport, auto& app) {
  for(auto re : tcp::resolver {ex}.resolve(host, port)) {
    auto ep {re.endpoint()};
    PySys_WriteStdout("Listening on: http://%s:%s\n",
        ep.address().to_string().c_str(), std::to_string(ep.port()).c_str());
    asio::co_spawn(ex, listener(std::move(ep), reuseport, app), detached);
  }
}

asio::awaitable<void> handle_header(asio::io_context& io) {
  static std::chrono::seconds interval {1};

  asio::steady_timer timer {io};

  for(;;) {
    timer.expires_from_now(interval);
    co_await timer.async_wait(deferred);
    gRequiredHeaders = std::format(gRequiredHeadersFormat,
        std::chrono::floor<std::chrono::seconds>(
            std::chrono::system_clock::now()));
  }
}

asio::awaitable<void> handle_signals(asio::io_context& io) {
  auto old_sigint {std::signal(SIGINT, SIG_DFL)};
  auto old_sigterm {std::signal(SIGTERM, SIG_DFL)};

  asio::signal_set signals {io};
  signals.add(SIGINT);
  signals.add(SIGTERM);

  for(;;) {
    int sig {co_await signals.async_wait(deferred)};
    PyErr_SetInterruptEx(sig);
    if(PyErr_CheckSignals()) {
      io.stop();
      break;
    }
  }

  signals.clear();
  std::signal(SIGINT, old_sigint);
  std::signal(SIGTERM, old_sigterm);
}

PyObject* run(PyObject*, PyObject* const* args, Py_ssize_t nargs,
    PyObject* kwnames) {
  static const char* keywords[] {"app", "host", "port", "reuseport", nullptr};
  static _PyArg_Parser parser {.format = "O|ssp:run", .keywords = keywords};

  PyObject* appObj;
  const char* host {"localhost"};
  const char* port {"8000"};
  int reuseport {0};

  if(!_PyArg_ParseStackAndKeywords(args, nargs, kwnames, &parser, &appObj,
         &host, &port, &reuseport))
    return nullptr;

  Py_IncRef(appObj);

  asio::io_context io {1};
  asio::co_spawn(io, handle_signals(io), detached);
  asio::co_spawn(io, handle_header(io), detached);

  PythonApp app {appObj, host, port};
  accept(io.get_executor(), host, port, reuseport, app);
  io.run();

  Py_DecRef(appObj);

  // There is no way to exit the run loop that isn't via an exception being set
  return nullptr;
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
  if(PyModule_AddStringConstant(mod, "__version__", "0.0.8") == -1)
    return nullptr;
  return mod;
}
