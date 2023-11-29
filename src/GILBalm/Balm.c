#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "Balm.h"
#include "balm_threads.h"

static PyTypeObject BalmStringView_Type;
static PyTypeObject BalmString_Type;

static struct {
  mtx_t lock;
  BalmStringNode* head;
} strList;

static void balmstr_init(BalmStringNode* str, BalmStringNode* next) {
  // clang-format off
  *str = (BalmStringNode) {
      .next = next,
      .str = {
          ._base = {
              ._base = {
                  .hash = -1,
                  .state = {
                      .interned = 0,
                      .kind = PyUnicode_1BYTE_KIND,
                      .compact = 0,
                      .ascii = 1,
                  },
              },
          },
      }
  };
  // clang-format on
}

BalmStringNode* balmstr_block_alloc(size_t len) {
  BalmStringNode* base = malloc(sizeof(*base) * len);
  BalmStringNode* prev = NULL;
  for(BalmStringNode* v = base; v < base + len; ++v) {
    balmstr_init(v, prev);
    prev = v;
  }
  return &base[len - 1];
}

static void balmstr_push(PyObject* str) {
  BalmStringNode* node = (BalmStringNode*) str;
  mtx_lock(&strList.lock);
  node->next = strList.head;
  strList.head = node;
  mtx_unlock(&strList.lock);
}

static BalmString* balmstr_pop() {
  mtx_lock(&strList.lock);
  if(!strList.head)
    strList.head = balmstr_block_alloc(BALM_STRING_ALLOCATION_BLOCK_SIZE);
  BalmStringNode* node = strList.head;
  strList.head = node->next;
  mtx_unlock(&strList.lock);
  return &node->str;
}

static void balmstrview_dealloc(PyObject* str) {
  balmstr_push(str);
}

static void balmstr_dealloc(PyObject* str) {
  free(((BalmString*) str)->_base.utf8);
  balmstr_push(str);
}

static BalmString* new_balmstring(char* data, size_t len) {
  BalmString* str = balmstr_pop();
  str->_base._base.hash = -1;
  str->_base._base.length = len;
  str->_base.utf8_length = len;
  str->_base.utf8 = data;
  str->data.any = data;
  str->_base._base.ob_base.ob_refcnt = 0;
  return str;
}

Py_LOCAL_SYMBOL BalmString* New_BalmString(char* data, size_t len) {
  BalmString* str = new_balmstring(data, len);
  str->_base._base.ob_base.ob_type = &BalmString_Type;
  return str;
}

Py_LOCAL_SYMBOL BalmStringView* New_BalmStringView(char* data, size_t len) {
  BalmString* str = new_balmstring(data, len);
  str->_base._base.ob_base.ob_type = &BalmStringView_Type;
  return str;
}

Py_LOCAL_SYMBOL Py_ssize_t GetLen_BalmString(BalmString* str) {
  return str->_base.utf8_length;
}

Py_LOCAL_SYMBOL Py_ssize_t GetLen_BalmStringView(BalmStringView* str) {
  return str->_base.utf8_length;
}

Py_LOCAL_SYMBOL char* GetData_BalmString(BalmString* str) {
  return str->_base.utf8;
}

Py_LOCAL_SYMBOL char* GetData_BalmStringView(BalmStringView* str) {
  return str->_base.utf8;
}

Py_LOCAL_SYMBOL void UpdateLen_BalmString(BalmString* str, size_t len) {
  str->_base._base.length = len;
  str->_base.utf8_length = len;
}

Py_LOCAL_SYMBOL void UpdateLen_BalmStringView(BalmStringView* str, size_t len) {
  str->_base._base.length = len;
  str->_base.utf8_length = len;
}

Py_LOCAL_SYMBOL char* SwapBuffer_BalmString(BalmString* str, char* data,
    size_t len, size_t* old_len) {
  char* old = str->_base.utf8;
  str->_base.utf8 = data;
  str->data.any = data;
  if(old_len)
    *old_len = str->_base.utf8_length;
  UpdateLen_BalmString(str, len);
  return old;
}

static PyTypeObject BalmDict_Type;

static struct {
  mtx_t lock;
} dictList;

static void balmdict_init(BalmDictNode* dict, BalmDictNode* next) {
  // clang-format off
  *dict = (BalmDictNode) {
      .next = next,
      .dict = {
          .ob_base = {
              .ob_type = &BalmDict_Type,
          }
      },
  };
  // clang-format on
}

static void balmdict_dealloc(PyObject* dict) {}

Py_LOCAL_SYMBOL void balm_init() {
  BalmString_Type = PyUnicode_Type;
  BalmString_Type.tp_new = 0;
  BalmString_Type.tp_free = 0;
  BalmString_Type.tp_dealloc = balmstr_dealloc;

  BalmStringView_Type = BalmString_Type;
  BalmStringView_Type.tp_dealloc = balmstrview_dealloc;

  BalmDict_Type = PyDict_Type;
  BalmDict_Type.tp_new = 0;
  BalmDict_Type.tp_free = 0;
  BalmDict_Type.tp_dealloc = balmdict_dealloc;

  mtx_init(&strList.lock, mtx_plain);
  strList.head = balmstr_block_alloc(BALM_STRING_ALLOCATION_BLOCK_SIZE);
}
