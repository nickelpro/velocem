#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <llhttp.h>
#include <uv.h>

// Until EDG learns what a bool is
#include "Intellisense.h"

#include "constants.h"
#include "GILBalm/Balm.h"
#include "util.h"

// Get the actual fuck out of here with this shit MSVC
#ifdef environ
#undef environ
#endif

#define DEFAULT_BUF_SIZE (1 << 16)
#define DEFAULT_HEADER_COUNT (1 << 5)
#define DEFAULT_SEND_BUFFERS (DEFAULT_HEADER_COUNT * 4 + (1 << 3))

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

static const char _cHTTP_1_1[] = "HTTP/1.1 ";
static const char _cHTTP_rn[] = "\r\n";
static const char _cHTTP_rnrn[] = "\r\n\r\n";
static const char _cHTTP_colon[] = ": ";
static const char _cHTTP_500[] = "HTTP/1.1 500 Internal Server Error\r\n\r\n";

#define BUFFER_STR_SIZE(buf, str, size)                                        \
  do {                                                                         \
    uv_buf_t* b = (buf);                                                       \
    b->base = (char*) (str);                                                   \
    b->len = (size);                                                           \
  } while(0)
#define BUFFER_STR(buf, str) BUFFER_STR_SIZE(buf, str, STRSZ(str))
#define BUFFER_PYSTR(buf, pyobj)                                               \
  BUFFER_STR_SIZE(buf, PyUnicode_DATA(pyobj), PyUnicode_GET_LENGTH(pyobj))
#define BUFFER_PYBYTES(buf, pyobj)                                             \
  BUFFER_STR_SIZE(buf, PyBytes_AsString(pyobj), PyBytes_GET_SIZE(pyobj))

typedef struct {
  uv_tcp_t server;
  PyObject* app;
  PyObject* base_environ;
} Server;

typedef struct {
  uv_tcp_t client;

  uv_write_t write;
  enum {
    WRITE_NONE,
    WRITE_LIST,
    WRITE_ITER,
  } write_status;

  uv_work_t thread;
  struct {
    bool borked : 1;
    bool keepalive : 1;
    bool received : 1;
    bool processing : 1;
    bool sending : 1;
  } state;

  PyObject* app;
  PyObject* base_environ;

  llhttp_t parser;
  llhttp_settings_t settings;

  BalmStringView* url;
  BalmStringView* query;

  BalmStringNode* recv_headers;
  BalmStringViewNode* recv_values;

  BalmStringNode* proc_headers;
  BalmStringNode* proc_values;

  struct {
    char conlen[43];
    PyObject* status;
    PyObject* headers;
  } resp;

  struct {
    size_t len;
    size_t used;
    uv_buf_t* bufs;
  } send;

  size_t len;
  size_t used;
  size_t parse_idx;
  char buf[];
} Request;

#define GET_REQUEST_FROM_FIELD(pointer, field)                                 \
  ((Request*) (((char*) pointer) - offsetof(Request, field)))
#define GET_PARSER_REQUEST(pointer) GET_REQUEST_FROM_FIELD(pointer, parser)
#define GET_THREAD_REQUEST(pointer) GET_REQUEST_FROM_FIELD(pointer, thread)
#define GET_WRITE_REQUEST(pointer) GET_REQUEST_FROM_FIELD(pointer, write)

static int handle_exc_info(PyObject* exc_info, _Bool resp_sent) {
  if(exc_info && exc_info != Py_None) {
    if(!PyTuple_Check(exc_info) || PyTuple_GET_SIZE(exc_info) != 3) {
      PyErr_Format(PyExc_TypeError,
          "start_response argument 3 must be a 3-tuple (got '%.200s' object "
          "instead)",
          Py_TYPE(exc_info)->tp_name);
      return -1;
    }

    PyErr_Restore(PyTuple_GET_ITEM(exc_info, 0), PyTuple_GET_ITEM(exc_info, 1),
        PyTuple_GET_ITEM(exc_info, 2));

    if(resp_sent)
      return -1;

    PyErr_Print();
  } else if(resp_sent) {
    PyErr_SetString(PyExc_RuntimeError,
        "'start_response' called more than once without passing 'exc_info' "
        "after the first call");
    return -1;
  }

  return 0;
}

