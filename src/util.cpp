#include <charconv>
#include <stddef.h>

extern "C" char* velocem_itoa(size_t val, char* dst, char* last) {
  return std::to_chars(dst, last, val).ptr;
}
