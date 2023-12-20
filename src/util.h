#ifndef VELOCEM_UTIL_H
#define VELOCEM_UTIL_H

#include <stddef.h>
#include <string.h>

#define STRSZ(str) (sizeof(str) - 1)
#define STRCPY(dst, str) memcpy(dst, str, STRSZ(str))

#define GET_STRUCT_FROM_FIELD(pointer, s_type, field)                          \
  ((s_type*) (((char*) pointer) - offsetof(s_type, field)))

#define GET_NODE_PYOBJ(pointer)                                                \
  GET_STRUCT_FROM_FIELD(pointer, BalmStringNode, str)

char* velocem_itoa(size_t val, char* dst, char* last);

#endif // VELOCEM_UTIL_H
