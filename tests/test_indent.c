/* Einruecken: Auto-Indent bei Return, Tab/Shift+Tab ueber mehrere Zeilen,
 * Tab vs. Leerzeichen je nach Datei, Undo als ein Schritt, Selektion bleibt
 * auf ihrem Text. Fuzz: Ausruecken nach Einruecken ergibt das Original. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "highlight.h"

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
    editor_insert_newline(&ed, 0);
    CHECK(eq(&ed, "    foo\n    ") && ed.cursor == 12, "Return copies 4-space indent");
    type(&ed, "bar");
    CHECK(eq(&ed, "    foo\n    bar"), "typing continues on the indented line");
    editor_undo(&ed);
    CHECK(eq(&ed, "    foo\n    "), "undo removes the typing");
    editor_undo(&ed);
    CHECK(eq(&ed, "    foo") && ed.cursor == 7, "one undo removes newline and indent");

    set(&ed, "\t\tx = 1;");
    sel(&ed, 8, 8);
    editor_insert_newline(&ed, 0);
    CHECK(eq(&ed, "\t\tx = 1;\n\t\t"), "Return copies tabs");

    set(&ed, "    foo");
    sel(&ed, 2, 2); /* mitten in der Einrueckung */
    editor_insert_newline(&ed, 0);
    CHECK(eq(&ed, "  \n    foo") && ed.cursor == 5, "indent copied only up to the cursor");

    set(&ed, "a\n  b\xC3\xA4" "c");
    sel(&ed, 4, 7); /* Selektion "b" + "ae" wird ersetzt */
    editor_insert_newline(&ed, 0);
    CHECK(eq(&ed, "a\n  \n  c"), "Return replaces selection, indent from selection start's line");
    editor_undo(&ed);
    CHECK(eq(&ed, "a\n  b\xC3\xA4" "c"), "one undo restores the selection text");

    set(&ed, "no indent");
    sel(&ed, 9, 9);
    editor_insert_newline(&ed, 0);
    CHECK(eq(&ed, "no indent\n"), "no indentation: plain newline");

    set(&ed, "");
    editor_insert_newline(&ed, 0);
    CHECK(eq(&ed, "\n"), "empty document");
    editor_free(&ed);

    Editor field;
    editor_init(&field);
    editor_set_single_line(&field, 1);
    type(&field, "  x");
    editor_insert_newline(&field, 0);
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

/* Unabhaengige Referenz fuer Block-Ein-/Ausruecken auf einem C-String:
 * Zeilenanfaenge als Menge, Text und Positionen in einem Durchlauf. */
static int ref_blank(const char *d, size_t len, size_t i) {
    while (i < len && (d[i] == ' ' || d[i] == '\t')) i++;
    return i >= len || d[i] == '\n' || (d[i] == '\r' && (i + 1 >= len || d[i + 1] == '\n'));
}
static size_t ref_block(const char *d, size_t len, size_t a, size_t c, int outdent, const char *unit, size_t ulen,
                        char *out, size_t *na, size_t *nc) {
    size_t s = a < c ? a : c, e = a < c ? c : a;
    size_t last = (e > s && d[e - 1] == '\n') ? e - 1 : e;
    size_t first = s;
    while (first > 0 && d[first - 1] != '\n') first--;
    static char is_start[1024];
    memset(is_start, 0, len + 1);
    is_start[first] = 1;
    for (size_t i = first; i < last; i++) if (d[i] == '\n') is_start[i + 1] = 1;
    long da = 0, dc = 0;
    size_t o = 0, i = 0;
    while (i <= len) {
        if (is_start[i]) {
            if (!outdent) {
                if (!ref_blank(d, len, i)) {
                    memcpy(out + o, unit, ulen); o += ulen;
                    if (a > i) da += (long)ulen;
                    if (c > i) dc += (long)ulen;
                }
            } else {
                size_t n = 0;
                if (i < len && d[i] == '\t') n = 1;
                else while (n < 4 && i + n < len && d[i + n] == ' ') n++;
                if (a > i) da -= (long)((a < i + n ? a : i + n) - i);
                if (c > i) dc -= (long)((c < i + n ? c : i + n) - i);
                i += n;
            }
        }
        if (i == len) break;
        out[o++] = d[i++];
    }
    *na = (size_t)((long)a + da);
    *nc = (size_t)((long)c + dc);
    return o;
}

/* Zufaellige Dokumente (Tab- und Leerzeichen-Stil, vorhandene Einrueckung,
 * CRLF-Leerzeilen, Umlaute) und Selektionen: Ergebnis und Selektion exakt
 * wie die Referenz, Undo/Redo exakt, Cursor nach Undo am Bereichsanfang. */