static PyObject* start_response(PyObject* self, PyObject* const* args,
    Py_ssize_t nargs, PyObject* kwnames) {

  PyObject* exc_info = NULL;
  static const char* _keywords[] = {"", "", "exc_info", NULL};
  static _PyArg_Parser _parser = {
      .keywords = _keywords,
      .format = "OO|O:start_response",
  };

  // If you hit this you're a degenerate, don't hang on to start_response
  // object references
  if(!self) {
    PyObject* temp;
    if(_PyArg_ParseStackAndKeywords(args, nargs, kwnames, &_parser, &temp,
           &temp, &exc_info))
      handle_exc_info(exc_info, 1);
    return NULL;
  }

  // We made it passed the mounties, grab the goods
  Request* req = ((Request*) self);

  if(req->resp.status) {
    Py_DECREF(req->resp.status);
    Py_DECREF(req->resp.headers);
  }

  if(!_PyArg_ParseStackAndKeywords(args, nargs, kwnames, &_parser,
         &req->resp.status, &req->resp.headers, &exc_info))
    return NULL;

  if(handle_exc_info(exc_info, req->state.sending))
    return NULL;

  Py_INCREF(req->resp.status);
  Py_INCREF(req->resp.headers);

  Py_RETURN_NONE;
}

static PyMethodDef _START_RESPONSE_DEF = {
    "start_response",
    (PyCFunction) start_response,
    METH_FASTCALL | METH_KEYWORDS,
};


static PyObject* normalize_header(BalmString* header) {
  size_t old_len = GetLen_BalmString(header);

  size_t new_len = old_len + 5;
  char* new_str = malloc(new_len + 1);

  char* old_str = SwapBuffer_BalmString(header, new_str, new_len, NULL);

  char* target = new_str + 5;
  for(size_t i = 0; i < old_len; ++i) {
    // CVE-2015-0219
    // https://www.djangoproject.com/weblog/2015/jan/13/security/
    if(old_str[i] == '_') {
      return NULL;
    } else if(old_str[i] == '-') {
      target[i] = '_';
    } else {
      target[i] = old_str[i] & 0xDF;
    }
  }

  memcpy(new_str, "HTTP_", 5);
  new_str[new_len] = 0;
  return (PyObject*) header;
}

static void push_headers(PyObject* dict, Request* req) {
  BalmStringNode* header = req->proc_headers;
  BalmStringViewNode* value = req->proc_values;
  while(header && value) {
    PyObject* norm = normalize_header(&header->str);
    if(norm) {
      PyDict_SetItem(dict, norm, (PyObject*) value);
      header = header->next;
      value = value->next;
    } else {
      header = header->next;
      value = value->next;
      _Py_Dealloc((PyObject*) header);
      _Py_Dealloc((PyObject*) value);
    }
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

  PyDict_SetItem(environ, _REQUEST_METHOD,
      (PyObject*) &HTTP_METHS[req->parser.method]);

  char* url = GetData_BalmStringView(req->url);
  size_t url_len = GetLen_BalmStringView(req->url);
  url_len = unquote_url_inplace(url, url_len);
  UpdateLen_BalmString(req->url, url_len);

  if(req->query) {
    char* query = GetData_BalmStringView(req->query);
    size_t query_len = GetLen_BalmStringView(req->query);
    query_len = unquote_url_inplace(query, query_len);
    UpdateLen_BalmString(req->query, query_len);
    PyDict_SetItem(environ, _QUERY_STRING, (PyObject*) req->query);
  } else {
    PyDict_SetItem(environ, _QUERY_STRING, _EMPTY_STRING);
  }

  PyDict_SetItem(environ, _PATH_INFO, (PyObject*) req->url);
  PyDict_SetItem(environ, _SERVER_PROTOCOL,
      req->parser.http_minor ? _HTTP_1_1 : _HTTP_1_0);

  push_headers(environ, req);
  replace_key(environ, _HTTP_CONTENT_LENGTH, _CONTENT_LENGTH);
  replace_key(environ, _HTTP_CONTENT_TYPE, _CONTENT_TYPE);

  return environ;
}

static int on_query_next(llhttp_t* parser, const char*, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);
  req->query->_base.utf8_length += length;
  req->query->_base._base.length += length;
  return 0;
}

static int on_url_next(llhttp_t* parser, const char* at, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);
  char* query = memchr(at, '?', length);
  if(query) {
    size_t increment = query - at;
    req->url->_base.utf8_length += increment;
    req->url->_base._base.length += increment;
    ++query;
    req->query = New_BalmStringView(query, length - (query - at));
    req->settings.on_url = on_query_next;
  } else {
    req->url->_base.utf8_length += length;
    req->url->_base._base.length += length;
  }
  return 0;
}

