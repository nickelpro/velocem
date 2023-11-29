#ifndef VELOCEM_CONSTANTS_H
#define VELOCEM_CONSTANTS_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define HTTP_METH_MAX 46

// Technically 14, ("GET_PARAMETER" + Null Byte), but powers of 2 are cool
#define HTTP_METH_MAX_STRING_SIZE 16

typedef struct {
  PyASCIIObject _base;
  char data[HTTP_METH_MAX_STRING_SIZE];
} HttpMeth;

Py_LOCAL_SYMBOL extern HttpMeth HTTP_METHS[HTTP_METH_MAX];

Py_LOCAL_SYMBOL void init_constants();

#endif // VELOCEM_CONSTANT_H
