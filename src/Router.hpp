#ifndef VELOCEM_ROUTER_HPP
#define VELOCEM_ROUTER_HPP

#include <array>
#include <memory>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

using std::operator""sv;

#include "absl/container/flat_hash_map.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "util/Constants.hpp"

namespace velocem {

struct CaptureNode;

struct Node {
  absl::flat_hash_map<std::string, Node> abs;
  std::unique_ptr<CaptureNode> cap;
  PyObject* pyo = nullptr;
};

struct CaptureNode : Node {
  std::string name;
};

struct Router;

struct RegClosure {
  HTTPMethod meth;
  Router* router;
  PyObject* route;

  static PyObject* alloc(HTTPMethod meth, Router* router, PyObject* route) {
    Py_INCREF(router);
    Py_INCREF(route);
    auto rg {new RegClosure {meth, router, route}};
    return PyCapsule_New(rg, nullptr, dealloc);
  }

  static void dealloc(PyObject* cap) {
    auto rg {static_cast<RegClosure*>(PyCapsule_GetPointer(cap, nullptr))};
    Py_DECREF(rg->router);
    Py_DECREF(rg->route);
    delete rg;
  }
};

struct Router : PyObject {

  std::array<Node, static_cast<std::size_t>(HTTPMethod::MAX)> routes;

  using RouteData = std::pair<PyObject*,
      std::vector<std::pair<std::string_view, std::string_view>>>;

private:
  friend void init_gVT(PyObject* mod);
  static void init_type(PyTypeObject* RouterType) {
    static std::array<PyMethodDef, 6> meths {
        PyMethodDef {"get", (PyCFunction) get, METH_FASTCALL},
        PyMethodDef {"post", (PyCFunction) post, METH_FASTCALL},
        PyMethodDef {"put", (PyCFunction) put, METH_FASTCALL},
        PyMethodDef {"delete", (PyCFunction) delete_, METH_FASTCALL},
        PyMethodDef {"get_route", (PyCFunction) get_route, METH_FASTCALL},
        {nullptr, nullptr},
    };

    *RouterType = PyTypeObject {
        .tp_name = "Router",
        .tp_basicsize = sizeof(Router),
        .tp_dealloc = (destructor) dealloc,
        .tp_methods = meths.data(),
        .tp_new = (newfunc) new_,
    };
  }

  RouteData get_app(HTTPMethod meth, std::string_view route) {
    std::vector<std::pair<std::string_view, std::string_view>> captures;
    Node* op = &routes[static_cast<std::size_t>(meth)];
    if(op) {
      for(auto slug : std::views::split(route, "/"sv)) {
        if(slug.empty())
          continue;

        auto sv {std::string_view(slug)};
        if(auto it {op->abs.find(sv)}; it != op->abs.end()) {
          op = &it->second;
          continue;
        }

        if(op->cap) {
          if(!op->cap->name.empty())
            captures.emplace_back(op->cap->name, sv);
          op = op->cap.get();
          continue;
        }

        op = nullptr;
        break;
      }
    }

    return {op ? op->pyo : nullptr, captures};
  }

  void reg_route(HTTPMethod meth, std::string_view route, PyObject* app) {
    Node* op = &routes[static_cast<std::size_t>(meth)];
    for(auto slug : std::views::split(route, "/"sv)) {
      if(slug.empty())
        continue;

      auto sv {std::string_view(slug)};
      if(sv.front() == '{' && sv.back() == '}') {
        sv = sv.substr(1, sv.size() - 2);
        if(!op->cap) {
          op->cap = std::make_unique<CaptureNode>();
          op->cap->name = sv;
        }
        op = op->cap.get();
        continue;
      }

      op = &op->abs[sv];
    }
    if(op->pyo)
      Py_DECREF(op->pyo);

    Py_INCREF(app);
    op->pyo = app;
  }