static int on_url(llhttp_t* parser, const char* at, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);
  char* query = memchr(at, '?', length);
  if(query) {
    req->url = New_BalmStringView((char*) at, query - at);
    ++query;
    req->query = New_BalmStringView(query, length - (query - at));
    req->settings.on_url = on_query_next;
  } else {
    req->url = New_BalmStringView((char*) at, length);
    req->settings.on_url = on_url_next;
  }
  return 0;
}

static int on_url_complete(llhttp_t* parser) {
  GET_PARSER_REQUEST(parser)->settings.on_url = on_url;
  return 0;
}

static int on_header_field_next(llhttp_t* parser, const char*, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);
  req->recv_headers->str._base.utf8_length += length;
  req->recv_headers->str._base._base.length += length;
  return 0;
}

static int on_header_field(llhttp_t* parser, const char* at, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);

  BalmStringNode* prev = req->recv_headers;
  req->recv_headers = (BalmStringNode*) New_BalmString((char*) at, length);
  req->recv_headers->next = prev;
  req->settings.on_header_field = on_header_field_next;
  return 0;
}

static int on_header_value_next(llhttp_t* parser, const char*, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);
  req->recv_values->str._base.utf8_length += length;
  req->recv_values->str._base._base.length += length;
  return 0;
}

static int on_header_value(llhttp_t* parser, const char* at, size_t length) {
  Request* req = GET_PARSER_REQUEST(parser);
  BalmStringViewNode* prev = req->recv_values;
  req->recv_values =
      (BalmStringViewNode*) New_BalmStringView((char*) at, length);
  req->recv_values->next = prev;
  req->settings.on_header_value = on_header_value_next;
  return 0;
}

static int on_header_value_complete(llhttp_t* parser) {
  Request* req = GET_PARSER_REQUEST(parser);
  req->settings.on_header_field = on_header_field;
  req->settings.on_header_value = on_header_value;
  return 0;
}


static void free_request(Request* req) {
  free(req->send.bufs);
  if(req->resp.status) {
    PyGILState_STATE state = PyGILState_Ensure();
    Py_DECREF(req->resp.status);
    Py_DECREF(req->resp.headers);
    if(req->write_status == WRITE_LIST)
      Py_DECREF(req->write.data);
    PyGILState_Release(state);
  }
}

static void free_request_worker(uv_work_t* thread) {
  free_request(GET_THREAD_REQUEST(thread));
}

static void free_request_cb(uv_work_t* thread, int /*status*/) {
  free(GET_THREAD_REQUEST(thread));
}


static void on_close(uv_handle_t* handle) {
  Request* req = (Request*) handle;

  // We might need to acquire the GIL, so do this in a thread
  uv_queue_work(uv_default_loop(), &req->thread, free_request_worker,
      free_request_cb);
}

static uv_buf_t* get_buf(Request* req) {
  if(req->send.used == req->send.len) {
    size_t sz = sizeof(*req->send.bufs) * req->send.len * 2;
    req->send.bufs = realloc(req->send.bufs, sz);
    memset(req->send.bufs + req->send.len, 0, req->send.len);
    req->send.len *= 2;
  }

  return &(req->send.bufs[req->send.used++]);
}

// ToDo PyObject type checking
static int buffer_headers(Request* req) {
  BUFFER_STR(get_buf(req), _cHTTP_1_1);
  BUFFER_PYSTR(get_buf(req), req->resp.status);

  Py_ssize_t len = PyList_GET_SIZE(req->resp.headers);

  for(Py_ssize_t i = 0; i < len; ++i) {
    PyObject* tuple = PyList_GET_ITEM(req->resp.headers, i);
    PyObject* field = PyTuple_GET_ITEM(tuple, 0);
    PyObject* value = PyTuple_GET_ITEM(tuple, 1);

    BUFFER_STR(get_buf(req), _cHTTP_rn);
    BUFFER_PYSTR(get_buf(req), field);
    BUFFER_STR(get_buf(req), _cHTTP_colon);
    BUFFER_PYSTR(get_buf(req), value);
  }
  return 0;
}

