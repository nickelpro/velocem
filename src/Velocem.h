#ifndef VELOCEM_H
#define VELOCEM_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

Py_LOCAL_SYMBOL void init();

Py_LOCAL_SYMBOL int run_server(PyObject* app, char* host, unsigned port,
    unsigned backlog);

#endif