static void test_fuzz(void) {
    static const char *pieces[] = { "a", "bc", " ", "\n", "\n", "\t", "    ", "  ", "\xC3\xA4", "x y", "\xE2\x82\xAC", "\r\n", "\r" };
    Editor ed;
    editor_init(&ed);
    static char doc[1024], want[2048];
    for (int iter = 0; iter < 40000; iter++) {
        size_t len = 0, k = rnd(30);
        for (size_t i = 0; i < k; i++) {
            const char *p = pieces[rnd(13)];
            memcpy(doc + len, p, strlen(p));
            len += strlen(p);
        }
        editor_set_text(&ed, doc, len);
        size_t a = editor_utf8_seq_start(&ed, rnd((unsigned)len + 1));
        size_t c = editor_utf8_seq_start(&ed, rnd((unsigned)len + 1));
        int outdent = (int)rnd(2);
        int block = outdent;
        for (size_t i = (a < c ? a : c); i < (a < c ? c : a); i++) block |= doc[i] == '\n';
        if (!block) continue; /* einfacher Tab ohne Blockauswahl: eigene Faelle oben */
        const char *unit = editor_indent_uses_spaces(&ed) ? "    " : "\t";
        size_t wa, wc;
        size_t wlen = ref_block(doc, len, a, c, outdent, unit, strlen(unit), want, &wa, &wc);
        sel(&ed, a, c);
        size_t undo_before = ed.undo.count;
        editor_tab_key(&ed, outdent);
        size_t l1;
        char *got = text_of(&ed, &l1);
        CHECK(l1 == wlen && memcmp(got, want, wlen) == 0, "text differs from reference (iter %d, outdent %d)", iter, outdent);
        CHECK(ed.anchor == wa && ed.cursor == wc, "selection %zu/%zu, reference %zu/%zu (iter %d, outdent %d)",
              ed.anchor, ed.cursor, wa, wc, iter, outdent);
        int changed = !(wlen == len && memcmp(want, doc, len) == 0);
        CHECK(ed.undo.count - undo_before <= 2 && (changed || ed.undo.count == undo_before),
              "at most 2 undo records, none for a no-op (iter %d)", iter);
        if (changed) {
            editor_undo(&ed);
            size_t lu;
            char *t = text_of(&ed, &lu);
            CHECK(lu == len && memcmp(t, doc, len) == 0, "one undo restores original (iter %d)", iter);
            size_t first = (a < c ? a : c);
            while (first > 0 && doc[first - 1] != '\n') first--;
            CHECK(ed.cursor == first, "cursor after undo at first touched line (%zu want %zu, iter %d)", ed.cursor, first, iter);
            free(t);
            editor_redo(&ed);
            t = text_of(&ed, &lu);
            CHECK(lu == l1 && memcmp(t, got, l1) == 0, "redo exact (iter %d)", iter);
            free(t);
        }
        free(got);
    }
    editor_free(&ed);
}

static void test_more_cases(void) {
    Editor ed;
    editor_init(&ed);
    /* leere CRLF-Zeilen und reine Leerraum-Zeilen bekommen keine Einrueckung */
    set(&ed, "a\r\n\r\n  \r\nb\r\n");
    sel(&ed, 0, editor_length(&ed));
    editor_tab_key(&ed, 0);
    /* die Leerraum-Zeile "  " macht es zur Leerzeichen-Datei: Einheit 4 Leerzeichen */
    CHECK(eq(&ed, "    a\r\n\r\n  \r\n    b\r\n"), "CRLF blank and whitespace-only lines stay untouched");

    /* Outdent-Redo; Outdent ohne Wirkung legt keinen Schritt an und laesst Redo stehen */
    set(&ed, "\ta\n\tb");
    sel(&ed, 0, 5);
    editor_tab_key(&ed, 1);
    editor_undo(&ed);
    editor_redo(&ed);
    CHECK(eq(&ed, "a\nb"), "redo after outdent");
    editor_undo(&ed);
    CHECK(eq(&ed, "\ta\n\tb"), "undo again");
    set(&ed, "x\ny");
    type(&ed, "z");
    editor_undo(&ed);
    size_t n = ed.undo.count, p = ed.undo.pos;
    sel(&ed, 0, 3);
    editor_tab_key(&ed, 1);
    CHECK(ed.undo.count == n && ed.undo.pos == p, "no-op outdent: no undo step, redo kept");
    editor_redo(&ed);
    CHECK(eq(&ed, "zx\ny"), "redo still works after no-op outdent");

    /* Tab mit Selektion in einer Zeile, Leerzeichen-Datei: ein Schritt */
    set(&ed, "    a\n    b\nxyz");
    sel(&ed, 12, 14);
    editor_tab_key(&ed, 0);
    CHECK(eq(&ed, "    a\n    b\n    z"), "spaces file: selection replaced by spaces");
    editor_undo(&ed);
    CHECK(eq(&ed, "    a\n    b\nxyz"), "one undo");
    editor_free(&ed);
}

