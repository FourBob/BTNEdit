#include "gapbuffer.h"

#include <stdlib.h>
#include <string.h>

static void gb_move_gap(GapBuffer *gb, size_t pos) {
    size_t len = gb_length(gb);
    if (pos > len) {
        pos = len;
    }
    if (pos < gb->gap_start) {
        size_t n = gb->gap_start - pos;
        memmove(gb->data + gb->gap_end - n, gb->data + pos, n);
        gb->gap_start -= n;
        gb->gap_end -= n;
    } else if (pos > gb->gap_start) {
        size_t n = pos - gb->gap_start;
        memmove(gb->data + gb->gap_start, gb->data + gb->gap_end, n);
        gb->gap_start += n;
        gb->gap_end += n;
    }
}

static void gb_grow(GapBuffer *gb, size_t min_extra) {
    size_t gap_len = gb->gap_end - gb->gap_start;
    if (gap_len >= min_extra) {
        return;
    }

    size_t tail_len = gb->capacity - gb->gap_end;
    size_t needed = gb->capacity - gap_len + min_extra;
    size_t new_capacity = gb->capacity ? gb->capacity * 2 : 16;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    char *new_data = malloc(new_capacity);
    memcpy(new_data, gb->data, gb->gap_start);
    memcpy(new_data + new_capacity - tail_len, gb->data + gb->gap_end, tail_len);

    free(gb->data);
    gb->data = new_data;
    gb->gap_end = new_capacity - tail_len;
    gb->capacity = new_capacity;
}

void gb_init(GapBuffer *gb, size_t initial_capacity) {
    if (initial_capacity < 16) {
        initial_capacity = 16;
    }
    gb->data = malloc(initial_capacity);
    gb->capacity = initial_capacity;
    gb->gap_start = 0;
    gb->gap_end = initial_capacity;
}

void gb_free(GapBuffer *gb) {
    free(gb->data);
    gb->data = NULL;
    gb->capacity = gb->gap_start = gb->gap_end = 0;
}

size_t gb_length(const GapBuffer *gb) {
    return gb->capacity - (gb->gap_end - gb->gap_start);
}

void gb_insert(GapBuffer *gb, size_t pos, const char *text, size_t len) {
    if (len == 0) {
        return;
    }
    gb_move_gap(gb, pos);
    gb_grow(gb, len);
    memcpy(gb->data + gb->gap_start, text, len);
    gb->gap_start += len;
}

void gb_delete(GapBuffer *gb, size_t pos, size_t len) {
    size_t total = gb_length(gb);
    if (pos > total) {
        pos = total;
    }
    if (pos + len > total) {
        len = total - pos;
    }
    gb_move_gap(gb, pos);
    gb->gap_end += len;
}

char gb_char_at(const GapBuffer *gb, size_t pos) {
    if (pos < gb->gap_start) {
        return gb->data[pos];
    }
    return gb->data[pos + (gb->gap_end - gb->gap_start)];
}

char *gb_copy_range(const GapBuffer *gb, size_t start, size_t len) {
    char *out = malloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        out[i] = gb_char_at(gb, start + i);
    }
    out[len] = '\0';
    return out;
}
