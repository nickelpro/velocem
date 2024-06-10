#ifndef VELOCEM_REQUEST_H
#define VELOCEM_REQUEST_H

#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <asio.hpp>

#include "Balm.hpp"

namespace velocem {

struct Header {

  Header(std::function<void(BalmStringView*)> f_free, char* base = nullptr,
      size_t len = 0)
      : bsv {f_free, base, len} {}

  Header(Header&& other)
      : bsv {std::move(other.bsv)}, buf {std::move(other.buf)} {
    bsv.from(buf.data(), buf.size());
  }

  Header(const Header& other) : bsv {other.bsv}, buf {other.buf} {
    bsv.from(buf.data(), buf.size());
  }

  BalmStringView bsv;
  std::string buf;

  bool process() {
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
};

struct Request {

  Request() {
    headers.reserve(32);
    values.reserve(32);
  }


  std::size_t ref_count {1};


  std::function<void(BalmStringView*)> f_free {[this](BalmStringView*) {
    if(!--ref_count)
      delete this;
  }};

  BalmStringView& url() {
    return url_;
  }

  bool has_query() {
    return query_.has_value();
  }

  BalmStringView& query() {
    if(query_)
      return *query_;
    ref_count++;
    return query_.emplace(f_free);
  }


  void process_url() {
    size_t len {unquote_url_inplace(url_._base.utf8, url_._base.utf8_length)};
    url_.resize(len);
    if(query_) {
      len = unquote_url_inplace(query_->_base.utf8, query_->_base.utf8_length);
      query_->resize(len);
    }
  }

  BalmStringView& next_header(char* base = nullptr, size_t len = 0) {
    ++ref_count;
    return headers.emplace_back(f_free, base, len).bsv;
  }

  BalmStringView& last_header() {
    return headers.back().bsv;
  }

  bool process_header() {
    if(!headers.back().process()) {
      headers.pop_back();
      --ref_count;
      return false;
    }
    return true;
  }

  BalmStringView& next_value(char* base = nullptr, std::size_t len = 0) {
    ++ref_count;
    return values.emplace_back(f_free, base, len);
  }

  BalmStringView& last_value() {
    return values.back();
  }

  auto get_read_buf(std::size_t offset, std::size_t minsize = 1024) {
    std::size_t diff {buf.size() - offset};
    if(diff < minsize)
      buf.resize(buf.size() + 2 * minsize);

    return asio::buffer(buf.data() + offset, buf.size() - offset);
  }

  auto get_parse_buf(std::size_t offset, std::size_t n) {
    return asio::buffer(buf.data() + offset, n);
  }

  BalmStringView url_ {f_free};
  std::optional<BalmStringView> query_;

  std::vector<Header> headers;
  std::vector<BalmStringView> values;
  std::vector<char> buf;


// Directly from bjoern
#define NOHEX ((char) -1)
#define UNHEX(c)                                                               \
  ((c >= '0' && c <= '9')          ? (c - '0')                                 \
          : (c >= 'a' && c <= 'f') ? (c - 'a' + 10)                            \
          : (c >= 'A' && c <= 'F') ? (c - 'A' + 10)                            \
                                   : NOHEX)

  static size_t unquote_url_inplace(char* url, size_t len) {
    for(char *p = url, *end = url + len; url != end; ++url, ++p) {
      if(*url == '%') {
        if(url >= end - 2) {
          /* Less than two characters left after the '%' */
          return 0;
        }
        char a = UNHEX(url[1]);
        char b = UNHEX(url[2]);
        if(a == NOHEX || b == NOHEX)
          return 0;
        *p = a * 16 + b;
        url += 2;
        len -= 2;
      } else {
        *p = *url;
      }
    }
    return len;
  }
#undef NOHEX
#undef UNHEX
};

} // namespace velocem

#endif // VELOCEM_REQUEST_H
