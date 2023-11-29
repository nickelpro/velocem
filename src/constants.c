#include "string.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "constants.h"
#include "util.h"

HttpMeth HTTP_METHS[HTTP_METH_MAX];

static HttpMeth init_meth(size_t number, char* data, size_t data_len) {
  HttpMeth* meth = &HTTP_METHS[number];
  // clang-format off
  *meth = (HttpMeth) {
      ._base = {
          .ob_base = {
              .ob_refcnt = _Py_IMMORTAL_REFCNT,
              .ob_type   = &PyUnicode_Type,
          },
          .length  = data_len,
          .hash    = -1,
          .state   = {
              .interned = 0,
              .kind = PyUnicode_1BYTE_KIND,
              .compact = 1,
              .ascii = 1,
          },
      },
  };
  // clang-format on

  // Globally allocated objects are zero initialized, don't need to tack on the
  // null terminator
  memcpy(meth->data, data, data_len);
  meth->_base.hash = PyUnicode_Type.tp_hash((PyObject*) meth);
}

void init_constants() {

#define HTTP_METHOD(code, name) init_meth(code, #name, STRSZ(#name));
#include "defs/http_method.def"
}
