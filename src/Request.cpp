#include "Request.hpp"

#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

#include <asio.hpp>

#include "Balm.hpp"

namespace {
std::size_t unquote_url_inplace(char* url, size_t len) {

  // clang-format off
  static const char tbl[256] {
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        0, 1, 2, 3, 4, 5, 6, 7,  8, 9,-1,-1,-1,-1,-1,-1,
      -1,10,11,12,13,14,15,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,10,11,12,13,14,15,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
  };
  // clang-format on

  char* end = url + len;
  char* max_percent = end - 2;
  char* in = url;

  while(*in != '%' && in < end)
    ++in;

  char* out = in;

  for(; in < end; ++in, ++out) {
    if(*in == '%') {

      if(in >= max_percent) [[unlikely]]
        return std::numeric_limits<std::size_t>::max();

      char a = tbl[static_cast<unsigned char>(url[1])];
      char b = tbl[static_cast<unsigned char>(url[2])];

      if(a == -1 || b == -1) [[unlikely]]
        return std::numeric_limits<std::size_t>::max();

      *out = (a << 4) | b;
      in += 2;
    } else {
      *out = *in;
    }
  }
  return out - url;
}
} // namespace

namespace velocem {

Header::Header(std::function<void(BalmStringView*)> f_free, char* base,
    size_t len)
    : bsv {f_free, base, len} {}

Header::Header(const Header& other) : bsv {other.bsv}, buf {other.buf} {
  bsv.from(buf.data(), buf.size());
}

Header::Header(Header&& other)
    : bsv {std::move(other.bsv)}, buf {std::move(other.buf)} {
  bsv.from(buf.data(), buf.size());
}

bool Header::process() {
  buf.resize(bsv._base.utf8_length + 5);
  char* target = buf.data() + 5;
  for(Py_ssize_t i = 0; i < bsv._base.utf8_length; ++i) {
    // CVE-2015-0219
    // https://www.djangoproject.com/weblog/2015/jan/13/security/
    if(bsv._base.utf8[i] == '_') {
      return false;
    } else if(bsv._base.utf8[i] == '-') {
      target[i] = '_';
    } else {
      target[i] = bsv._base.utf8[i] & 0xDF;
    }
  }
  std::memcpy(buf.data(), "HTTP_", 5);
  bsv.from(buf.data(), buf.size());
  return true;
}

Request::Request() {
  headers_.reserve(32);
  values_.reserve(32);
}

Request::Request(std::function<void(BalmStringView*)> f_free)
    : f_free_ {f_free} {
  headers_.reserve(32);
  values_.reserve(32);
}

void Request::reset() {
  ref_count_ = 1;
  query_.reset();
  headers_.clear();
  values_.clear();
  buf_.clear();
}

BalmStringView& Request::url() {
  return url_;
}

bool Request::has_query() {
  return query_.has_value();
}

BalmStringView& Request::query() {
  if(query_)
    return *query_;
  ref_count_++;
  return query_.emplace(f_free_);
}


int Request::process_url() {
  std::size_t len {
      unquote_url_inplace(url_._base.utf8, url_._base.utf8_length)};

  if(len == std::numeric_limits<std::size_t>::max()) [[unlikely]]
    return -1;

  url_.resize(len);
  if(query_) {
    len = unquote_url_inplace(query_->_base.utf8, query_->_base.utf8_length);

    if(len == std::numeric_limits<std::size_t>::max()) [[unlikely]]
      return -1;

    query_->resize(len);
  }

  return 0;
}

BalmStringView& Request::next_header(char* base, size_t len) {
  ++ref_count_;
  return headers_.emplace_back(f_free_, base, len).bsv;
}

BalmStringView& Request::last_header() {
  return headers_.back().bsv;
}

bool Request::process_header() {
  if(!headers_.back().process()) {
    headers_.pop_back();
    --ref_count_;
    return false;
  }
  return true;
}

BalmStringView& Request::next_value(char* base, std::size_t len) {
  ++ref_count_;
  return values_.emplace_back(f_free_, base, len);
}

BalmStringView& Request::last_value() {
  return values_.back();
}

asio::mutable_buffer Request::get_read_buf(std::size_t offset,
    std::size_t minsize) {
  std::size_t diff {buf_.size() - offset};
  if(diff < minsize)
    buf_.resize(buf_.size() + 2 * minsize);

  return asio::buffer(buf_.data() + offset, buf_.size() - offset);
}

asio::mutable_buffer Request::get_parse_buf(std::size_t offset, std::size_t n) {
  return asio::buffer(buf_.data() + offset, n);
}

} // namespace velocem
