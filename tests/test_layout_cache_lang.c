/* Wie test_layout_cache.c, zusaetzlich mit Sprachwechsel zwischen den Edits. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "highlight.h"

/* Minimaler Ersatz fuer render.h (ohne CoreGraphics). */
typedef struct {
    size_t start;
    size_t len;
    size_t logical_line;
    int is_continuation;
} BtnRow;

/* Im Test: text_width IST chars_per_row (render.c misst sonst die Schrift). */
static long chars_per_row_for(double text_width) {
    long n = (long)text_width;
    return n > 0 ? n : 1;
}

#include "render_pure_extracted.h"

/* ---------- Referenz: alte, ungecachte Implementierungen ---------- */

static size_t old_row_for_offset(const BtnRow *rows, size_t row_count, size_t offset) {
    for (size_t i = 0; i + 1 < row_count; i++) {
        if (offset < rows[i + 1].start) {
            return i;
        }
    }
    return row_count > 0 ? row_count - 1 : 0;
}

static int old_comment_state_before_line(Editor *ed, const BtnLangSpec *lang, size_t logical_line) {
    int state = 0;
    if (logical_line == 0) {
        return state;
    }
    size_t total_len = editor_length(ed);
    size_t li = 0, line_start = 0;
    for (size_t i = 0; i <= total_len && li < logical_line; i++) {
        if (i == total_len || gb_char_at(&ed->buffer, i) == '\n') {
            size_t line_len = i - line_start;
            char *text = gb_copy_range(&ed->buffer, line_start, line_len);
            int ends;
            btn_highlight_tokenize(text, line_len, lang, state, &ends, NULL, 0);
            free(text);
            state = ends;
            li++;
            line_start = i + 1;
        }
    }
    return state;
}

/* ---------- Test ---------- */

static long failures = 0, checks = 0;
static unsigned long rng = 12345;
static unsigned rnd(unsigned n) {
    rng = rng * 6364136223846793005UL + 1442695040888963407UL;
    return (unsigned)((rng >> 33) % n);
}

#define FAIL(...) do { if (failures < 20) { printf("FAIL: " __VA_ARGS__); printf("\n"); } failures++; } while (0)

static const char *PIECES[] = {
    "a", "b", " ", "  ", "\t", "\n", "\n\n", "/*", "*/", "//", "\"", "x = 1;", "int ",
    "\xC3\xA4", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "word_1", "averyveryveryverylongwordwithoutbreaks",
    "/* multi\nline\ncomment */", "\"str /* not comment */\"", "#define X 1\n",
};

static void random_edit(Editor *ed) {
    size_t len = editor_length(ed);
    unsigned op = rnd(100);
    if (op < 50 || len == 0) {
        editor_set_cursor(ed, len ? rnd((unsigned)len + 1) : 0, 0);
        /* Cursor auf Zeichengrenze ziehen, wie es die echte App tut */
        ed->cursor = editor_utf8_seq_start(ed, ed->cursor);
        ed->anchor = ed->cursor;
        const char *p = PIECES[rnd(sizeof(PIECES) / sizeof(PIECES[0]))];
        editor_insert_text(ed, p, strlen(p));
    } else if (op < 70) {
        size_t a = rnd((unsigned)len + 1), b = rnd((unsigned)len + 1);
        if (a > b) { size_t t = a; a = b; b = t; }
        a = editor_utf8_seq_start(ed, a);
        b = editor_utf8_seq_start(ed, b);
        editor_set_cursor(ed, a, 0);
        editor_set_cursor(ed, b, 1);
        if (b > a) editor_delete_selection(ed);
    } else if (op < 80) {
        editor_set_cursor(ed, rnd((unsigned)len + 1), 0);
        ed->cursor = editor_utf8_seq_start(ed, ed->cursor);
        ed->anchor = ed->cursor;
        editor_delete_backward(ed);
    } else if (op < 90) {
        editor_undo(ed);
    } else if (op < 98) {
        editor_redo(ed);
    } else {
        const char *t = "fresh\n/* new */\ntext";
        editor_set_text(ed, t, strlen(t));
    }
}

