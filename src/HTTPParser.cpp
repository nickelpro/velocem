#include "HTTPParser.hpp"

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

HTTPParser::HTTPParser() {
  llhttp_init(static_cast<llhttp_t*>(this), HTTP_REQUEST, &settings_);
}

HTTPParser::HTTPParser(Request* req) : req_ {req} {
  llhttp_init(static_cast<llhttp_t*>(this), HTTP_REQUEST, &settings_);
}

void HTTPParser::reset(Request* request) {
  req_ = request;
  done_ = false;
  keep_alive_ = false;
  settings_ = {
      .on_url = on_url_tr,
      .on_header_field = on_header_field_tr,
      .on_header_value = on_header_value_tr,
      .on_message_complete = on_message_complete_tr,
      .on_url_complete = on_url_complete_tr,
      .on_header_field_complete = on_header_field_complete_tr,
      .on_header_value_complete = on_header_value_complete_tr,
  };
}

llhttp_errno_t HTTPParser::parse(asio::mutable_buffer buffer) {
  return parse((char*) buffer.data(), buffer.size());
}

llhttp_errno_t HTTPParser::parse(char* data, size_t len) {
  auto ret {llhttp_execute(static_cast<llhttp_t*>(this), data, len)};
  if(ret != HPE_OK && ret != HPE_PAUSED)
    throw std::runtime_error {"HTTP error"};
  return llhttp_execute(static_cast<llhttp_t*>(this), data, len);
}

llhttp_errno_t HTTPParser::resume(Request* req, asio::mutable_buffer buffer) {
  return resume(req, (char*) buffer.data(), buffer.size());
}

llhttp_errno_t HTTPParser::resume(Request* req, char* data, size_t len) {
  req_ = req;
  done_ = false;
  keep_alive_ = false;
  llhttp_resume(static_cast<llhttp_t*>(this));
  return parse(data, len);
}

bool HTTPParser::done() {
  return done_;
}

bool HTTPParser::keep_alive() {
  return keep_alive_;
}

std::span<char> HTTPParser::get_rem(std::size_t offset) {
  const char* endp = llhttp_get_error_pos(static_cast<llhttp_t*>(this));
  const char* used = req_->buf_.data() + offset;
  size_t dist = used - endp;
  return {const_cast<char*>(endp), dist};
}

int HTTPParser::on_url(const char* at, size_t length) {
  char* cur {(char*) std::memchr(at, '?', length)};
  if(cur) {
    std::ptrdiff_t inc {cur - at};
    req_->url().from(const_cast<char*>(at), inc);
    ++cur;
    req_->query().from(cur, length - inc);
    settings_.on_url = on_query_next_tr;
  } else {
    req_->url().from(const_cast<char*>(at), length);
    settings_.on_url = on_url_next_tr;
  }
  return 0;
}

int HTTPParser::on_url_tr(llhttp_t* parser, const char* at, size_t length) {
  return static_cast<HTTPParser*>(parser)->on_url(at, length);
}

int HTTPParser::on_url_next(const char* at, size_t length) {
  char* cur {(char*) std::memchr(at, '?', length)};
  if(cur) {
    std::ptrdiff_t inc {cur - at};
    req_->url().extend(inc);
    ++cur;
    req_->query().from(cur, length - inc);
    settings_.on_url = on_query_next_tr;
  } else {
    req_->url().extend(length);
  }
  return 0;
}

int HTTPParser::on_url_next_tr(llhttp_t* parser, const char* at,
    size_t length) {
  return static_cast<HTTPParser*>(parser)->on_url_next(at, length);
}

int HTTPParser::on_query_next(const char*, size_t length) {
  req_->query().extend(length);
  return 0;
}

int HTTPParser::on_query_next_tr(llhttp_t* parser, const char* at,
    size_t length) {
  return static_cast<HTTPParser*>(parser)->on_query_next(at, length);
}

int HTTPParser::on_url_complete() {
  settings_.on_url = on_url_tr;
  return req_->process_url();
}

int HTTPParser::on_url_complete_tr(llhttp_t* parser) {
  return static_cast<HTTPParser*>(parser)->on_url_complete();
}

int HTTPParser::on_header_field(const char* at, size_t length) {
  req_->next_header(const_cast<char*>(at), length);
  settings_.on_header_field = on_header_field_next_tr;
  return 0;
}

int HTTPParser::on_header_field_tr(llhttp_t* parser, const char* at,
    size_t length) {
  return static_cast<HTTPParser*>(parser)->on_header_field(at, length);
}

int HTTPParser::on_header_field_next(const char*, size_t length) {
  req_->last_header().extend(length);
  return 0;
}

int HTTPParser::on_header_field_next_tr(llhttp_t* parser, const char* at,
    size_t length) {
  return static_cast<HTTPParser*>(parser)->on_header_field_next(at, length);
}

int HTTPParser::on_header_field_complete() {
  if(!req_->process_header()) {
    settings_.on_header_value = nullptr;
  }
  settings_.on_header_field = on_header_field_tr;
  return 0;
}

int HTTPParser::on_header_field_complete_tr(llhttp_t* parser) {
  return static_cast<HTTPParser*>(parser)->on_header_field_complete();
}

int HTTPParser::on_header_value(const char* at, size_t length) {
  req_->next_value(const_cast<char*>(at), length);
  settings_.on_header_value = on_header_value_next_tr;
  return 0;
}

int HTTPParser::on_header_value_tr(llhttp_t* parser, const char* at,
    size_t length) {
  return static_cast<HTTPParser*>(parser)->on_header_value(at, length);
}

int HTTPParser::on_header_value_next(const char*, size_t length) {
  req_->last_value().extend(length);
  return 0;
}

int HTTPParser::on_header_value_next_tr(llhttp_t* parser, const char* at,
    size_t length) {
  return static_cast<HTTPParser*>(parser)->on_header_value_next(at, length);
}

int HTTPParser::on_header_value_complete() {
  settings_.on_header_value = on_header_value_tr;
  return 0;
}

int HTTPParser::on_header_value_complete_tr(llhttp_t* parser) {
  return static_cast<HTTPParser*>(parser)->on_header_value_complete();
}

int HTTPParser::on_message_complete() {
  done_ = true;
  keep_alive_ = llhttp_should_keep_alive(static_cast<llhttp_t*>(this));
  return keep_alive_ ? HPE_PAUSED : 0;
}

int HTTPParser::on_message_complete_tr(llhttp_t* parser) {
  return static_cast<HTTPParser*>(parser)->on_message_complete();
}

} // namespace velocem
