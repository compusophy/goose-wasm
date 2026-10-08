/* Minimal freestanding <stdlib.h> for wasm32-unknown-unknown. Goose programs
   never allocate on a heap; the generated code only callocs its stack block
   once at startup, so a bump allocator over memory.grow is all this needs. */
#pragma once
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t count, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);
_Noreturn void exit(int code);
_Noreturn void abort(void);