/* Alles auswaehlen + Tab bei 200 000 Zeilen: zwei Undo-Records, schnell. */
static void test_large(void) {
    size_t lines = 200000, len = 0;
    char *doc = malloc(lines * 12);
    for (size_t i = 0; i < lines; i++) { memcpy(doc + len, "some text\n", 10); len += 10; }
    Editor ed;
    editor_init(&ed);
    editor_set_text(&ed, doc, len);
    editor_select_all(&ed);
    editor_tab_key(&ed, 0);
    CHECK(editor_length(&ed) == len + lines && ed.undo.count == 2, "200k lines indented with 2 undo records (%zu)", ed.undo.count);
    editor_tab_key(&ed, 1);
    CHECK(editor_length(&ed) == len, "and outdented again");
    editor_free(&ed);
    free(doc);
}

/* Return nach '{' / ':' eine Stufe tiefer, Aufteilen von {}, '}' richtet
 * sich nach der oeffnenden Klammer, Leerraum-Zeilen werden leer. */
static void newline_at(Editor *ed, const char *text, size_t cursor, int rules) {
    set(ed, text);
    sel(ed, cursor, cursor);
    editor_insert_newline(ed, rules);
}

static void test_smart(void) {
    Editor ed;
    editor_init(&ed);
    const int B = BTN_INDENT_BRACES, PY = BTN_INDENT_BRACES | BTN_INDENT_COLON;

    newline_at(&ed, "int f() {", 9, B);
    CHECK(eq(&ed, "int f() {\n\t") && ed.cursor == 11, "after '{': one tab deeper (no indentation in the file yet)");
    newline_at(&ed, "x\n    if (x) {", 14, B);
    CHECK(eq(&ed, "x\n    if (x) {\n        ") && ed.cursor == 23, "spaces file: four spaces deeper");
    newline_at(&ed, "\tcall(a,", 8, B);
    CHECK(eq(&ed, "\tcall(a,\n\t"), "only when the opener is the last character");
    newline_at(&ed, "\tcall(", 6, B);
    CHECK(eq(&ed, "\tcall(\n\t\t"), "'(' opens too");
    newline_at(&ed, "a = [  ", 7, B);
    CHECK(eq(&ed, "a = [  \n\t"), "trailing spaces after the opener are skipped");
    newline_at(&ed, "int f() {", 9, 0);
    CHECK(eq(&ed, "int f() {\n"), "no rules (plain text): indentation only copied");

    /* {} aufteilen */
    newline_at(&ed, "void f() {}", 10, B);
    CHECK(eq(&ed, "void f() {\n\t\n}") && ed.cursor == 12 && !editor_has_selection(&ed), "{|} split: cursor indented in the middle");
    editor_undo(&ed);
    CHECK(eq(&ed, "void f() {}") && ed.cursor == 10, "split is one undo step");
    newline_at(&ed, "x\n    y = [  ]", 11, B);
    CHECK(eq(&ed, "x\n    y = [\n        \n    ]") && ed.cursor == 20, "[  ] split, spaces in between dropped");
    newline_at(&ed, "f(a)", 2, B);
    CHECK(eq(&ed, "f(\n\ta)"), "not split when something else follows (only deeper)");
    newline_at(&ed, "a{)", 2, B);
    CHECK(eq(&ed, "a{\n\t)"), "not split for a different closer");

    /* Python */
    newline_at(&ed, "def f(x):", 9, PY);
    CHECK(eq(&ed, "def f(x):\n\t"), "Python: deeper after ':'");
    newline_at(&ed, "x\n    if a:  ", 13, PY);
    CHECK(eq(&ed, "x\n    if a:  \n        "), "':' with trailing spaces");
    newline_at(&ed, "case 1:", 7, B);
    CHECK(eq(&ed, "case 1:\n"), "C: ':' does not indent");
    newline_at(&ed, "d = {", 5, PY);
    CHECK(eq(&ed, "d = {\n\t"), "Python: brackets too");

    /* Leerraum-Zeile wird leer */
    newline_at(&ed, "a\n    ", 6, 0);
    CHECK(eq(&ed, "a\n\n    ") && ed.cursor == 7, "whitespace-only line: no trailing whitespace left behind");
    editor_undo(&ed);
    CHECK(eq(&ed, "a\n    "), "one undo step (cursor %zu)", ed.cursor);
    newline_at(&ed, "a\n    \nb", 4, 0);
    CHECK(eq(&ed, "a\n\n    \nb") && ed.cursor == 5, "cursor inside a whitespace-only line: before it cleared, the rest moves down");
    newline_at(&ed, "a\n  x", 4, 0);
    CHECK(eq(&ed, "a\n  \n  x"), "text after the cursor: the line keeps its indentation (unchanged behaviour)");

    /* einzeiliges Feld: keine Regeln */
    Editor field;
    editor_init(&field);
    editor_set_single_line(&field, 1);
    type(&field, "a{}");
    sel(&field, 2, 2);
    editor_insert_newline(&field, B);
    CHECK(eq(&field, "a{ }"), "single-line field: rules ignored, newline becomes a space");
    editor_set_text(&field, "b{  }", 5);
    sel(&field, 2, 2);
    editor_insert_newline(&field, B);
    CHECK(eq(&field, "b{   }"), "single-line field: spaces before the closer stay");
    editor_free(&field);

    /* '}' richtet sich aus */
    set(&ed, "int f() {\n\t\t");
    sel(&ed, 12, 12);
    CHECK(editor_type_closing_bracket(&ed, '}', B) && eq(&ed, "int f() {\n}") && ed.cursor == 11, "'}' aligned with its '{' line");
    editor_undo(&ed);
    CHECK(eq(&ed, "int f() {\n\t\t"), "one undo step");
    set(&ed, "{\n    {\n        x;\n        ");
    sel(&ed, 28, 28);
    CHECK(editor_type_closing_bracket(&ed, '}', B) && eq(&ed, "{\n    {\n        x;\n    }"), "nested: the inner '{'");
    set(&ed, "{\n    a[{}];\n    ");
    sel(&ed, 18, 18);
    CHECK(editor_type_closing_bracket(&ed, '}', B) && eq(&ed, "{\n    a[{}];\n}"), "closed pairs in between are skipped");
    set(&ed, "call(\n    a,\n    ");
    sel(&ed, 17, 17);
    CHECK(editor_type_closing_bracket(&ed, ')', B) && eq(&ed, "call(\n    a,\n)"), "')' too");
    set(&ed, "x\n        ");
    sel(&ed, 10, 10);
    CHECK(editor_type_closing_bracket(&ed, '}', B) && eq(&ed, "x\n    }"), "no opener: one level less (spaces)");
    set(&ed, "x\n\t\t");
    sel(&ed, 4, 4);
    CHECK(editor_type_closing_bracket(&ed, ']', B) && eq(&ed, "x\n\t]"), "no opener: one level less (tab)");
    set(&ed, "    a\n    b\n\t\t");
    sel(&ed, 14, 14);
    CHECK(editor_type_closing_bracket(&ed, ']', B) && eq(&ed, "    a\n    b\n\t]"), "tab-indented line in a spaces file: one tab less");
    set(&ed, "{\n  x }");
    sel(&ed, 6, 6);
    CHECK(!editor_type_closing_bracket(&ed, '}', B), "text before the cursor: not responsible");
    set(&ed, "{\n    }");
    sel(&ed, 6, 6);
    CHECK(!editor_type_closing_bracket(&ed, '}', B), "same bracket right after the cursor: overtyping handles it");
    set(&ed, "{\n    ");
    sel(&ed, 6, 6);
    CHECK(!editor_type_closing_bracket(&ed, '}', 0) && !editor_type_closing_bracket(&ed, 'x', B) && eq(&ed, "{\n    "),
          "no rules / other characters: not responsible");
    sel(&ed, 3, 6);
    CHECK(!editor_type_closing_bracket(&ed, '}', B), "selection: not responsible");
    set(&ed, "{");
    sel(&ed, 1, 1);
    CHECK(!editor_type_closing_bracket(&ed, '}', B), "at column 0 after text / no whitespace: not responsible");
    editor_free(&ed);

    /* Regeln je Sprache (highlight.c) */
    CHECK(btn_highlight_indent_rules(btn_highlight_lang_for_path("a.c")) == B &&
              btn_highlight_indent_rules(btn_highlight_lang_for_path("a.js")) == B &&
              btn_highlight_indent_rules(btn_highlight_lang_for_path("a.swift")) == B &&
              btn_highlight_indent_rules(btn_highlight_lang_for_path("a.sh")) == B,
          "C/JS/Swift/Shell: brackets");
    CHECK(btn_highlight_indent_rules(btn_highlight_lang_for_path("a.py")) == PY, "Python: brackets and ':'");
    CHECK(btn_highlight_indent_rules(btn_highlight_lang_for_path("a.md")) == 0 &&
              btn_highlight_indent_rules(btn_highlight_lang_for_path("a.ini")) == 0 &&
              btn_highlight_indent_rules(btn_highlight_lang_for_path("a.txt")) == 0 &&
              btn_highlight_indent_rules(NULL) == 0,
          "Markdown/INI/text/none: only copy the indentation");
}

int main(void) {
    test_newline();
    test_smart();
    test_tab();
    test_style();
    test_more_cases();
    test_fuzz();
    test_large();
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
