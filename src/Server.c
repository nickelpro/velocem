#include <stddef.h>
#include <stdlib.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <llhttp.h>
#include <uv.h>

#define DEFAULT_BUF_SIZE (1 << 16)
#define DEFAULT_HEADER_COUNT (1 << 5)

static PyObject* _GLOBAL_ENVIRON;

#define COMMON_STRINGS(X)                                                      \
  X(CONTENT_LENGTH)                                                            \
  X(HTTP_CONTENT_LENGTH)                                                       \
  X(CONTENT_TYPE)                                                              \
  X(HTTP_CONTENT_TYPE)                                                         \
  X(GET)                                                                       \
  X(POST)                                                                      \
  X(PUT)                                                                       \
  X(DELETE)                                                                    \
  X(REQUEST_METHOD)                                                            \
  X(PATH_INFO)                                                                 \
  X(QUERY_STRING)                                                              \
  X(SERVER_NAME)                                                               \
  X(SERVER_PORT)                                                               \
  X(SERVER_PROTOCOL)

#define X(str) static PyObject* _##str;
COMMON_STRINGS(X)
#undef X

static PyObject* _HTTP_1_1;
static PyObject* _HTTP_1_0;
static PyObject* _EMPTY_STRING;


typedef struct {
  size_t len;
  const char* base;
} StrView;

#define VIEW2PY(v) PyUnicode_FromStringAndSize(v.base, v.len)
#define PyDict_SetView(dict, key, view)                                        \
  do {                                                                         \
    PyObject* v = VIEW2PY(view);                                               \
    PyDict_SetItem(dict, key, v);                                              \
    Py_DECREF(v);                                                              \
  } while(0)

typedef struct {
  StrView field;
  StrView value;
} HTTPHeader;

typedef struct {
  uv_tcp_t server;
  PyObject* app;
  PyObject* base_environ;
} Server;

typedef struct {
  uv_tcp_t client;
  PyObject* app;
  PyObject* base_environ;

  llhttp_t parser;
  llhttp_settings_t settings;

  StrView method;
  StrView url;
  StrView query;

  size_t headers_size;
  size_t headers_used;
  HTTPHeader* headers;

  size_t len;
  size_t used;
  size_t parse_idx;
  char buf[];
} Request;

#define GET_REQUEST_FROM_FIELD(pointer, field)                                 \
  ((Request*) (((char*) pointer) - offsetof(Request, field)))
#define GET_PARSER_REQUEST(pointer) GET_REQUEST_FROM_FIELD(pointer, parser)

static PyObject* create_header_string(StrView field) {
  PyObject* str = PyUnicode_New(field.len + 5, 127);
  Py_UCS1* buf = PyUnicode_1BYTE_DATA(str);
  memcpy(buf, "HTTP_", 5);
  buf += 5;
  for(size_t i = 0; i < field.len; ++i) {
    // CVE-2015-0219
    // https://www.djangoproject.com/weblog/2015/jan/13/security/
    if(field.base[i] == '_') {
      Py_DecRef(str);
      return NULL;
    } else if(field.base[i] == '-') {
      buf[i] = '_';
    } else {
      buf[i] = field.base[i] & 0xDF;
    }
  }
  return str;
}

static PyObject* push_headers(PyObject* dict, HTTPHeader* headers, size_t len) {
  for(HTTPHeader* hdr = headers; hdr < headers + len; ++hdr) {
    PyObject* k = create_header_string(hdr->field);
    if(!k)
      continue;
    PyDict_SetView(dict, k, hdr->value);
    Py_DECREF(k);
  }
}

static void replace_key(PyObject* dict, PyObject* old, PyObject* new) {
  PyObject* value = PyDict_GetItem(dict, old);
  if(value) {
    Py_INCREF(value);
    PyDict_DelItem(dict, old);
    PyDict_SetItem(dict, new, value);
    Py_DECREF(value);
  }
}

// Directly from bjoern
#define UNHEX(c)                                                               \
  ((c >= '0' && c <= '9')          ? (c - '0')                                 \
          : (c >= 'a' && c <= 'f') ? (c - 'a' + 10)                            \
          : (c >= 'A' && c <= 'F') ? (c - 'A' + 10)                            \
                                   : NOHEX)
#define NOHEX ((char) -1)

