#ifndef VELOCEM_REQUEST_H
#define VELOCEM_REQUEST_H

#include <cstdlib>
#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include <asio.hpp>

#include "Balm.hpp"

namespace velocem {

struct Header {

  Header(std::function<void(BalmStringView*)> f_free, char* base = nullptr,
      size_t len = 0);
  Header(const Header& other);
  Header(Header&& other);

  BalmStringView bsv;
  std::string buf;

  bool process();
};

struct Request {

  Request();
  Request(std::function<void(BalmStringView*)> f_free);

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

  std::size_t ref_count_ {1};

  std::function<void(BalmStringView*)> f_free_ {[this](BalmStringView*) {
    if(!--ref_count_)
      delete this;
  }};

  BalmStringView url_ {f_free_};
  std::optional<BalmStringView> query_;

  std::vector<Header> headers_;
  std::vector<BalmStringView> values_;
  std::vector<char> buf_;
};

struct QueuedRequest : Request {
  QueuedRequest(std::queue<QueuedRequest*>& q)
      : Request {[this, &q](BalmStringView*) {
          if(!--ref_count_) {
            reset();
            q.push(this);
          }
        }} {};
};

} // namespace velocem

#endif // VELOCEM_REQUEST_H
