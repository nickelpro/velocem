#include "Server.hpp"

#include <chrono>
#include <csignal>
#include <format>
#include <queue>
#include <string_view>
#include <utility>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <asio.hpp>

#include "HTTPParser.hpp"
#include "plat/plat.hpp"
#include "Request.hpp"
#include "util/Constants.hpp"
#include "util/Util.hpp"

#include "App.hpp"

using asio::awaitable;
using asio::deferred;
using asio::detached;
using asio::ip::tcp;
namespace this_coro = asio::this_coro;

namespace velocem {

namespace {

struct {
  std::queue<WSGIRequest*> q;

  WSGIRequest* pop() {
    if(q.empty())
      return new WSGIRequest {[&](WSGIRequest* self) {
        self->reset();
        q.push(self);
      }};
    auto ptr = q.front();
    q.pop();
    return ptr;
  }

  void push(WSGIRequest* ptr) {
    ptr->reset();
    q.push(ptr);
  }

} ReqQ;

asio::awaitable<void> handle_iter(tcp::socket& s, WSGIAppRet& app) {
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

asio::awaitable<void> client(tcp::socket s, WSGIApp& app) {
  WSGIRequest* req {ReqQ.pop()};
  WSGIRequest* next_req {nullptr};
  WSGIAppRet* app_ret {nullptr};
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


      WSGIRequest* tmp = req;
      req = nullptr;
      app_ret = app.run(tmp, http.http_minor, http.method, http.keep_alive());

      if(app_ret) [[likely]] {
        if(!app_ret->iter) {
          co_await s.async_send(asio::buffer(app_ret->buf), deferred);
        } else {
          co_await handle_iter(s, *app_ret);
        }
      } else {
        co_await s.async_send(
            buffer_literal("HTTP/1.1 500 Internal Server Error\r\n\r\n"),
            deferred);
      }

      if(!http.keep_alive() || !app_ret) {
        asio::error_code ec;
        s.shutdown(s.shutdown_both, ec);
        s.close(ec);
        break;
      }

      push_WSGIAppRet(app_ret);
      app_ret = nullptr;
    }
  } catch(...) {
    asio::error_code ec;
    s.shutdown(s.shutdown_both, ec);
    s.close(ec);
  }

  if(app_ret)
    push_WSGIAppRet(app_ret);

  if(req)
    ReqQ.push(req);

  if(next_req)
    ReqQ.push(next_req);
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
  constexpr std::chrono::seconds interval {1};

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

constexpr const char* _rs_keywords[] {"app", "host", "port", "reuseport",
    nullptr};
_PyArg_Parser _rs_parser {.format = "O|ssp:run", .keywords = _rs_keywords};

} // namespace

PyObject* run_wsgi_server(PyObject* /* self */, PyObject* const* args,
    Py_ssize_t nargs, PyObject* kwnames) {

  PyObject* appObj;
  const char* host {"localhost"};
  const char* port {"8000"};
  int reuseport {0};

  if(!_PyArg_ParseStackAndKeywords(args, nargs, kwnames, &_rs_parser, &appObj,
         &host, &port, &reuseport))
    return nullptr;

  Py_IncRef(appObj);

  asio::io_context io {1};
  asio::co_spawn(io, handle_signals(io), detached);
  asio::co_spawn(io, handle_header(io), detached);

  WSGIApp app {appObj, host, port};
  accept(io.get_executor(), host, port, reuseport, app);
  io.run();

  Py_DecRef(appObj);

  // There is no way to exit the run loop that isn't via an exception being set
  return nullptr;
}

} // namespace velocem
