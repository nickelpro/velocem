#ifndef VELOCEM_WSGI_REQUEST_HPP
#define VELOCEM_WSGI_REQUEST_HPP

#include <cstdlib>
#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include <asio.hpp>

#include "util/BalmStringView.hpp"

#include "Input.hpp"

namespace velocem {

struct WSGIHeader {

  WSGIHeader(std::function<void(BalmStringView*)> f_free, char* base = nullptr,
      size_t len = 0);
  WSGIHeader(const WSGIHeader& other);
  WSGIHeader(WSGIHeader&& other);

  BalmStringView bsv;
  std::string buf;

  bool process();
};

struct WSGIRequest {

  WSGIRequest();
  WSGIRequest(std::function<void(WSGIRequest*)> f_free);

  void reset();

  BalmStringView& url();

  bool has_query();
  BalmStringView& query();

  int process_url();

  BalmStringView& next_header(char* base = nullptr, size_t len = 0);
  BalmStringView& last_header();
  bool process_header();

  BalmStringView& next_value(char* base = nullptr, std::size_t len = 0);
  BalmStringView& last_value();

  asio::mutable_buffer get_read_buf(std::size_t offset,
      std::size_t minsize = 1024);
  asio::mutable_buffer get_parse_buf(std::size_t offset, std::size_t n);

  std::size_t ref_count_ {2};

  std::function<void(WSGIRequest*)> f_free_ {
      [this](WSGIRequest*) { delete this; }};

  WSGIInput input_ {[this](WSGIInput*) {
    if(!--ref_count_)
      f_free_(this);
  }};

  BalmStringView url_ {[this](BalmStringView*) {
    if(!--ref_count_)
      f_free_(this);
  }};

  std::optional<BalmStringView> query_;

  std::vector<WSGIHeader> headers_;
  std::vector<BalmStringView> values_;
  std::vector<char> buf_;
};

} // namespace velocem

#endif // VELOCEM_WSGI_REQUEST_HPP
