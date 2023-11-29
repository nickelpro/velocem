#include <stdio.h>
#include <string.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "GILBalm/Balm.h"
#include "Velocem.h"

static PyObject* run(PyObject* self, PyObject* const* args, Py_ssize_t nargs,
    PyObject* kwnames) {
  static const char* _keywords[] = {"", "host", "port", "listen_backlog", NULL};
  static _PyArg_Parser _parser = {
      .initialized = 0,
      .keywords = _keywords,
      .format = "O|sII:run",
  };

  PyObject* app;
  char* host = "127.0.0.1";
  unsigned port = 8000;
  unsigned listen_backlog = 1024;

  if(!_PyArg_ParseStackAndKeywords(args, nargs, kwnames, &_parser, &app, &host,
         &port, &listen_backlog))
    return NULL;

  Py_INCREF(app);

  PySys_WriteStdout("Running server at http://%s:%u\n", host, port);
  if(run_server(app, host, port, listen_backlog))
    return NULL;

  Py_DECREF(app);
  Py_RETURN_NONE;
}

static PyMethodDef VelocemMethods[] = {
    {"run", (PyCFunction) run, METH_FASTCALL | METH_KEYWORDS},
    {0},
};

static struct PyModuleDef VelocemModule = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "velocem",
    .m_doc = "velocem docsting",
    .m_size = -1,
    .m_methods = VelocemMethods,
};

PyMODINIT_FUNC PyInit_velocem(void) {
  init();
  PyObject* mod = PyModule_Create(&VelocemModule);
  if(!mod)
    return NULL;
  if(PyModule_AddStringConstant(mod, "__version__", "0.0.1") == -1)
    return NULL;
  return mod;
}
