#ifndef VELOCEM_GIL_BALM_H
#define VELOCEM_GIL_BALM_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

typedef PyDictObject BalmDict;

#define BALM_STRING_ALLOCATION_BLOCK_SIZE (1 << 8)
#define BALM_DICT_ALLOCATION_BLOCK_SIZE (1 << 8)

Py_LOCAL_SYMBOL void balm_init();

typedef union {
  union {
    PyASCIIObject ascii;
    struct {
      // Force alignment on data[] to be correct
      struct {
        PyObject_HEAD;
        Py_ssize_t length;
        Py_hash_t hash;
        struct {
          unsigned int interned : 2;
          unsigned int kind : 3;
          unsigned int compact : 1;
          unsigned int ascii : 1;
          unsigned int statically_allocated : 1;
          // Balm state data, lives in unused padding bytes
          unsigned int balm : 2;
          unsigned int : 22;
        } state;
      };
      char data[];
    };
  };
  PyUnicodeObject uc;
} BalmString;

#define BALM_COMPACT_ALLOCATION 128
#define BALM_COMPACT_MAX_STR (BALM_COMPACT_ALLOCATION - sizeof(PyASCIIObject))

enum BalmStringType {
  BALM_STRING_VIEW,
  BALM_STRING_COMPACT,
  BALM_STRING_BIG,
};

typedef struct BalmStringNode {
  struct BalmStringNode* next;
  BalmString str;
} BalmStringNode;

Py_LOCAL_SYMBOL BalmString* New_BalmString(size_t len);
Py_LOCAL_SYMBOL BalmString* New_BalmStringView(char* data, size_t len);

Py_LOCAL_SYMBOL char* GetData_BalmString(BalmString* str);

typedef struct BalmDictNode {
  BalmDict dict;
  struct BalmDictNode* next;
} BalmDictNode;

#endif // VELOCEM_GIL_BALM_H
