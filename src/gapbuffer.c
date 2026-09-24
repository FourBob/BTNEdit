#include "gapbuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void btn_oom(size_t n) {
    fprintf(stderr, "BTNEdit: Speicher erschoepft (%zu Bytes angefordert)\n", n);
    abort();
}

void *btn_xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        btn_oom(n);
    }
    return p;
}

void *btn_xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        btn_oom(n);
    }
    return q;
}

size_t btn_xmul(size_t n, size_t size) {
    if (size != 0 && n > (size_t)-1 / size) {
        btn_oom((size_t)-1);
    }
    return n * size;
}

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
    size_t new_capacity = gb->capacity ? btn_xmul(gb->capacity, 2) : 16;
    while (new_capacity < needed) {
        new_capacity = btn_xmul(new_capacity, 2);
    }

    char *new_data = btn_xmalloc(new_capacity);
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
    gb->data = btn_xmalloc(initial_capacity);
    gb->capacity = initial_capacity;
    gb->gap_start = 0;
    gb->gap_end = initial_capacity;
}

void gb_free(GapBuffer *gb) {
    free(gb->data);
    gb->data = NULL;
    gb->capacity = gb->gap_start = gb->gap_end = 0;
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


/* Hoechstens zwei memcpy (Teil vor und Teil hinter der Luecke) statt
 * Byte fuer Byte - editor_copy_all() kopiert so das ganze Dokument. */
char *gb_copy_range(const GapBuffer *gb, size_t start, size_t len) {
    char *out = btn_xmalloc(len + 1);
    size_t before_gap = 0;
    if (start < gb->gap_start) {
        size_t end = start + len;
        before_gap = (end <= gb->gap_start ? end : gb->gap_start) - start;
        memcpy(out, gb->data + start, before_gap);
    }
    if (before_gap < len) {
        size_t gap = gb->gap_end - gb->gap_start;
        memcpy(out + before_gap, gb->data + start + before_gap + gap, len - before_gap);
    }
    out[len] = '\0';
    return out;
}
