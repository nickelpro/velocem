#ifndef VELOCEM_WSGI_SERVER_HPP
#define VELOCEM_WSGI_SERVER_HPP

#include <Python.h>

namespace velocem {

PyObject* run_wsgi_server(PyObject* /* self */, PyObject* const* args,
    Py_ssize_t nargs, PyObject* kwnames);

} // namespace velocem

#endif // VELOCEM_WSGI_SERVER_HPP
