/* Einruecken: Auto-Indent bei Return, Tab/Shift+Tab ueber mehrere Zeilen,
 * Tab vs. Leerzeichen je nach Datei, Undo als ein Schritt, Selektion bleibt
 * auf ihrem Text. Fuzz: Ausruecken nach Einruecken ergibt das Original. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 25) { printf("FAIL: " __VA_ARGS__); printf("\n"); } fails++; } } while (0)

static unsigned long rng = 31337;
static unsigned rnd(unsigned n) { rng = rng * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)((rng >> 33) % n); }

static char *text_of(Editor *ed, size_t *len) { return editor_copy_all(ed, len); }
static int eq(Editor *ed, const char *s) {
    size_t l;
    char *t = text_of(ed, &l);
    int r = l == strlen(s) && memcmp(t, s, l) == 0;
    if (!r) printf("   got: \"%.*s\"\n  want: \"%s\"\n", (int)l, t, s);
    free(t);
    return r;
}
static void set(Editor *ed, const char *s) { editor_set_text(ed, s, strlen(s)); }
static void sel(Editor *ed, size_t anchor, size_t cursor) { editor_set_cursor(ed, anchor, 0); editor_set_cursor(ed, cursor, 1); }
static void type(Editor *ed, const char *s) { editor_insert_text(ed, s, strlen(s)); }

static void test_newline(void) {
    Editor ed;
    editor_init(&ed);

    set(&ed, "    foo");
    sel(&ed, 7, 7);
    editor_insert_newline(&ed);
    CHECK(eq(&ed, "    foo\n    ") && ed.cursor == 12, "Return copies 4-space indent");
    type(&ed, "bar");
    CHECK(eq(&ed, "    foo\n    bar"), "typing continues on the indented line");
    editor_undo(&ed);
    CHECK(eq(&ed, "    foo\n    "), "undo removes the typing");
    editor_undo(&ed);
    CHECK(eq(&ed, "    foo") && ed.cursor == 7, "one undo removes newline and indent");

    set(&ed, "\t\tx = 1;");
    sel(&ed, 8, 8);
    editor_insert_newline(&ed);
    CHECK(eq(&ed, "\t\tx = 1;\n\t\t"), "Return copies tabs");

    set(&ed, "    foo");
    sel(&ed, 2, 2); /* mitten in der Einrueckung */
    editor_insert_newline(&ed);
    CHECK(eq(&ed, "  \n    foo") && ed.cursor == 5, "indent copied only up to the cursor");

    set(&ed, "a\n  b\xC3\xA4" "c");
    sel(&ed, 4, 7); /* Selektion "b" + "ae" wird ersetzt */
    editor_insert_newline(&ed);
    CHECK(eq(&ed, "a\n  \n  c"), "Return replaces selection, indent from selection start's line");
    editor_undo(&ed);
    CHECK(eq(&ed, "a\n  b\xC3\xA4" "c"), "one undo restores the selection text");

    set(&ed, "no indent");
    sel(&ed, 9, 9);
    editor_insert_newline(&ed);
    CHECK(eq(&ed, "no indent\n"), "no indentation: plain newline");

    set(&ed, "");
    editor_insert_newline(&ed);
    CHECK(eq(&ed, "\n"), "empty document");
    editor_free(&ed);

    Editor field;
    editor_init(&field);
    editor_set_single_line(&field, 1);
    type(&field, "  x");
    editor_insert_newline(&field);
    CHECK(eq(&field, "  x "), "single-line field: newline becomes one space, no indent");
    editor_free(&field);
}