static void verify(Editor *ed, const BtnLangSpec *lang, double width) {
    size_t n_cached, n_fresh, words_fresh;
    const BtnRow *cached = btn_layout_get(ed, width, &n_cached);
    BtnRow *fresh = layout_build(ed, chars_per_row_for(width), &n_fresh, &words_fresh, NULL);

    checks++;
    if (n_cached != n_fresh || memcmp(cached, fresh, n_fresh * sizeof(BtnRow)) != 0) {
        FAIL("layout cache differs from fresh build (rows %zu vs %zu)", n_cached, n_fresh);
    }
    checks++;
    {
        size_t tl; char *t = editor_copy_all(ed, &tl); size_t rc = 0;
        for (size_t k = 0; k < tl; k += btn_utf8_char_len((const unsigned char *)t + k, tl - k)) rc++;
        free(t);
        if (g_layout.char_count != rc) FAIL("char count %zu vs %zu", g_layout.char_count, rc);
    }
    if (g_layout.word_count != editor_word_count(ed)) {
        FAIL("word count %zu vs editor_word_count %zu", g_layout.word_count, editor_word_count(ed));
    }
    checks++;
    if (fresh[n_fresh - 1].logical_line + 1 != editor_line_count(ed)) {
        FAIL("line count from rows %zu vs %zu", fresh[n_fresh - 1].logical_line + 1, editor_line_count(ed));
    }

    size_t len = editor_length(ed);
    for (size_t off = 0; off <= len; off++) {
        checks++;
        size_t a = btn_layout_row_for_offset(fresh, n_fresh, off);
        size_t b = old_row_for_offset(fresh, n_fresh, off);
        if (a != b) {
            FAIL("row_for_offset(%zu): binary %zu vs linear %zu", off, a, b);
            break;
        }
    }

    for (size_t r = 0; r < n_fresh; r++) {
        size_t s1, l1, s2, l2;
        line_bounds_from_rows(fresh, n_fresh, r, &s1, &l1);
        editor_line_bounds(ed, fresh[r].logical_line, &s2, &l2);
        checks++;
        if (s1 != s2 || l1 != l2) {
            FAIL("line bounds row %zu line %zu: rows (%zu,%zu) vs editor (%zu,%zu)",
                 r, fresh[r].logical_line, s1, l1, s2, l2);
            break;
        }
        checks++;
        if (row_of_line_start(fresh, n_fresh, fresh[r].logical_line) != first_row_of_line(fresh, r)) {
            FAIL("row_of_line_start mismatch at row %zu", r);
            break;
        }
    }

    /* Footer: Zeile/Spalte aus Rows vs. alte Vollscans */
    size_t cur = ed->cursor;
    size_t cur_row = btn_layout_row_for_offset(fresh, n_fresh, cur);
    checks++;
    if (fresh[cur_row].logical_line != editor_offset_to_line(ed, cur)) {
        FAIL("footer line %zu vs %zu", fresh[cur_row].logical_line, editor_offset_to_line(ed, cur));
    }
    size_t col = editor_visual_column_in_range(ed, fresh[first_row_of_line(fresh, cur_row)].start, cur);
    checks++;
    if (col != editor_visual_column(ed, cur)) {
        FAIL("footer column %zu vs %zu", col, editor_visual_column(ed, cur));
    }

    /* Inkrementeller Kommentar-Zustand: einige Zielzeilen in zufaelliger
     * Reihenfolge (auch rueckwaerts, wie beim Hochscrollen) */
    if (lang) {
        size_t lines = fresh[n_fresh - 1].logical_line + 1;
        for (int k = 0; k < 4; k++) {
            size_t target = rnd((unsigned)lines);
            int inc = comment_state_before_line(ed, lang, cached, n_cached, target);
            int ref = old_comment_state_before_line(ed, lang, target);
            checks++;
            if (inc != ref) {
                FAIL("comment state before line %zu: incremental %d vs full %d", target, inc, ref);
            }
        }
    }
    free(fresh);
}

int main(void) {
    const BtnLangSpec *c_lang = btn_highlight_lang_for_path("x.c");
    if (!c_lang) {
        printf("no C lang spec\n");
        return 2;
    }

    /* Mehrere Editoren wie Tabs - abwechselnd gezeichnet, dazwischen per
     * memmove verschoben (close_tab), damit Cache-Schluessel-Kollisionen
     * auffallen wuerden. */
    enum { NDOC = 3 };
    Editor docs[NDOC];
    for (int d = 0; d < NDOC; d++) {
        editor_init(&docs[d]);
    }
    const double widths[] = { 5, 12, 40, 200 };

    for (int step = 0; step < 20000; step++) {
        int d = (int)rnd(NDOC);
        random_edit(&docs[d]);
        if (rnd(10) == 0) {
            random_edit(&docs[d]);  /* zwei Edits zwischen zwei Frames */
        }
        double w = widths[rnd(4)];
        const BtnLangSpec *langs3[3] = { c_lang, btn_highlight_lang_for_path("x.py"), btn_highlight_lang_for_path("x.html") }; const BtnLangSpec *lang = rnd(8) == 0 ? NULL : langs3[rnd(3)];
        verify(&docs[d], lang, w);

        if (rnd(200) == 0) {
            /* close_tab-Simulation: doc 0 freigeben, 1 und 2 nach vorn schieben, neu anlegen */
            editor_free(&docs[0]);
            memmove(&docs[0], &docs[1], (NDOC - 1) * sizeof(Editor));
            editor_init(&docs[NDOC - 1]);
        }
    }
    for (int d = 0; d < NDOC; d++) {
        editor_free(&docs[d]);
    }

    printf("%ld checks, %ld failures\n", checks, failures);
    printf(failures ? "\nTESTS FAILED\n" : "\nALL TESTS PASSED\n");
    return failures ? 1 : 0;
}
