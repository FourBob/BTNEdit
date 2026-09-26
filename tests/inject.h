#include <stddef.h>
void *inj_malloc(size_t n);
void *inj_realloc(void *p, size_t n);
#define malloc(n) inj_malloc(n)
#define realloc(p, n) inj_realloc(p, n)
