/* Benchmark (kein Test): Layout-/Render-Kosten pro Tastendruck bei grossen
 * Dokumenten. Aufruf: make bench. */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "editor.h"
#include "highlight.h"

typedef struct {
    size_t start;
    size_t len;
    size_t logical_line;
    int is_continuation;
} BtnRow;

static long chars_per_row_for(double text_width) {
    long n = (long)text_width;
    return n > 0 ? n : 1;
}

#include "render_pure_extracted.h"

static int old_comment_state_before_line(Editor *ed, const BtnLangSpec *lang, size_t logical_line) {
    int state = 0;
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

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static int failures = 0;
static void check(int cond, const char *label) {
    printf("%s [%s]\n", cond ? "ok  " : "FAIL", label);
    if (!cond) failures++;
}

/* Arbeit eines Frames nach der ALTEN Methode (was vorher pro Tastendruck lief). */
static void old_frame(Editor *ed, const BtnLangSpec *lang, double width, size_t first_line, size_t visible) {
    size_t n;
    BtnRow *r1 = layout_build(ed, chars_per_row_for(width), &n, NULL, NULL); /* sync_scroll_to_cursor */
    free(r1);
    BtnRow *rows = layout_build(ed, chars_per_row_for(width), &n, NULL, NULL); /* btn_render_frame */
    volatile int st = old_comment_state_before_line(ed, lang, first_line);
    (void)st;
    for (size_t l = first_line; l < first_line + visible; l++) {
        size_t s, len;
        editor_line_bounds(ed, l, &s, &len);                                 /* draw_row_line */
    }
    volatile size_t f = editor_offset_to_line(ed, ed->cursor) + editor_visual_column(ed, ed->cursor)
                        + editor_line_count(ed) + editor_word_count(ed);    /* draw_footer */
    (void)f;
    free(rows);
}

/* Dieselbe Arbeit nach der NEUEN Methode. */
static void new_frame(Editor *ed, const BtnLangSpec *lang, double width, size_t first_row, size_t visible) {
    size_t n;
    const BtnRow *rows = btn_layout_get(ed, width, &n);                     /* sync_scroll_to_cursor */
    rows = btn_layout_get(ed, width, &n);                                    /* btn_render_frame (Cache-Treffer) */
    volatile int st = comment_state_before_line(ed, lang, rows, n, rows[first_row].logical_line);
    (void)st;
    for (size_t r = first_row; r < first_row + visible && r < n; r++) {
        size_t s, len;
        line_bounds_from_rows(rows, n, r, &s, &len);
    }
    size_t cur_row = btn_layout_row_for_offset(rows, n, ed->cursor);
    volatile size_t f = rows[cur_row].logical_line
                        + editor_visual_column_in_range(ed, rows[first_row_of_line(rows, cur_row)].start, ed->cursor)
                        + rows[n - 1].logical_line + g_layout.word_count;
    (void)f;
}

int main(void) {
    const BtnLangSpec *lang = btn_highlight_lang_for_path("x.c");

    /* ---- Gezielt: inkrementeller Pfad wird wirklich genutzt ---- */
    Editor ed;
    editor_init(&ed);
    for (int i = 0; i < 2000; i++) {
        const char *line = (i % 7 == 0) ? "/* comment start\n" : (i % 7 == 3) ? "end */ int x;\n" : "int y = 2;\n";
        editor_insert_text(&ed, line, strlen(line));
    }
    size_t n;
    const BtnRow *rows = btn_layout_get(&ed, 80, &n);
    int a = comment_state_before_line(&ed, lang, rows, n, 1999);
    check(a == old_comment_state_before_line(&ed, lang, 1999), "I1 full compute matches reference");
    check(g_cstate.known == 2000, "I2 states for all 2000 lines known");

    size_t s, l;
    editor_line_bounds(&ed, 1990, &s, &l);
    editor_set_cursor(&ed, s + 2, 0);
    editor_insert_text(&ed, "/*", 2);                         /* oeffnet einen Kommentar in Zeile 1990 */
    rows = btn_layout_get(&ed, 80, &n);
    int b = comment_state_before_line(&ed, lang, rows, n, 5);
    check(g_cstate.known == 1991, "I3 edit in line 1990 keeps states 0..1990 (known=1991)");
    check(b == old_comment_state_before_line(&ed, lang, 5), "I4 cached early line correct");
    int c = comment_state_before_line(&ed, lang, rows, n, 1999);
    check(c == old_comment_state_before_line(&ed, lang, 1999), "I5 recomputed tail matches reference after edit");
    check(g_cstate.known == 2000, "I6 tail re-extended");

    editor_undo(&ed);                                          /* Undo: Aenderung ebenfalls ab Zeile 1990 */
    rows = btn_layout_get(&ed, 80, &n);
    int d = comment_state_before_line(&ed, lang, rows, n, 1999);
    check(d == old_comment_state_before_line(&ed, lang, 1999), "I7 after undo matches reference");
    editor_free(&ed);

    /* ---- Benchmark: ~9 MB / 100k Zeilen, Sichtfenster am Ende, Tippen am Ende ---- */
    editor_init(&ed);
    const char *tmpl = "    int value_%06d = compute(alpha, beta) + /* inline */ gamma * delta_%d; // trailing comment xx\n";
    char line[256];
    for (int i = 0; i < 100000; i++) {
        int len = snprintf(line, sizeof(line), tmpl, i, i);
        editor_insert_text(&ed, line, (size_t)len);
    }
    printf("\ndocument: %zu bytes, %zu lines\n", editor_length(&ed), editor_line_count(&ed));
    const double width = 120;
    const size_t visible = 45;

    size_t n_rows;
    BtnRow *probe = layout_build(&ed, chars_per_row_for(width), &n_rows, NULL, NULL);
    size_t first_row = n_rows - visible;
    size_t first_line = probe[first_row].logical_line;
    free(probe);

    const int frames = 5;
    double t0 = now_ms();
    for (int k = 0; k < frames; k++) {
        editor_set_cursor(&ed, editor_length(&ed), 0);
        editor_insert_text(&ed, "x", 1);
        old_frame(&ed, lang, width, first_line, visible);
    }
    double old_ms = (now_ms() - t0) / frames;

    /* Warmlauf fuer den inkrementellen Cache (erster Frame nach dem Oeffnen) */
    size_t nr;
    const BtnRow *wr = btn_layout_get(&ed, width, &nr);
    double tw = now_ms();
    comment_state_before_line(&ed, lang, wr, nr, wr[first_row].logical_line);
    double first_open_ms = now_ms() - tw;

    t0 = now_ms();
    for (int k = 0; k < frames; k++) {
        editor_set_cursor(&ed, editor_length(&ed), 0);
        editor_insert_text(&ed, "x", 1);
        new_frame(&ed, lang, width, first_row, visible);
    }
    double new_ms = (now_ms() - t0) / frames;

    printf("per keystroke near EOF:  old %.1f ms   new %.1f ms   (speedup %.0fx)\n", old_ms, new_ms, old_ms / new_ms);
    printf("first frame after open (one-time comment-state fill): %.1f ms\n", first_open_ms);
    check(new_ms * 5 < old_ms, "B1 new per-keystroke cost at least 5x lower");
    editor_free(&ed);

    printf(failures ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
