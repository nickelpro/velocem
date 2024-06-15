
#include <asio.hpp>

#include <sys/socket.h>

int set_reuse_port(asio::ip::tcp::acceptor& sock) {
  auto native {sock.native_handle()};
  int optval {1};
  return setsockopt(native, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
}
