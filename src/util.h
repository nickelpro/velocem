#ifndef VELOCEM_UTIL_H
#define VELOCEM_UTIL_H

#include <stddef.h>

#define STRSZ(str) (sizeof(str) - 1)

#define GET_STRUCT_FROM_FIELD(pointer, s_type, field)                          \
  ((s_type*) (((char*) pointer) - offsetof(s_type, field)))

#define GET_NODE_PYOBJ(pointer)                                                \
  GET_STRUCT_FROM_FIELD(pointer, BalmStringNode, str)

#endif // VELOCEM_UTIL_H
