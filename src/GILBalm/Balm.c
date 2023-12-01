#include <stdio.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "Balm.h"
#include "balm_threads.h"

#include "util.h"

static PyTypeObject BalmStringView_Type;
static PyTypeObject BalmStringCompact_Type;
static PyTypeObject BalmStringBig_Type;

typedef struct {
  mtx_t lock;
  BalmStringNode* head;
} StrPool;

static struct {
  size_t outstanding;
  StrPool bigviews;
  StrPool compacts;
} str_pools;

BalmStringNode* compactbalmstr_block_alloc(size_t len) {
  size_t sz = BALM_COMPACT_ALLOCATION * len;
  BalmStringNode* base = malloc(sz);
  char* cur = (char*) base;
  char* end = cur + sz;
  BalmStringNode* prev = NULL;

  for(BalmStringNode* v = base; cur < end;
      v = (BalmStringNode*) (cur += BALM_COMPACT_ALLOCATION)) {
    v->next = prev;
    prev = v;
    // clang-format off
    v->str = (BalmString) {
        .ob_base.ob_type = &BalmStringCompact_Type,
        .state = {
            .kind = PyUnicode_1BYTE_KIND,
            .compact = 1,
            .ascii = 1,
            .balm = BALM_STRING_COMPACT,
        },
    };
    // clang-format on
  }

  return prev;
}

BalmStringNode* balmstr_block_alloc(size_t len) {
  BalmStringNode* base = malloc(sizeof(*base) * len);
  BalmStringNode* prev = NULL;
  for(BalmStringNode* v = base; v < base + len; ++v) {
    v->next = prev;
    prev = v;
    v->str = (BalmString) {.state = {.kind = PyUnicode_1BYTE_KIND, .ascii = 1}};
  }
  return prev;
}

static void balmstr_push(StrPool* pool, PyObject* str) {
  BalmStringNode* node = GET_NODE_PYOBJ(str);
  mtx_lock(&pool->lock);
  node->next = pool->head;
  pool->head = node;
  mtx_unlock(&pool->lock);
}

static BalmString* balmstr_pop(StrPool* pool, BalmStringNode* (*alloc)(size_t),
    size_t alloc_len) {
  mtx_lock(&pool->lock);
  if(!pool->head)
    pool->head = alloc(alloc_len);
  BalmStringNode* node = pool->head;
  pool->head = node->next;
  mtx_unlock(&pool->lock);
  return &node->str;
}

static void balmstrview_dealloc(PyObject* str) {
  balmstr_push(&str_pools.bigviews, str);
}

static void balmstrbig_dealloc(PyObject* str) {
  free(((BalmString*) str)->uc.data.any);
  balmstr_push(&str_pools.bigviews, str);
}

static void balmstrcompact_dealloc(PyObject* str) {
  balmstr_push(&str_pools.compacts, str);
}

Py_LOCAL_SYMBOL BalmString* New_BalmString(size_t len) {
  BalmString* str;
  if(len > BALM_COMPACT_MAX_STR) {
    BalmString* str = balmstr_pop(&str_pools.bigviews, balmstr_block_alloc,
        BALM_STRING_ALLOCATION_BLOCK_SIZE);
    str->ob_base.ob_type = &BalmStringBig_Type;
    str->uc.data.any = malloc(len);
    str->uc._base.utf8 = str->uc.data.any;
    str->uc._base.utf8_length = len;
    str->length = len;
    str->state.balm = BALM_STRING_BIG;
  } else {
    str = balmstr_pop(&str_pools.compacts, compactbalmstr_block_alloc,
        BALM_STRING_ALLOCATION_BLOCK_SIZE);
    str->length = len;
  }
  return str;
}

Py_LOCAL_SYMBOL BalmString* New_BalmStringView(char* data, size_t len) {
  BalmString* str = balmstr_pop(&str_pools.bigviews, balmstr_block_alloc,
      BALM_STRING_ALLOCATION_BLOCK_SIZE);
  str->ob_base.ob_type = &BalmStringView_Type;
  str->uc.data.any = data;
  str->uc._base.utf8 = data;
  str->uc._base.utf8_length = len;
  str->length = len;
  str->state.balm = BALM_STRING_VIEW;
  return str;
}

Py_LOCAL_SYMBOL char* GetData_BalmString(BalmString* str) {
  if(str->state.balm == BALM_STRING_COMPACT)
    return str->data;
  return str->uc.data.any;
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

static void init_strpool(StrPool* pool, BalmStringNode* (*alloc)(size_t)) {
  mtx_init(&pool->lock, mtx_plain);
  pool->head = alloc(BALM_STRING_ALLOCATION_BLOCK_SIZE);
}

Py_LOCAL_SYMBOL void balm_init() {
  BalmStringBig_Type = PyUnicode_Type;
  BalmStringBig_Type.tp_new = 0;
  BalmStringBig_Type.tp_free = 0;
  BalmStringBig_Type.tp_dealloc = balmstrbig_dealloc;

  BalmStringCompact_Type = BalmStringBig_Type;
  BalmStringCompact_Type.tp_dealloc = balmstrcompact_dealloc;

  BalmStringView_Type = BalmStringBig_Type;
  BalmStringView_Type.tp_dealloc = balmstrview_dealloc;

  BalmDict_Type = PyDict_Type;
  BalmDict_Type.tp_new = 0;
  BalmDict_Type.tp_free = 0;
  BalmDict_Type.tp_dealloc = balmdict_dealloc;

  init_strpool(&str_pools.bigviews, balmstr_block_alloc);
  init_strpool(&str_pools.compacts, compactbalmstr_block_alloc);
}