size_t unquote_url_inplace(char* url, size_t len) {
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

static PyObject* build_environ(Request* req) {
  PyObject* environ = PyDict_Copy(req->base_environ);

  // If Bjoern's single cute micro-optimization is good, more is better
  switch(req->parser.method) { // clang-format off
    case HTTP_GET:    PyDict_SetItem(environ, _REQUEST_METHOD, _GET);    break;
    case HTTP_POST:   PyDict_SetItem(environ, _REQUEST_METHOD, _POST);   break;
    case HTTP_PUT:    PyDict_SetItem(environ, _REQUEST_METHOD, _PUT);    break;
    case HTTP_DELETE: PyDict_SetItem(environ, _REQUEST_METHOD, _DELETE); break;
    default:          PyDict_SetView(environ, _REQUEST_METHOD, req->method);
  } // clang-format on

  // Casting away const here so some explanation is in order:
  // This is the buffer from alloc_request, it's perfectly fine to modify in
  // place. The pointers are const because we grabbed them during parsing,
  // llhttp very reasonably treats the buffers as const, and generally it's a
  // good idea to treat these StrViews as const.
  //
  // But we're going to be deallocating the whole request in a little bit, so
  // let's just do the modifications we need in place.
  req->url.len = unquote_url_inplace((char*) req->url.base, req->url.len);
  req->query.len = unquote_url_inplace((char*) req->query.base, req->query.len);
  PyDict_SetView(environ, _PATH_INFO, req->url);
  PyDict_SetView(environ, _QUERY_STRING, req->query);
  PyDict_SetItem(environ, _SERVER_PROTOCOL,
      req->parser.http_minor ? _HTTP_1_1 : _HTTP_1_0);

  push_headers(environ, req->headers, req->headers_used);
  replace_key(environ, _HTTP_CONTENT_LENGTH, _CONTENT_LENGTH);
  replace_key(environ, _HTTP_CONTENT_TYPE, _CONTENT_TYPE);

  return environ;
}

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

static int on_method_next(llhttp_t* parser, const char*, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);
  req->method.len += length;
  return 0;
}

static int on_method(llhttp_t* parser, const char* at, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);
  req->method.base = at;
  req->method.len = length;
  req->settings.on_method = on_method_next;
  return 0;
}

static int on_header_field_next(llhttp_t* parser, const char*, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);
  HTTPHeader* header = &req->headers[req->headers_used];
  header->field.len += length;
  return 0;
}

static int on_header_field(llhttp_t* parser, const char* at, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);

  if(req->headers_used == req->headers_size) {
    req->headers_size <<= 1;
    size_t alloc = req->headers_size * sizeof(*req->headers);
    req->headers = realloc(req->headers, alloc);
  }

  HTTPHeader* header = &req->headers[req->headers_used];
  header->field.base = at;
  header->field.len = length;

  req->settings.on_header_field = on_header_field_next;
  return 0;
}

static int on_header_value_next(llhttp_t* parser, const char*, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);
  HTTPHeader* header = &req->headers[req->headers_used];
  header->value.len += length;
  return 0;
}

static int on_header_value(llhttp_t* parser, const char* at, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);
  HTTPHeader* header = &req->headers[req->headers_used];
  header->value.base = at;
  header->value.len = length;
  req->settings.on_header_value = on_header_value_next;
  return 0;
}

static int on_header_value_complete(llhttp_t* parser) {
  Request* req = GET_PARSER_REQUEST(parser);
  ++req->headers_used;
  req->settings.on_header_field = on_header_field;
  req->settings.on_header_value = on_header_value;
  return 0;
}

static void free_request(Request* req) {
  free(req->headers);
  free(req);
}

static void on_close(uv_handle_t* handle) {
  free_request((Request*) handle);
}

static int on_message_complete(llhttp_t* parser) {
  Request* req = GET_PARSER_REQUEST(parser);

  PyGILState_STATE state = PyGILState_Ensure();
  PyObject* environ = build_environ(req);
  PyObject* retval =
      PyObject_CallFunctionObjArgs(req->app, environ, Py_None, NULL);
  Py_DECREF(environ);
  PyGILState_Release(state);

  uv_close((uv_handle_t*) req, on_close);

  return 0;
}

static llhttp_settings_t init_settings = {
    .on_method = on_method,
    .on_url = on_url,
    .on_header_field = on_header_field,
    .on_header_value = on_header_value,
    .on_header_value_complete = on_header_value_complete,
    .on_message_complete = on_message_complete,
};

static Request* alloc_request(Server* srv, size_t buf_size, size_t hdr_count) {
  Request* req = malloc(sizeof(*req) + buf_size);

  req->app = srv->app;
  req->base_environ = srv->base_environ;

  req->settings = init_settings;
  llhttp_init(&req->parser, HTTP_REQUEST, &req->settings);

  req->headers = malloc(sizeof(*req->headers) * hdr_count);
  req->headers_size = hdr_count;
  req->headers_used = 0;

  req->method.len = 0;
  req->url.len = 0;
  req->query.len = 0;

  req->parse_idx = 0;
  req->used = 0;
  req->len = buf_size;
}

static llhttp_errno_t exec_parse(Request* req) {
  llhttp_errno_t err = llhttp_execute(&req->parser, req->buf + req->parse_idx,
      req->used - req->parse_idx);
  req->parse_idx = req->used;
  return err;
}

