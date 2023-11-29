#ifndef VELOCEM_GIL_BALM_H
#define VELOCEM_GIL_BALM_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

typedef PyUnicodeObject BalmString;
typedef PyUnicodeObject BalmStringView;
typedef PyDictObject BalmDict;

#define BALM_STRING_ALLOCATION_BLOCK_SIZE (1 << 8)
#define BALM_DICT_ALLOCATION_BLOCK_SIZE (1 << 8)

Py_LOCAL_SYMBOL void balm_init();

typedef struct BalmStringNode {
  BalmString str;
  struct BalmStringNode* next;
} BalmStringNode;

typedef BalmStringNode BalmStringViewNode;

Py_LOCAL_SYMBOL BalmString* New_BalmString(char* data, size_t len);
Py_LOCAL_SYMBOL BalmStringView* New_BalmStringView(char* data, size_t len);

Py_LOCAL_SYMBOL void UpdateLen_BalmString(BalmString* str, size_t len);
Py_LOCAL_SYMBOL void UpdateLen_BalmStringView(BalmStringView* str, size_t len);

Py_LOCAL_SYMBOL Py_ssize_t GetLen_BalmString(BalmString* str);
Py_LOCAL_SYMBOL Py_ssize_t GetLen_BalmStringView(BalmStringView* str);

Py_LOCAL_SYMBOL char* GetData_BalmString(BalmString* str);
Py_LOCAL_SYMBOL char* GetData_BalmStringView(BalmStringView* str);

Py_LOCAL_SYMBOL char* SwapBuffer_BalmString(BalmString* str, char* data,
    size_t len, size_t* old_len);

typedef struct BalmDictNode {
  BalmDict dict;
  struct BalmDictNode* next;
} BalmDictNode;

#endif // VELOCEM_GIL_BALM_H
