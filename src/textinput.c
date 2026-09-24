#include "textinput.h"
#include "gapbuffer.h" /* btn_xmalloc */

#include <stdlib.h>
#include <string.h>

/* 64 KB Text reichen macOS zur Orientierung ueber eine Selektion. */
#define BTN_TI_SEL_CAP (64 * 1024)
#define BTN_TI_SUBSTRING_CAP 4096

static size_t units_for(size_t char_len) {
    return char_len == 4 ? 2 : 1;
}

size_t btn_ti_utf16_len(const char *s, size_t len) {
    size_t n = 0, i = 0;
    while (i < len) {
        size_t c = btn_utf8_char_len((const unsigned char *)s + i, len - i);
        n += units_for(c);
        i += c;
    }
    return n;
}

size_t btn_ti_utf16_to_bytes(const char *s, size_t len, size_t u16) {
    size_t n = 0, i = 0;
    while (i < len && n < u16) {
        size_t c = btn_utf8_char_len((const unsigned char *)s + i, len - i);
        n += units_for(c);
        i += c;
    }
    return i;
}

void btn_marked_set(BtnMarkedText *m, const char *utf8, size_t len, size_t sel_u16_loc, size_t sel_u16_len) {
    char *copy = btn_xmalloc(len + 1);
    memcpy(copy, utf8, len);
    copy[len] = '\0';
    free(m->text);
    m->text = copy;
    m->len = len;
    m->sel_start = btn_ti_utf16_to_bytes(copy, len, sel_u16_loc);
    m->sel_end = btn_ti_utf16_to_bytes(copy, len, sel_u16_loc + sel_u16_len);
}

void btn_marked_clear(BtnMarkedText *m) {
    free(m->text);
    m->text = NULL;
    m->len = 0;
    m->sel_start = 0;
    m->sel_end = 0;
}

size_t btn_ti_origin(Editor *ed) {
    size_t p = editor_selection_start(ed);
    for (size_t chars = 0; chars < BTN_TI_WINDOW && p > 0 && gb_char_at(&ed->buffer, p - 1) != '\n'; chars++) {
        p = editor_utf8_seq_start(ed, p - 1);
    }
    return p;
}

/* UTF-16-Einheiten in [from, to) */
static size_t units_between(Editor *ed, size_t from, size_t to) {
    size_t n = 0;
    while (from < to) {
        size_t c = editor_char_len(ed, from, to);
        n += units_for(c);
        from += c;
    }
    return n;
}

void btn_ti_selection(Editor *ed, size_t *loc_u16, size_t *len_u16) {
    size_t origin = btn_ti_origin(ed);
    size_t s = editor_selection_start(ed), e = editor_selection_end(ed);
    if (e - s > BTN_TI_SEL_CAP) {
        e = editor_utf8_seq_start(ed, s + BTN_TI_SEL_CAP);
    }
    *loc_u16 = units_between(ed, origin, s);
    *len_u16 = units_between(ed, s, e);
}

/* Vom Byte-Offset *i (bei *units Einheiten ab Ursprung) weiter, bis
 * mindestens target Einheiten erreicht sind; 0, wenn das Dokumentende
 * vorher kommt. Liegt target mitten in einem Surrogatpaar, zaehlt das ganze
 * Zeichen. Anfang und Ende eines Bereichs werden so beide vom Ursprung aus
 * gezaehlt - ein aufgerundeter Anfang verschiebt das Ende nicht. */
static int advance_to(Editor *ed, size_t *i, size_t *units, size_t target) {
    size_t total = editor_length(ed);
    while (*units < target) {
        if (*i >= total) {
            return 0;
        }
        size_t c = editor_char_len(ed, *i, total);
        *units += units_for(c);
        *i += c;
    }
    return 1;
}

int btn_ti_range_to_bytes(Editor *ed, size_t loc, size_t len, size_t *start, size_t *end) {
    size_t i = btn_ti_origin(ed), units = 0;
    if (!advance_to(ed, &i, &units, loc)) {
        return 0;
    }
    *start = i;
    if (!advance_to(ed, &i, &units, loc + len)) {
        return 0;
    }
    *end = i;
    return 1;
}

uint16_t *btn_ti_substring(Editor *ed, size_t loc, size_t len, size_t *actual_loc, size_t *n) {
    size_t total = editor_length(ed);
    size_t i = btn_ti_origin(ed);
    size_t at = 0;
    while (at < loc) {
        if (i >= total) {
            return NULL;
        }
        size_t c = editor_char_len(ed, i, total);
        at += units_for(c);
        i += c;
    }
    if (len > BTN_TI_SUBSTRING_CAP) {
        len = BTN_TI_SUBSTRING_CAP;
    }
    uint16_t *out = btn_xmalloc((len + 2) * sizeof(uint16_t));
    size_t o = 0;
    while (o < len && i < total) {
        unsigned char b[4];
        size_t c = editor_char_len(ed, i, total);
        for (size_t k = 0; k < c; k++) {
            b[k] = (unsigned char)gb_char_at(&ed->buffer, i + k);
        }
        unsigned long cp;
        if (c == 1) {
            cp = b[0]; /* ASCII oder einzelnes Byte als Latin-1 */
        } else if (c == 2) {
            cp = ((unsigned long)(b[0] & 0x1F) << 6) | (b[1] & 0x3F);
        } else if (c == 3) {
            cp = ((unsigned long)(b[0] & 0x0F) << 12) | ((unsigned long)(b[1] & 0x3F) << 6) | (b[2] & 0x3F);
        } else {
            cp = ((unsigned long)(b[0] & 0x07) << 18) | ((unsigned long)(b[1] & 0x3F) << 12) |
                 ((unsigned long)(b[2] & 0x3F) << 6) | (b[3] & 0x3F);
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out[o++] = (uint16_t)(0xD800 + (cp >> 10));
            out[o++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
        } else {
            out[o++] = (uint16_t)cp;
        }
        i += c;
    }
    *actual_loc = at;
    *n = o;
    return out;
}
