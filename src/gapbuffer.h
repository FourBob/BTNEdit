#ifndef BTN_GAPBUFFER_H
#define BTN_GAPBUFFER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *data;
    size_t capacity;
    size_t gap_start;
    size_t gap_end;
} GapBuffer;

void gb_init(GapBuffer *gb, size_t initial_capacity);
void gb_free(GapBuffer *gb);
size_t gb_length(const GapBuffer *gb);
void gb_insert(GapBuffer *gb, size_t pos, const char *text, size_t len);
void gb_delete(GapBuffer *gb, size_t pos, size_t len);
char gb_char_at(const GapBuffer *gb, size_t pos);

/* Gibt einen neu allokierten, NUL-terminierten Ausschnitt zurueck (caller muss free() aufrufen). */
char *gb_copy_range(const GapBuffer *gb, size_t start, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BTN_GAPBUFFER_H */
