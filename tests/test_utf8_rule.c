/* Die eine Zeichenregel (btn_utf8_char_len) fuer Cursor, Umbruch, Spalten,
 * Undo und Anzeige - erschoepfend und per Zufall. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "editor.h"
#include "highlight.h"

typedef uint16_t UniChar;
typedef struct {
    size_t start;
    size_t len;
    size_t logical_line;
    int is_continuation;
} BtnRow;
static long chars_per_row_for(double w) { long n = (long)w; return n > 0 ? n : 1; }

#include "render_pure_extracted.h"      /* layout_build, row lookup, ... */
#include "render_decode_extracted.h"    /* decode_row_for_display */

static long failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { if (failures < 25) { printf("FAIL: " __VA_ARGS__); printf("\n"); } failures++; } } while (0)

static unsigned long rng = 777;
static unsigned rnd(unsigned n) { rng = rng * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)((rng >> 33) % n); }

/* Unabhaengige Referenz: dekodieren, dann Ueberlaenge/Surrogate/Bereich pruefen. */
static size_t ref_char_len(const unsigned char *s, size_t avail) {
    unsigned char b = s[0];
    size_t n;
    unsigned long cp;
    if (b < 0x80) return 1;
    else if ((b & 0xE0) == 0xC0) { n = 2; cp = b & 0x1F; }
    else if ((b & 0xF0) == 0xE0) { n = 3; cp = b & 0x0F; }
    else if ((b & 0xF8) == 0xF0) { n = 4; cp = b & 0x07; }
    else return 1;
    if (avail < n) return 1;
    for (size_t k = 1; k < n; k++) {
        if ((s[k] & 0xC0) != 0x80) return 1;
        cp = (cp << 6) | (s[k] & 0x3F);
    }
    static const unsigned long min_cp[5] = { 0, 0, 0x80, 0x800, 0x10000 };
    if (cp < min_cp[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 1;
    return n;
}

static void test_char_len_exhaustive(void) {
    static const unsigned char probe[] = { 0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC2, 0xE0, 0xF4, 0xFF };
    unsigned char s[4];
    for (int b0 = 0; b0 < 256; b0++) {
        for (size_t i1 = 0; i1 < sizeof(probe); i1++) {
            for (size_t i2 = 0; i2 < sizeof(probe); i2++) {
                for (size_t i3 = 0; i3 < sizeof(probe); i3++) {
                    s[0] = (unsigned char)b0; s[1] = probe[i1]; s[2] = probe[i2]; s[3] = probe[i3];
                    for (size_t avail = 1; avail <= 4; avail++) {
                        size_t got = btn_utf8_char_len(s, avail), want = ref_char_len(s, avail);
                        CHECK(got == want, "char_len %02X %02X %02X %02X avail %zu: got %zu want %zu",
                              s[0], s[1], s[2], s[3], avail, got, want);
                    }
                }
            }
        }
    }
}

static const char *PIECES[] = {
    "a", "b", " ", "\t", "\xC3\xA4", "\xE2\x82\xAC", "\xF0\x9F\x98\x80",   /* gueltig */
    "\xE4", "\xB0", "\xFC", "\xA9",                                         /* Latin-1 */
    "\xC3", "\xE2\x82", "\xF0\x9F\x98",                                     /* abgeschnitten */
    "\x80\x80", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xF5\x80",  /* ueberlang/Surrogat/zu gross */
    "word", "xy\t",
};

/* Vorwaerts-Zerlegung in Zeichen - Referenz fuer alle Konsistenz-Eigenschaften. */
static size_t boundaries(const unsigned char *s, size_t len, size_t *out) {
    size_t n = 0, i = 0;
    out[n++] = 0;
    while (i < len) { i += ref_char_len(s + i, len - i); out[n++] = i; }
    return n;
}

static void test_segmentation_and_columns(void) {
    for (int doc = 0; doc < 3000; doc++) {
        unsigned char buf[400];
        size_t len = 0;
        int pieces = 1 + (int)rnd(40);
        for (int p = 0; p < pieces; p++) {
            const char *piece = PIECES[rnd(sizeof(PIECES) / sizeof(PIECES[0]))];
            size_t pl = strlen(piece);
            if (len + pl >= sizeof(buf)) break;
            memcpy(buf + len, piece, pl);
            len += pl;
        }
        Editor ed;
        editor_init(&ed);
        editor_insert_text(&ed, (const char *)buf, len);
        size_t b[401];
        size_t nb = boundaries(buf, len, b);

        /* 1) Pfeil rechts von 0 besucht genau die Grenzen; Pfeil links von len genau rueckwaerts */
        editor_set_cursor(&ed, 0, 0);
        for (size_t k = 1; k < nb; k++) {
            editor_move(&ed, BTN_MOVE_RIGHT, 0);
            CHECK(ed.cursor == b[k], "doc %d RIGHT step %zu: %zu want %zu", doc, k, ed.cursor, b[k]);
        }
        editor_set_cursor(&ed, len, 0);
        for (size_t k = nb - 1; k > 0; k--) {
            editor_move(&ed, BTN_MOVE_LEFT, 0);
            CHECK(ed.cursor == b[k - 1], "doc %d LEFT step to %zu: got %zu", doc, b[k - 1], ed.cursor);
        }
        /* 2) seq_start(pos) == letzte Grenze <= pos */
        size_t bi = 0;
        for (size_t pos = 0; pos < len; pos++) {
            while (bi + 1 < nb && b[bi + 1] <= pos) bi++;
            CHECK(editor_utf8_seq_start(&ed, pos) == b[bi], "doc %d seq_start(%zu)=%zu want %zu",
                  doc, pos, editor_utf8_seq_start(&ed, pos), b[bi]);
        }
        /* 3) Spalte <-> Offset Hin und zurueck konsistent, auch mit Tabs */
        for (size_t k = 0; k < nb; k++) {
            size_t col = editor_visual_column_in_range(&ed, 0, b[k]);
            size_t back = editor_offset_for_column_in_range(&ed, 0, len, col);
            CHECK(back == b[k], "doc %d col(%zu)=%zu -> offset %zu", doc, b[k], col, back);
        }
        /* 4) KERN: gezeichnete Glyph-Position == Cursor-Spalte fuer jede Zeichengrenze */
        UniChar *u16 = malloc((len * 4 + 2) * sizeof(UniChar));
        size_t *map = malloc((len + 1) * sizeof(size_t));
        size_t n16 = decode_row_for_display(buf, len, u16, map);
        for (size_t k = 0; k < nb; k++) {
            size_t glyph = 0;   /* Glyphen vor map[b[k]], Surrogatpaar = 1 Glyph */
            for (size_t u = 0; u < map[b[k]]; u++) {
                if (!(u16[u] >= 0xDC00 && u16[u] <= 0xDFFF)) glyph++;
            }
            size_t col = editor_visual_column_in_range(&ed, 0, b[k]);
            CHECK(glyph == col, "doc %d boundary %zu: glyph pos %zu vs cursor col %zu", doc, b[k], glyph, col);
        }
        CHECK(map[len] == n16, "doc %d map[len]", doc);
        /* 5) Latin-1-Bytes werden als U+00XX gezeichnet */
        for (size_t k = 0; k + 1 < nb; k++) {
            if (b[k + 1] - b[k] == 1 && buf[b[k]] >= 0x80) {
                CHECK(u16[map[b[k]]] == buf[b[k]], "doc %d latin1 byte %02X drawn as %04X", doc, buf[b[k]], u16[map[b[k]]]);
            }
        }
        free(u16);
        free(map);

        /* 6) Umbruch: Rows beginnen auf Zeichengrenzen und passen in chars_per_row */
        size_t nrows, words, nchars;
        long cpr = 1 + (long)rnd(12);
        BtnRow *rows = layout_build(&ed, cpr, &nrows, &words, &nchars);
        CHECK(nchars == nb - 1, "doc %d char count %zu vs %zu", doc, nchars, nb - 1);
        for (size_t r = 0; r < nrows; r++) {
            CHECK(editor_utf8_seq_start(&ed, rows[r].start) == rows[r].start || rows[r].start == len,
                  "doc %d row %zu starts mid-char at %zu", doc, r, rows[r].start);
            size_t w = editor_visual_column_in_range(&ed, rows[r].start, rows[r].start + rows[r].len);
            size_t chars = 0;
            for (size_t k = 0; k < nb; k++) if (b[k] > rows[r].start && b[k] <= rows[r].start + rows[r].len) chars++;
            CHECK(w <= (size_t)cpr || chars <= 1, "doc %d row %zu width %zu > cpr %ld", doc, r, w, cpr);
        }
        CHECK(words == editor_word_count(&ed), "doc %d words %zu vs %zu", doc, words, editor_word_count(&ed));
        free(rows);
        editor_free(&ed);
    }
}

static void test_specific(void) {
    Editor ed;
    /* Entf auf Latin-1 "ae" loescht NUR dieses Byte (vorher: + Zeilenumbruch + X) */
    editor_init(&ed);
    editor_insert_text(&ed, "\xE4\nXY", 4);
    editor_set_cursor(&ed, 0, 0);
    editor_delete_forward(&ed);
    size_t len;
    char *t = editor_copy_all(&ed, &len);
    CHECK(len == 3 && memcmp(t, "\nXY", 3) == 0, "forward delete on Latin-1 byte removed %zu bytes", 4 - len);
    free(t);
    editor_free(&ed);

    /* Klick/Hoch/Runter: "B E4 r" col 2 -> offset 2 (vorher 3) */
    editor_init(&ed);
    editor_insert_text(&ed, "B\xE4r", 3);
    CHECK(editor_offset_for_column_in_range(&ed, 0, 3, 2) == 2, "latin1 col 2 -> offset %zu",
          editor_offset_for_column_in_range(&ed, 0, 3, 2));
    editor_free(&ed);

    /* Grad-Zeichen (0xB0, Latin-1) zaehlt als Spalte (vorher 0) */
    editor_init(&ed);
    editor_insert_text(&ed, "a\xB0" "b", 3);
    CHECK(editor_visual_column_in_range(&ed, 0, 3) == 3, "a°b columns %zu", editor_visual_column_in_range(&ed, 0, 3));
    editor_free(&ed);

    /* Tab nach Umlaut: "ae\tx" -> x bei Spalte 4, gezeichnet an Einheit 4 */
    unsigned char row[] = { 0xC3, 0xA4, '\t', 'x' };
    UniChar u16[16];
    size_t map[8];
    size_t n = decode_row_for_display(row, 4, u16, map);
    CHECK(n == 5 && u16[0] == 0xE4 && u16[1] == ' ' && u16[3] == ' ' && u16[4] == 'x' && map[3] == 4,
          "decode ae\\tx: n=%zu map[3]=%zu", n, map[3]);
    editor_init(&ed);
    editor_insert_text(&ed, (const char *)row, 4);
    CHECK(editor_visual_column_in_range(&ed, 0, 3) == 4, "cursor col of x = %zu", editor_visual_column_in_range(&ed, 0, 3));
    editor_free(&ed);

    /* Emoji -> Surrogatpaar */
    unsigned char emo[] = { 0xF0, 0x9F, 0x98, 0x80, 'z' };
    n = decode_row_for_display(emo, 5, u16, map);
    CHECK(n == 3 && u16[0] == 0xD83D && u16[1] == 0xDE00 && u16[2] == 'z' && map[4] == 2, "emoji surrogates");

    /* Undo: a, ae, b getippt -> EIN Undo-Schritt */
    editor_init(&ed);
    editor_insert_text(&ed, "a", 1);
    editor_insert_text(&ed, "\xC3\xA4", 2);
    editor_insert_text(&ed, "b", 1);
    CHECK(ed.undo.count == 1, "typing a,ae,b -> %zu undo records", ed.undo.count);
    editor_undo(&ed);
    CHECK(editor_length(&ed) == 0, "one undo removes all typed text (len %zu)", editor_length(&ed));
    /* Backspace-Kette ueber "aaeb" -> EIN Record */
    editor_insert_text(&ed, "x", 1);  /* neuer Stand */
    editor_free(&ed);
    editor_init(&ed);
    editor_insert_text(&ed, "a\xC3\xA4" "b", 4);
    ed.suppress_coalesce = 1;
    editor_delete_backward(&ed);
    editor_delete_backward(&ed);
    editor_delete_backward(&ed);
    CHECK(ed.undo.count == 2, "insert + backspace chain over a,ae,b -> %zu records (want 2)", ed.undo.count);
    editor_undo(&ed);
    t = editor_copy_all(&ed, &len);
    CHECK(len == 4 && memcmp(t, "a\xC3\xA4" "b", 4) == 0, "undo restores a ae b");
    free(t);
    /* Paste mehrerer Zeichen wird weiterhin NICHT an vorherige Eingabe angehaengt */
    editor_free(&ed);
    editor_init(&ed);
    editor_insert_text(&ed, "a", 1);
    editor_insert_text(&ed, "bc", 2);
    CHECK(ed.undo.count == 2, "multi-char insert not coalesced (%zu)", ed.undo.count);
    editor_free(&ed);
}

/* Ende-Taste: dieselbe Rechnung wie main.c's move_row_edge(to_end). */
static size_t end_key(Editor *ed, const BtnRow *rows, size_t row_count) {
    size_t cur_row = btn_layout_row_for_offset(rows, row_count, ed->cursor);
    size_t new_offset = rows[cur_row].start + rows[cur_row].len;
    if (cur_row + 1 < row_count && rows[cur_row + 1].is_continuation && rows[cur_row].len > 0) {
        new_offset = editor_utf8_seq_start(ed, new_offset - 1);
    }
    return new_offset;
}

static void test_end_key(void) {
    struct { const char *text; long cpr; size_t cursor; size_t want; } cases[] = {
        { "hello world foo", 12, 3, 11 },          /* weicher Umbruch: hinter "world", vor dem Leerzeichen */
        { "hello world foo", 12, 13, 15 },         /* letzte Row: echtes Zeilenende */
        { "abcdefghijklmnop", 5, 1, 4 },           /* erzwungener Umbruch: bleibt in der Row */
        { "ab\xC3\xA4" "cdefgh", 3, 0, 2 },        /* Row "abae": vor dem mehrbytigen Zeichen, nicht mittendrin */
        { "short\nnext", 20, 1, 5 },               /* kein Umbruch: Zeilenende */
    };
    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        Editor ed;
        editor_init(&ed);
        editor_insert_text(&ed, cases[k].text, strlen(cases[k].text));
        size_t nrows, words;
        BtnRow *rows = layout_build(&ed, cases[k].cpr, &nrows, &words, NULL);
        ed.cursor = cases[k].cursor;
        size_t got = end_key(&ed, rows, nrows);
        size_t before = btn_layout_row_for_offset(rows, nrows, cases[k].cursor);
        size_t after = btn_layout_row_for_offset(rows, nrows, got);
        CHECK(got == cases[k].want, "End case %zu: offset %zu want %zu", k, got, cases[k].want);
        CHECK(after == before, "End case %zu: caret moved from row %zu to row %zu", k, before, after);
        free(rows);
        editor_free(&ed);
    }
}

int main(void) {
    test_char_len_exhaustive();
    test_segmentation_and_columns();
    test_specific();
    test_end_key();
    printf("%ld checks, %ld failures\n%s\n", checks, failures, failures ? "TESTS FAILED" : "ALL TESTS PASSED");
    return failures ? 1 : 0;
}