static int handle_list(Request* req, PyObject* op) {

  buffer_headers(req);

  Py_ssize_t sz = PyList_GET_SIZE(op);
  if(!sz) {
    BUFFER_STR(get_buf(req), _cHTTP_rnrn);
    return 0;
  }

  Py_INCREF(op);
  req->write.data = op;
  req->write_status = WRITE_LIST;

  size_t cl = 0;
  for(Py_ssize_t i = 0; i < sz; ++i)
    cl += PyBytes_GET_SIZE(PyList_GET_ITEM(op, i));

  int len = sprintf(req->resp.conlen, "\r\nContent-Length: %zu\r\n\r\n", cl);
  BUFFER_STR_SIZE(get_buf(req), req->resp.conlen, len);

  for(Py_ssize_t i = 0; i < sz; ++i)
    BUFFER_PYBYTES(get_buf(req), PyList_GET_ITEM(op, i));

  req->state.processing = false;
  return 0;
}

static int handle_tuple(Request* req, PyObject* op) {
  buffer_headers(req);

  Py_ssize_t sz = PyTuple_GET_SIZE(op);
  if(!sz) {
    BUFFER_STR(get_buf(req), _cHTTP_rnrn);
    return 0;
  }

  Py_INCREF(op);
  req->write.data = op;
  req->write_status = WRITE_LIST;

  size_t cl = 0;
  for(Py_ssize_t i = 0; i < sz; ++i)
    cl += PyBytes_GET_SIZE(PyTuple_GET_ITEM(op, i));

  int len = sprintf(req->resp.conlen, "\r\nContent-Length: %zu\r\n\r\n", cl);
  BUFFER_STR_SIZE(get_buf(req), req->resp.conlen, len);

  for(Py_ssize_t i = 0; i < sz; ++i)
    BUFFER_PYBYTES(get_buf(req), PyTuple_GET_ITEM(op, i));

  req->state.processing = false;
  return 0;
}

// ToDo More type checking
static int handle_app_ret(Request* req, PyObject* op) {
  if(PyList_Check(op)) {
    return handle_list(req, op);
  } else if(PyTuple_Check(op)) {
    return handle_tuple(req, op);
  } else {
    BUFFER_STR(get_buf(req), _cHTTP_rnrn);
    req->write_status = WRITE_ITER;
    return 0;
  }
  return 1;
}

static void start_response_worker(uv_work_t* thread) {
  Request* req = GET_THREAD_REQUEST(thread);
  PyGILState_STATE state = PyGILState_Ensure();
  PyObject* environ = build_environ(req);
  PyObject* sr = PyCFunction_New(&_START_RESPONSE_DEF, NULL);

  // Illegal pointer smuggling, don't tell the Python border authorities
  ((PyCFunctionObject*) sr)->m_self = (PyObject*) req;
  PyObject* ret = PyObject_CallFunctionObjArgs(req->app, environ, sr, NULL);
  ((PyCFunctionObject*) sr)->m_self = NULL;

  Py_XINCREF(ret);
  Py_DECREF(environ);
  Py_DECREF(sr);

  if(!ret || handle_app_ret(req, ret)) {
    req->state.borked = true;
    PyErr_Print();
  }

  PyGILState_Release(state);
}

static void error_write_cb(uv_write_t* write, int /*status*/) {
  Request* req = GET_WRITE_REQUEST(write);
  uv_close((uv_handle_t*) req, on_close);
}

static void start_processing(Request* req);

static void happy_write_cb(uv_write_t* write, int /*status*/) {
  Request* req = GET_WRITE_REQUEST(write);
  if(!req->state.processing && req->state.keepalive) {
    req->send.used = 0;
    req->resp.status = NULL;

    req->state.sending = false;
    if(req->state.received)
      start_processing(req);
  }
}

static void resume_recv(Request* req);

static void start_response_worker_cb(uv_work_t* thread, int /*status*/) {
  Request* req = GET_THREAD_REQUEST(thread);

  if(req->state.borked) {
    BUFFER_STR(req->send.bufs, _cHTTP_500);
    uv_write(&req->write, (uv_stream_t*) req, req->send.bufs, 1,
        error_write_cb);
    return;
  }

  req->state.received = false;
  req->state.sending = true;

  if(req->state.keepalive)
    resume_recv(req);

  uv_write(&req->write, (uv_stream_t*) req, req->send.bufs, req->send.used,
      happy_write_cb);
}

