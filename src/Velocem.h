#ifndef VELOCEM_H
#define VELOCEM_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

void init();

int run_server(char* host, PyObject* app, unsigned port, unsigned backlog);

#endif