static void test_tab(void) {
    Editor ed;
    editor_init(&ed);

    /* Tab-Datei: drei Zeilen einruecken, Leerzeile bleibt leer */
    set(&ed, "a\n\nb\nc\nd");
    sel(&ed, 0, 6); /* "a\n\nb\nc" */
    editor_tab_key(&ed, 0);
    CHECK(eq(&ed, "\ta\n\n\tb\n\tc\nd"), "Tab indents touched lines, skips empty line");
    CHECK(ed.anchor == 0 && ed.cursor == 9, "selection covers the same text (anchor %zu cursor %zu)", ed.anchor, ed.cursor);
    editor_undo(&ed);
    CHECK(eq(&ed, "a\n\nb\nc\nd"), "one undo removes all indents");
    editor_redo(&ed);
    CHECK(eq(&ed, "\ta\n\n\tb\n\tc\nd"), "redo");
    sel(&ed, 0, 9);
    editor_tab_key(&ed, 1);
    CHECK(eq(&ed, "a\n\nb\nc\nd") && ed.anchor == 0 && ed.cursor == 6, "Shift+Tab outdents back");

    /* Selektion endet auf Spalte 0 der naechsten Zeile: die zaehlt nicht */
    set(&ed, "x\ny\nz");
    sel(&ed, 0, 4);
    editor_tab_key(&ed, 0);
    CHECK(eq(&ed, "\tx\n\ty\nz"), "line where selection ends at column 0 is not indented");

    /* Selektion mitten in der Zeile, rueckwaerts gezogen */
    set(&ed, "abc\ndef");
    sel(&ed, 6, 1); /* Anker in "def", Cursor in "abc" */
    editor_tab_key(&ed, 0);
    CHECK(eq(&ed, "\tabc\n\tdef") && ed.anchor == 8 && ed.cursor == 2, "backwards selection keeps direction");

    /* Leerzeichen-Datei: Einheit 4 Leerzeichen */
    set(&ed, "if x:\n    a\n    b\nc");
    sel(&ed, 6, 17);
    editor_tab_key(&ed, 0);
    CHECK(eq(&ed, "if x:\n        a\n        b\nc"), "spaces file indents with 4 spaces");
    editor_tab_key(&ed, 1);
    editor_tab_key(&ed, 1);
    CHECK(eq(&ed, "if x:\na\nb\nc"), "outdent removes up to 4 spaces per step");
    editor_tab_key(&ed, 1);
    CHECK(eq(&ed, "if x:\na\nb\nc"), "outdent on unindented lines changes nothing");

    /* Ausruecken: 2 Leerzeichen, Tab, gemischt */
    set(&ed, "  two\n\ttab\n      six");
    sel(&ed, 0, editor_length(&ed));
    editor_tab_key(&ed, 1);
    CHECK(eq(&ed, "two\ntab\n  six"), "outdent: 2 spaces, a tab, 4 of 6 spaces");

    /* Shift+Tab ohne Selektion: aktuelle Zeile, Cursor bleibt auf seinem Zeichen */
    set(&ed, "a\n\t\tfoo\nb");
    sel(&ed, 6, 6); /* auf dem zweiten 'o' */
    editor_tab_key(&ed, 1);
    CHECK(eq(&ed, "a\n\tfoo\nb") && ed.cursor == 5, "Shift+Tab without selection, cursor stays on 'o' (%zu)", ed.cursor);
    sel(&ed, 3, 3); /* Cursor mitten in der entfernten Einrueckung */
    editor_tab_key(&ed, 1);
    CHECK(eq(&ed, "a\nfoo\nb") && ed.cursor == 2, "cursor inside removed indent moves to line start");

    /* Tab ohne Selektion */
    set(&ed, "\tx\n\ty");
    sel(&ed, 2, 2);
    editor_tab_key(&ed, 0);
    CHECK(eq(&ed, "\tx\t\n\ty"), "tabs file: plain Tab inserts a tab");
    set(&ed, "    x\n    y\nab");
    sel(&ed, 13, 13); /* nach "a", Spalte 1 */
    editor_tab_key(&ed, 0);
    CHECK(eq(&ed, "    x\n    y\na   b"), "spaces file: plain Tab fills to the next tab stop");
    set(&ed, "    x\n    y\nabc");
    sel(&ed, 12, 14); /* "ab" in einer Zeile */
    editor_tab_key(&ed, 0);
    CHECK(eq(&ed, "    x\n    y\n    c"), "single-line selection is replaced (no block indent)");

    /* Umlaute: Einrueckung vor Mehrbyte-Zeichen, Positionen bleiben gueltig */
    set(&ed, "\xC3\xA4\xC3\xB6\n\xE2\x82\xAC");
    sel(&ed, 2, 7);
    editor_tab_key(&ed, 0);
    CHECK(eq(&ed, "\t\xC3\xA4\xC3\xB6\n\t\xE2\x82\xAC") && ed.anchor == 3 && ed.cursor == 9, "multibyte lines");

    /* ein Undo-Schritt, auch fuer Tab ohne Selektion in Leerzeichen-Datei */
    set(&ed, "    x\n    y\n");
    sel(&ed, 12, 12);
    editor_tab_key(&ed, 0);
    editor_undo(&ed);
    CHECK(eq(&ed, "    x\n    y\n"), "undo of space-tab");
    editor_free(&ed);
}