static void start_processing(Request* req) {
  req->proc_headers = req->recv_headers;
  req->proc_values = req->recv_values;
  req->recv_headers = NULL;
  req->recv_values = NULL;
  req->state.processing = true;
  uv_queue_work(uv_default_loop(), &req->thread, start_response_worker,
      start_response_worker_cb);
}

static int on_message_complete(llhttp_t* parser) {
  Request* req = GET_PARSER_REQUEST(parser);
  req->state.received = true;
  uv_read_stop((uv_stream_t*) req);

  req->state.keepalive = llhttp_should_keep_alive(parser);
  return req->state.keepalive ? HPE_PAUSED : 0;
}

static llhttp_settings_t init_settings = {
    .on_url = on_url,
    .on_url_complete = on_url_complete,
    .on_header_field = on_header_field,
    .on_header_value = on_header_value,
    .on_header_value_complete = on_header_value_complete,
    .on_message_complete = on_message_complete,
};

static Request* alloc_request(Server* srv, size_t buf_size, size_t hdr_count,
    size_t send_count) {
  Request* req = malloc(sizeof(*req) + buf_size);

  memset(&req->state, 0, sizeof(req->state));

  req->app = srv->app;
  req->base_environ = srv->base_environ;

  req->settings = init_settings;
  llhttp_init(&req->parser, HTTP_REQUEST, &req->settings);

  req->resp.status = NULL;
  req->resp.headers = NULL;
  req->write_status = WRITE_NONE;

  req->recv_headers = NULL;
  req->recv_values = NULL;

  size_t sz = sizeof(*req->send.bufs) * send_count;
  req->send.bufs = malloc(sz);
  memset(req->send.bufs, 0, sz);
  req->send.len = send_count;
  req->send.used = 0;

  req->url = NULL;
  req->query = NULL;

  req->parse_idx = 0;
  req->len = buf_size;
  req->used = 0;

  return req;
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

static void handle_parse(Request* req);

static void on_read(uv_stream_t* client, ssize_t nRead, const uv_buf_t* buf) {
  if(nRead < 0) {
    uv_close((uv_handle_t*) client, on_close);
    return;
  }

  Request* req = (Request*) client;
  req->used += nRead;

  handle_parse(req);
}

static void resume_recv(Request* req) {
  req->parse_idx = 0;

  const char* endp = llhttp_get_error_pos(&req->parser);
  const char* used_endp = req->buf + req->used;

  if(endp == used_endp) {
    req->used = 0;
  } else {
    size_t dist = used_endp - endp;
    memcpy(req->buf, endp, dist);
    req->used = dist;
  }

  llhttp_resume(&req->parser);
  uv_read_start((uv_stream_t*) req, alloc_buffer, on_read);
  if(req->used)
    handle_parse(req);
}

static void handle_parse(Request* req) {
  llhttp_errno_t err = exec_parse(req);

  if(err != HPE_OK && err != HPE_PAUSED)
    uv_close((uv_handle_t*) req, on_close);
  else if(req->state.received && !req->state.processing && !req->state.sending)
    start_processing(req);
}

static void on_connect(uv_stream_t* handle, int status) {
  if(status < 0)
    return;

  Request* req = alloc_request((Server*) handle, DEFAULT_BUF_SIZE,
      DEFAULT_HEADER_COUNT, DEFAULT_SEND_BUFFERS);

  uv_tcp_init(uv_default_loop(), (uv_tcp_t*) req);
  if(!uv_accept(handle, (uv_stream_t*) req)) {
    uv_read_start((uv_stream_t*) req, alloc_buffer, on_read);
  } else {
    uv_close((uv_handle_t*) req, on_close);
  }
}

Py_LOCAL_SYMBOL void init() {
#define X(str) _##str = PyUnicode_FromString(#str);
  COMMON_STRINGS(X)
#undef X

  init_constants();
  balm_init();

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

Py_LOCAL_SYMBOL int run_server(PyObject* app, char* host, unsigned port,
    unsigned backlog) {
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

  uv_getaddrinfo_t info = {};
  ret = uv_ip4_addr(host, port, &host_addr);
  if(ret) {
    static struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
    };
    ret = uv_getaddrinfo(uv_default_loop(), &info, NULL, host, NULL, &hints);
    if(ret)
      return ret;
    host_addr = *((struct sockaddr_in*) info.addrinfo->ai_addr);
    host_addr.sin_port = htons(port);
  }

  ret = uv_tcp_bind((uv_tcp_t*) &server, (struct sockaddr*) &host_addr, 0);
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