  static PyObject* get_route(Router* self, PyObject* const* args,
      Py_ssize_t nargs) {
    PyObject* meth;
    PyObject* route;
    if(!_PyArg_ParseStack(args, nargs, "O!O!:get_route", &PyUnicode_Type, &meth,
           &PyUnicode_Type, &route))
      return nullptr;

    Py_ssize_t sz;
    const char* c {PyUnicode_AsUTF8AndSize(meth, &sz)};
    HTTPMethod hmeth {str2meth({c, static_cast<std::size_t>(sz)})};

    c = {PyUnicode_AsUTF8AndSize(route, &sz)};
    auto r {self->get_app(hmeth, {c, static_cast<std::size_t>(sz)})};
    if(!r.first) {
      PyErr_SetString(PyExc_TypeError, "No such route");
      return nullptr;
    }

    PyObject* dict {_PyDict_NewPresized(r.second.size())};
    if(!dict)
      return nullptr;

    for(auto& [key, val] : r.second) {
      PyObject* k {PyUnicode_FromStringAndSize(key.data(), key.size())};
      if(!k) {
        Py_DECREF(dict);
        return nullptr;
      }
      PyObject* v {PyUnicode_FromStringAndSize(val.data(), val.size())};
      if(!v) {
        Py_DECREF(k);
        Py_DECREF(dict);
        return nullptr;
      }
      PyDict_SetItem(dict, k, v);
      Py_DECREF(k);
      Py_DECREF(v);
    }

    PyObject* tp {PyTuple_Pack(2, r.first, dict)};
    Py_DECREF(dict);
    return tp;
  }

  static PyObject* route(HTTPMethod meth, Router* self, PyObject* const* args,
      Py_ssize_t nargs) {
    PyObject* pyo;
    if(!_PyArg_ParseStack(args, nargs, "O!:get", &PyUnicode_Type, &pyo))
      return nullptr;

    static PyMethodDef regdef {
        .ml_name = "register_route",
        .ml_meth = (PyCFunction) register_route,
        .ml_flags = METH_FASTCALL,
    };

    auto cap {RegClosure::alloc(meth, self, pyo)};
    if(!cap)
      return nullptr;

    auto f {PyCFunction_New(&regdef, cap)};
    if(!f) {
      Py_DECREF(cap);
      return nullptr;
    }

    return f;
  }

  static PyObject* get(Router* self, PyObject* const* args, Py_ssize_t nargs) {
    return route(HTTPMethod::Get, self, args, nargs);
  }

  static PyObject* post(Router* self, PyObject* const* args, Py_ssize_t nargs) {
    return route(HTTPMethod::Post, self, args, nargs);
  }

  static PyObject* put(Router* self, PyObject* const* args, Py_ssize_t nargs) {
    return route(HTTPMethod::Put, self, args, nargs);
  }

  static PyObject* delete_(Router* self, PyObject* const* args,
      Py_ssize_t nargs) {
    return route(HTTPMethod::Delete, self, args, nargs);
  }

  static PyObject* register_route(PyObject* self, PyObject* const* args,
      Py_ssize_t nargs) {
    PyObject* pyo;
    if(!_PyArg_ParseStack(args, nargs, "O!:register_route", &PyFunction_Type,
           &pyo))
      return nullptr;

    auto rg {static_cast<RegClosure*>(PyCapsule_GetPointer(self, nullptr))};
    Py_ssize_t sz;
    const char* c {PyUnicode_AsUTF8AndSize(rg->route, &sz)};
    rg->router->reg_route(rg->meth, {c, static_cast<std::size_t>(sz)}, pyo);
    return pyo;
  }

  static void dealloc_node(Node& node) {
    if(node.pyo)
      Py_DECREF(node.pyo);

    if(node.cap)
      dealloc_node(*node.cap);

    for(auto& [_, child] : node.abs)
      dealloc_node(child);
  }

  static void dealloc(Router* self) {
    for(auto& node : self->routes)
      dealloc_node(node);
    self->~Router();
    self->ob_type->tp_free(self);
  }

  static Router* new_(PyTypeObject* subtype, PyObject*, PyObject*) {
    return new(subtype->tp_alloc(subtype, 0)) Router;
  }
};

} // namespace velocem

#endif // VELOCEM_ROUTER_HPP
