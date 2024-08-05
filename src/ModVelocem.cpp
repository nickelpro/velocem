#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "Constants.hpp"
#include "WSGIServer.hpp"

namespace {

PyMethodDef VelocemMethods[] {
    {"wsgi", (PyCFunction) velocem::run_wsgi_server,
        METH_FASTCALL | METH_KEYWORDS},
    {0},
};

PyModuleDef VelocemModule {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "velocem",
    .m_doc = "Hyperspeed Python Web Framework",
    .m_size = -1,
    .m_methods = VelocemMethods,
};

} // namespace

PyMODINIT_FUNC PyInit_velocem(void) {
  auto mod {PyModule_Create(&VelocemModule)};
  if(!mod)
    return nullptr;
  if(PyModule_AddStringConstant(mod, "__version__", "0.0.10") == -1)
    return nullptr;
  velocem::init_gPO();
  return mod;
}
