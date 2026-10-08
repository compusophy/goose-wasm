/* Minimal freestanding <string.h> for wasm32-unknown-unknown. The runtime
   (goose_wasm_rt.c) defines these; with bulk-memory enabled clang lowers most
   memcpy/memset calls straight to memory.copy / memory.fill. */
#pragma once
#include <stddef.h>
void *memcpy(void *restrict dst, const void *restrict src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
