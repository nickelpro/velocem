#include <stdexcept>

#include <asio.hpp>

int set_reuse_port(asio::ip::tcp::acceptor& sock) {
  throw std::logic_error {"SO_REUSEPORT unavailable on MacOS"};
}
