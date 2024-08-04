#ifndef VELOCEM_HTTP_PARSER_HPP
#define VELOCEM_HTTP_PARSER_HPP

#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <asio.hpp>
#include <llhttp.h>

#include "Request.hpp"

namespace velocem {

struct HTTPParser : llhttp_t {

  HTTPParser();

  HTTPParser(Request* req);

  void reset(Request* request);

  llhttp_errno_t parse(asio::mutable_buffer buffer);
  llhttp_errno_t parse(char* data, size_t len);

  llhttp_errno_t resume(Request* req, asio::mutable_buffer buffer);
  llhttp_errno_t resume(Request* req, char* data, size_t len);

  bool done();

  bool keep_alive();

  std::span<char> get_rem(std::size_t offset);

private:
  int on_url(const char* at, size_t length);
  static int on_url_tr(llhttp_t* parser, const char* at, size_t length);

  int on_url_next(const char* at, size_t length);
  static int on_url_next_tr(llhttp_t* parser, const char* at, size_t length);

  int on_query_next(const char*, size_t length);
  static int on_query_next_tr(llhttp_t* parser, const char* at, size_t length);

  int on_url_complete();
  static int on_url_complete_tr(llhttp_t* parser);

  int on_header_field(const char* at, size_t length);
  static int on_header_field_tr(llhttp_t* parser, const char* at,
      size_t length);

  int on_header_field_next(const char*, size_t length);
  static int on_header_field_next_tr(llhttp_t* parser, const char* at,
      size_t length);

  int on_header_field_complete();
  static int on_header_field_complete_tr(llhttp_t* parser);

  int on_header_value(const char* at, size_t length);
  static int on_header_value_tr(llhttp_t* parser, const char* at,
      size_t length);

  int on_header_value_next(const char*, size_t length);
  static int on_header_value_next_tr(llhttp_t* parser, const char* at,
      size_t length);

  int on_header_value_complete();
  static int on_header_value_complete_tr(llhttp_t* parser);

  int on_message_complete();
  static int on_message_complete_tr(llhttp_t* parser);

  llhttp_settings_t settings_ {
      .on_url = on_url_tr,
      .on_header_field = on_header_field_tr,
      .on_header_value = on_header_value_tr,
      .on_message_complete = on_message_complete_tr,
      .on_url_complete = on_url_complete_tr,
      .on_header_field_complete = on_header_field_complete_tr,
      .on_header_value_complete = on_header_value_complete_tr,
  };

  bool done_ {false};
  bool keep_alive_ {false};
  Request* req_;
};

} // namespace velocem

#endif // VELOCEM_HTTP_PARSER_HPP
