#ifndef VELOCEM_GIL_BALM_H
#define VELOCEM_GIL_BALM_H

#include <stddef.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define BALM_STRING_ALLOCATION_BLOCK_SIZE (1 << 8)
#define BALM_DICT_ALLOCATION_BLOCK_SIZE (1 << 8)
#define BALM_RD_SIZE (1 << 16)

typedef struct RefCountedData {
  struct RefCountedData* next;
  size_t ref_count;
  size_t len;
  size_t used;
  char data[];
} RefCountedData;

typedef union {
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
        unsigned int : 5;
        // Balm state data, lives in unused padding bytes
        unsigned int balm : 3;
        unsigned int balm_offset : 16;
      } state;
    };
    char data[];
  };
  struct {
    PyUnicodeObject uc;
    RefCountedData* rd;
  };
} BalmString;

#define BALM_COMPACT_ALLOCATION 128
#define BALM_COMPACT_MAX_STR (BALM_COMPACT_ALLOCATION - sizeof(PyASCIIObject))

enum BalmStringType {
  BALM_STRING_VIEW,
  BALM_STRING_COMPACT,
  BALM_STRING_BIG,
  BALM_STRING_VIEW_BLOCK,
  BALM_STRING_COMPACT_BLOCK,
};

typedef struct BalmStringNode {
  struct BalmStringNode* next;
  BalmString str;
} BalmStringNode;

typedef PyTupleObject BalmTuple;

typedef struct BalmTupleNode {
  struct BalmTupleNode* next;
  BalmTuple tpl;
} BalmTupleNode;

typedef struct {
  size_t ref_count;
  BalmString strs[];
} BalmStrBlock;

typedef struct BalmBlockNode {
  struct BalmBlockNode* next;
  BalmStrBlock blk;
} BalmBlockNode;

typedef struct {
  size_t ref_count;
  char strs[];
} BalmCompactStrBlock;

typedef struct BalmCompactBlockNode {
  struct BalmCompactBlockNode* next;
  BalmCompactStrBlock blk;
} BalmCompactBlockNode;

Py_LOCAL_SYMBOL void balm_init();

Py_LOCAL_SYMBOL BalmString* New_BalmString(size_t len);
Py_LOCAL_SYMBOL BalmString* New_BalmStringView(RefCountedData* rd, char* data,
    size_t len);

Py_LOCAL_SYMBOL char* GetData_BalmString(BalmString* str);

Py_LOCAL_SYMBOL BalmTuple* New_BalmTuple(size_t len);

Py_LOCAL_SYMBOL BalmStrBlock* New_BalmStringBlock();
Py_LOCAL_SYMBOL BalmString* Get_BalmStringFromBlock(BalmStrBlock* blk,
    size_t index, RefCountedData* rd, char* data, size_t len);

Py_LOCAL_SYMBOL BalmCompactStrBlock* New_BalmCompactStringBlock();
Py_LOCAL_SYMBOL BalmString* Get_BalmCompactStringFromBlock(
    BalmCompactStrBlock* blk, size_t index, size_t len);

Py_LOCAL_SYMBOL RefCountedData* New_RefData();

#define BALM_TUPLE_MAX_SIZE (1 << 13)
#define BALM_TUPLE_ALLOCATION_BLOCK_SIZE (1 << 8)

#define GET_STRUCT_FROM_FIELD(pointer, s_type, field)                          \
  ((s_type*) (((char*) pointer) - offsetof(s_type, field)))

#define GET_STRNODE_PYOBJ(pointer)                                             \
  GET_STRUCT_FROM_FIELD(pointer, BalmStringNode, str)

#define GET_TPLNODE_PYOBJ(pointer)                                             \
  GET_STRUCT_FROM_FIELD(pointer, BalmTupleNode, tpl)

#define GET_BLK_STRS(pointer) GET_STRUCT_FROM_FIELD(pointer, BalmStrBlock, strs)
#define GET_BLKNODE_BLK(pointer)                                               \
  GET_STRUCT_FROM_FIELD(pointer, BalmBlockNode, blk)

#define GET_COMPBLK_STRS(pointer)                                              \
  GET_STRUCT_FROM_FIELD(pointer, BalmCompactStrBlock, strs)
#define GET_COMPBLKNODE_BLK(pointer)                                           \
  GET_STRUCT_FROM_FIELD(pointer, BalmCompactBlockNode, blk)


typedef PyDictObject BalmDict;

typedef struct BalmDictNode {
  BalmDict dict;
  struct BalmDictNode* next;
} BalmDictNode;

#endif // VELOCEM_GIL_BALM_H