static void test_style(void) {
    Editor ed;
    editor_init(&ed);
    set(&ed, "");
    CHECK(!editor_indent_uses_spaces(&ed), "empty: tabs");
    set(&ed, "/*\n * doc\n */\n\tint x;\n");
    CHECK(!editor_indent_uses_spaces(&ed), "' * ' comment lines don't count as space indent");
    set(&ed, "a\n  b\n  c\n\td\n");
    CHECK(editor_indent_uses_spaces(&ed), "2 space lines vs 1 tab line: spaces");
    set(&ed, "a\n  b\n\tc\n\td\n");
    CHECK(!editor_indent_uses_spaces(&ed), "tabs win");
    editor_free(&ed);
}

/* Zufaellige Dokumente und Selektionen: Einruecken, dann Ausruecken ueber
 * dieselbe (mitgewanderte) Selektion ergibt das Original; Undo stellt nach
 * jedem Schritt exakt wieder her. */
static void test_fuzz(void) {
    static const char *pieces[] = { "a", "bc", " ", "\n", "\n", "\t", "  ", "\xC3\xA4", "x y", "\xE2\x82\xAC" };
    Editor ed;
    editor_init(&ed);
    char doc[512];
    for (int iter = 0; iter < 20000; iter++) {
        size_t len = 0, k = rnd(25);
        for (size_t i = 0; i < k; i++) {
            const char *p = pieces[rnd(10)];
            memcpy(doc + len, p, strlen(p));
            len += strlen(p);
        }
        /* Zeilen duerfen nicht schon mit Einrueckung beginnen, sonst ist
         * Ausruecken nicht das exakte Gegenteil (entfernt die vorhandene) */
        for (size_t i = 0; i < len; i++) {
            if ((i == 0 || doc[i - 1] == '\n') && (doc[i] == ' ' || doc[i] == '\t')) doc[i] = 'q';
        }
        editor_set_text(&ed, doc, len);
        size_t a = rnd((unsigned)len + 1), c = rnd((unsigned)len + 1);
        a = editor_utf8_seq_start(&ed, a);
        c = editor_utf8_seq_start(&ed, c);
        sel(&ed, a, c);
        editor_tab_key(&ed, 0);
        size_t l1;
        char *after = text_of(&ed, &l1);
        CHECK(ed.anchor <= l1 && ed.cursor <= l1, "selection inside buffer (iter %d)", iter);
        CHECK(editor_utf8_seq_start(&ed, ed.cursor) == ed.cursor || ed.cursor == l1, "cursor on char boundary (iter %d)", iter);
        editor_undo(&ed);
        {
            size_t lu;
            char *t = text_of(&ed, &lu);
            CHECK(lu == len && memcmp(t, doc, len) == 0, "undo restores original (iter %d)", iter);
            free(t);
        }
        editor_redo(&ed);
        {
            size_t lr;
            char *t = text_of(&ed, &lr);
            CHECK(lr == l1 && memcmp(t, after, l1) == 0, "redo restores indented (iter %d)", iter);
            free(t);
        }
        /* nur fuer echte Block-Einrueckung ist Ausruecken das Gegenteil */
        int block = 0;
        for (size_t i = (a < c ? a : c); i < (a < c ? c : a); i++) block |= doc[i] == '\n';
        if (block) {
            editor_set_cursor(&ed, a < c ? a : c, 0); /* Selektion ist nach Redo weg */
            sel(&ed, 0, 0);
            /* dieselben Zeilen wieder waehlen: Selektion aus dem Einruecken */
            editor_set_text(&ed, doc, len);
            sel(&ed, a, c);
            editor_tab_key(&ed, 0);
            editor_tab_key(&ed, 1);
            size_t l2;
            char *back = text_of(&ed, &l2);
            CHECK(l2 == len && memcmp(back, doc, len) == 0, "outdent(indent(x)) == x (iter %d)", iter);
            CHECK(ed.anchor == a && ed.cursor == c, "selection back where it was (iter %d: %zu/%zu want %zu/%zu)",
                  iter, ed.anchor, ed.cursor, a, c);
            free(back);
        }
        free(after);
    }
    editor_free(&ed);
}

int main(void) {
    test_newline();
    test_tab();
    test_style();
    test_fuzz();
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
