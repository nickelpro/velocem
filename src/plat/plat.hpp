#ifndef VELOCEM_PLAT_HPP
#define VELOCEM_PLAT_HPP

#include <asio.hpp>

int set_reuse_port(asio::ip::tcp::acceptor& sock);

#endif
