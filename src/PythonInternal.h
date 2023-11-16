#ifndef VELOCEM_PYTHON_INTERNAL_H
#define VELOCEM_PYTHON_INTERNAL_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>


// Added in Python 3.7
// Moved from cpython/modsupport.h -> internal/pycore_modsupport.h in
// Python 3.12, so we mirror it here
// See: https://github.com/python/cpython/pull/110966
PyAPI_FUNC(int) _PyArg_ParseStack(PyObject* const* args, Py_ssize_t nargs,
    const char* format, ...);

PyAPI_FUNC(int) _PyArg_ParseStackAndKeywords(PyObject* const* args,
    Py_ssize_t nargs, PyObject* kwnames, struct _PyArg_Parser*, ...);

#endif // VELOCEM_PYTHON_INTERNAL