static void alloc_buffer(uv_handle_t* handle, size_t hint, uv_buf_t* buf) {
  Request* req = (Request*) handle;
  buf->base = req->buf + req->used;
  buf->len = req->len - req->used;
}

static void on_read(uv_stream_t* client, ssize_t nRead, const uv_buf_t* buf) {
  if(nRead < 0) {
    uv_close((uv_handle_t*) client, on_close);
    return;
  }

  Request* req = (Request*) client;

  req->used += nRead;
  llhttp_errno_t err = exec_parse(req);
  if(err != HPE_OK)
    uv_close((uv_handle_t*) client, on_close);
}

static void on_connect(uv_stream_t* handle, int status) {
  if(status < 0)
    return;

  Request* req =
      alloc_request((Server*) handle, DEFAULT_BUF_SIZE, DEFAULT_HEADER_COUNT);

  uv_tcp_init(uv_default_loop(), (uv_tcp_t*) req);
  if(!uv_accept(handle, (uv_stream_t*) req))
    uv_read_start((uv_stream_t*) req, alloc_buffer, on_read);
  else
    uv_close((uv_handle_t*) req, on_close);
}

void init() {
#define X(str) _##str = PyUnicode_FromString(#str);
  COMMON_STRINGS(X)
#undef X

  _HTTP_1_0 = PyUnicode_FromString("HTTP/1.0");
  _HTTP_1_1 = PyUnicode_FromString("HTTP/1.1");
  _EMPTY_STRING = PyUnicode_New(0, 0);

  _GLOBAL_ENVIRON = PyDict_New();
  PyDict_SetItemString(_GLOBAL_ENVIRON, "SCRIPT_NAME", _EMPTY_STRING);
  PyDict_SetItemString(_GLOBAL_ENVIRON, "wsgi.input_terminated", Py_True);

  PyObject* version = PyTuple_Pack(2, PyLong_FromLong(1), PyLong_FromLong(0));
  PyDict_SetItemString(_GLOBAL_ENVIRON, "wsgi.version", version);
  Py_DECREF(version);

  PyObject* http = PyUnicode_FromString("http");
  PyDict_SetItemString(_GLOBAL_ENVIRON, "wsgi.url_scheme", http);
  Py_DECREF(http);

  PyDict_SetItemString(_GLOBAL_ENVIRON, "wsgi.errors",
      PySys_GetObject("stderr"));
  PyDict_SetItemString(_GLOBAL_ENVIRON, "wsgi.multithread", Py_False);
  PyDict_SetItemString(_GLOBAL_ENVIRON, "wsgi.multiprocess", Py_True);
  PyDict_SetItemString(_GLOBAL_ENVIRON, "wsgi.run_once", Py_False);
}

static void handle_sigint(uv_signal_t* handle, int signum) {
  PyErr_SetInterrupt();
  PyGILState_STATE state = PyGILState_Ensure();
  if(PyErr_CheckSignals() == -1) {
    PyErr_Print();
    exit(EXIT_FAILURE);
  }
  PyGILState_Release(state);
}

int run_server(char* host, PyObject* app, unsigned port, unsigned backlog) {

  uv_signal_t t;
  uv_signal_init(uv_default_loop(), &t);
  uv_signal_start(&t, handle_sigint, 2);

  Server server = {
      .app = app,
      .base_environ = PyDict_Copy(_GLOBAL_ENVIRON),
  };

  PyObject* h = PyUnicode_FromString(host);
  PyObject* p = PyUnicode_FromFormat("%u", port);

  PyDict_SetItem(server.base_environ, _SERVER_NAME, h);
  PyDict_SetItem(server.base_environ, _SERVER_PORT, p);

  Py_DECREF(h);
  Py_DECREF(p);

  struct sockaddr_in host_addr;

  int ret = uv_tcp_init_ex(uv_default_loop(), (uv_tcp_t*) &server, AF_INET);
  if(ret)
    return ret;

  struct sockaddr* ip_addr;
  uv_getaddrinfo_t info = {};
  ret = uv_ip4_addr(host, port, &host_addr);
  if(ret) {
    ret = uv_getaddrinfo(uv_default_loop(), &info, NULL, host, NULL, NULL);
    if(ret)
      return ret;
    ip_addr = (struct sockaddr*) info.addrinfo;
  } else {
    ip_addr = (struct sockaddr*) &host_addr;
  }

  ret = uv_tcp_bind((uv_tcp_t*) &server, ip_addr, 0);
  uv_freeaddrinfo(info.addrinfo);
  if(ret)
    return ret;

  if(uv_listen((uv_stream_t*) &server, backlog, on_connect))
    return EXIT_FAILURE;

  PyThreadState* thread = PyEval_SaveThread();
  ret = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  PyEval_RestoreThread(thread);

  Py_DECREF(server.base_environ);

  uv_signal_stop(&t);
  return ret;
}
