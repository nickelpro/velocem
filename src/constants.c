#include "string.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "constants.h"
#include "util.h"

// clang-format off
HttpMeth HTTP_METHS[HTTP_METH_MAX] = {
#define HTTP_METHOD(code, name)                                                \
  [code] = {                                                                   \
      ._base = {                                                               \
          .ob_base = {                                                         \
              .ob_refcnt = _Py_IMMORTAL_REFCNT,                                \
              .ob_type = NULL,                                                 \
          },                                                                   \
          .length = STRSZ(#name),                                              \
          .hash = -1,                                                          \
          .state = {                                                           \
              .interned = 0,                                                   \
              .kind = PyUnicode_1BYTE_KIND,                                    \
              .compact = 1,                                                    \
              .ascii = 1,                                                      \
          },                                                                   \
      },                                                                       \
      .data = #name,                                                           \
  },
#include "defs/http_method.def"
};
// clang-format on

void init_constants() {
  for(HttpMeth* meth = HTTP_METHS; meth < HTTP_METHS + HTTP_METH_MAX; ++meth) {
    meth->_base.ob_base.ob_type = &PyUnicode_Type;
    meth->_base.hash = PyObject_Hash((PyObject*) meth);
  }
}
