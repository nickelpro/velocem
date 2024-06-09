#ifndef VELOCEM_GIL_BALM_H
#define VELOCEM_GIL_BALM_H

#include <array>
#include <cstdlib>
#include <functional>
#include <string_view>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

namespace velocem {

struct BalmStringView : PyUnicodeObject {
  BalmStringView(std::function<void(BalmStringView*)> f_dealloc,
      char* base = nullptr, std::size_t length = 0)
      : f_dealloc {f_dealloc} {

    static PyTypeObject type = []() {
      PyTypeObject ret = PyUnicode_Type;
      ret.tp_new = nullptr;
      ret.tp_free = nullptr;
      ret.tp_dealloc = dealloc;
      return ret;
    }();

    data.any = base;
    _base = {
        ._base =
            {
                .ob_base = {.ob_type = &type},
                .length = (Py_ssize_t) length,
                .state = {.kind = PyUnicode_1BYTE_KIND, .ascii = 1},
            },
        .utf8_length = (Py_ssize_t) length,
        .utf8 = base,
    };
  }

  void from(char* at, std::size_t length) {
    data.any = at;
    _base.utf8 = at;
    _base.utf8_length = length;
    _base._base.length = length;
  }

  void extend(std::size_t length) {
    _base.utf8_length += length;
    _base._base.length += length;
  }

  void resize(std::size_t length) {
    _base.utf8_length = length;
    _base._base.length = length;
  }

private:
  const std::function<void(BalmStringView*)> f_dealloc;

  static void dealloc(PyObject* self) {
    auto ptr {reinterpret_cast<BalmStringView*>(self)};
    ptr->f_dealloc(ptr);
  }
};

inline auto format_as(BalmStringView sv) {
  return std::string_view {sv._base.utf8, (std::size_t) sv._base.utf8_length};
}

} // namespace velocem

#endif // VELOCEM_GIL_BALM_H
