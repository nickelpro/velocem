#include <llhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// View into buffer
typedef struct {
  size_t len;
  const char* base;
} StrView;

// Data structure to store llhttp parser and URL buffer
typedef struct {
  llhttp_t parser;
  llhttp_settings_t settings;
  StrView url;
  StrView query;

  size_t len;
  size_t used;
  size_t parse_idx;
  char buf[];
} Request;

// Testing utility
void recv(Request* req, char* data, size_t len) {
  memcpy(req->buf + req->used, data, len);
  req->used += len;
}

// Testing macro
#define STRSZ(str) (sizeof(str) - 1)


#define GET_REQUEST_FROM_FIELD(pointer, field)                                 \
  ((Request*) (((char*) pointer) - offsetof(Request, field)))
#define GET_PARSER_REQUEST(pointer) GET_REQUEST_FROM_FIELD(pointer, parser)

static int on_query_next(llhttp_t* parser, const char*, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);
  req->query.len += length;
  return 0;
}

static int on_url_next(llhttp_t* parser, const char* at, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);

  char* query = memchr(at, '?', length);
  if(query) {
    req->url.len += query - at;
    req->query.base = ++query;
    req->query.len = length - (query - at);
    req->settings.on_url = on_query_next;
  } else {
    req->url.len += length;
  }
  return 0;
}

static int on_url(llhttp_t* parser, const char* at, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);

  req->url.base = at;

  char* query = memchr(at, '?', length);
  if(query) {
    req->url.len = (query - at);
    req->query.base = ++query;
    req->query.len = length - (query - at);
    req->settings.on_url = on_query_next;
  } else {
    req->url.len = length;
    req->settings.on_url = on_url_next;
  }
  return 0;
}


// Callback function for llhttp to handle completion of URL
int on_url_complete(llhttp_t* parser) {
  Request* req = GET_PARSER_REQUEST(parser);

  // Process or store the completed URL as needed
  printf("URL: ");
  fwrite(req->url.base, 1, req->url.len, stdout);
  printf("\n");

  printf("Query: ");
  fwrite(req->query.base, 1, req->query.len, stdout);
  printf("\n");

  return 0;
}

static llhttp_settings_t init_settings = {
    .on_url = on_url,
    .on_url_complete = on_url_complete,
};

Request* alloc_request(size_t buf_size) {
  Request* req = malloc(sizeof(*req) + buf_size);

  req->settings = init_settings;
  llhttp_init(&req->parser, HTTP_REQUEST, &req->settings);

  req->parse_idx = 0;
  req->used = 0;
  req->len = buf_size;
}

llhttp_errno_t exec_parse(Request* req) {
  llhttp_errno_t err = llhttp_execute(&req->parser, req->buf + req->parse_idx,
      req->used - req->parse_idx);
  req->parse_idx = req->used;
  return err;
}

static char REQUEST_PART1[] = "GET /path/to";
static char REQUEST_PART2[] = "/resource?test=5&";
static char REQUEST_PART3[] = "lol=6 HTTP/1.1\r\nHost: example.com\r\n\r\n";

int main() {
  Request* req = alloc_request(1024);
  // Initially only have part of the request
  recv(req, REQUEST_PART1, STRSZ(REQUEST_PART1));

  llhttp_errno_t err = exec_parse(req);

  if(err != HPE_OK) {
    fprintf(stderr,
        "Error parsing partial HTTP request. llhttp error code: %s\n",
        llhttp_errno_name(err));
    return 1;
  }

  // More data arrives, for example, "resource HTTP/1.1\r\nHost:
  // example.com\r\n\r\n"
  recv(req, REQUEST_PART2, STRSZ(REQUEST_PART2));

  err = exec_parse(req);

  if(err != HPE_OK) {
    fprintf(stderr,
        "Error parsing remaining HTTP request. llhttp error code: %s\n",
        llhttp_errno_name(err));
    return 1;
  }

  recv(req, REQUEST_PART3, STRSZ(REQUEST_PART3));
  err = exec_parse(req);

  if(err != HPE_OK) {
    fprintf(stderr,
        "Error parsing remaining HTTP request. llhttp error code: %s\n",
        llhttp_errno_name(err));
    return 1;
  }

  return 0;
}
