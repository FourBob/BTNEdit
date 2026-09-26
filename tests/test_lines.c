/* Zeilen-Befehle (editor.c): Kommentar umschalten, Zeilen duplizieren,
 * Zeilen verschieben - samt Cursor/Selektion, Undo und Fuzz-Rueckwegen -
 * und die Markierungen fuer unsichtbare Zeichen aus render.c gegen die
 * Spaltenregel des Editors. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "eol.h"
#include "highlight.h"
#include "filestamp.h"
#include "doc_extracted.h"
#include "focus_extracted.h"

/* Zeilen-Befehle aus dem Menue (main.c): Stubs fuer Fenster/Fokus */
enum { BTN_MENU_TOGGLE_COMMENT = 1, BTN_MENU_DUPLICATE_LINES, BTN_MENU_MOVE_LINES_UP, BTN_MENU_MOVE_LINES_DOWN };
static Document g_doc;
static BtnFocus g_focus = BTN_FOCUS_DOCUMENT;
static int g_beeps = 0;
static Document *active_doc(void) { return &g_doc; }
static void btn_beep(void) { g_beeps++; }
#include "linecmd_extracted.h"

#include "invisibles_extracted.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 40) { printf("FAIL: " __VA_ARGS__); printf("\n"); } fails++; } } while (0)

static Editor ed;

static int text_is(const char *want) {
    size_t len;
    char *t = editor_copy_all(&ed, &len);
    int ok = len == strlen(want) && memcmp(t, want, len) == 0;
    if (!ok && fails < 40) {
        printf("   got [%.*s] want [%s]\n", (int)len, t, want);
    }
    free(t);
    return ok;
}

static void set(const char *text, size_t anchor, size_t cursor) {
    editor_set_text(&ed, text, strlen(text));
    editor_set_cursor(&ed, anchor, 0);
    editor_set_cursor(&ed, cursor, 1);
}

static void test_comment(void) {
    set("int a;\n    int b;\n", 0, 8);
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("// int a;\n//     int b;\n"), "comment at the smallest indentation (0)");
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("int a;\n    int b;\n"), "toggle back");

    set("    a\n        b\n", 4, 12);
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("    // a\n    //     b\n"), "indented block: prefix at the block's indentation");
    CHECK(ed.anchor == 7 && ed.cursor == 18, "selection stays on its text (%zu, %zu)", ed.anchor, ed.cursor);
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("    a\n        b\n") && ed.anchor == 4 && ed.cursor == 12, "and back, selection too");

    set("        foo\n\t\tbar\n", 0, 16);
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("//         foo\n// \t\tbar\n"),
          "tabs vs spaces: no common whitespace -> column 0, indentation not split");
    set("\t\tfoo\n\t  bar", 0, 13);
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("\t// \tfoo\n\t//   bar"), "common whitespace prefix (one tab)");
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("\t\tfoo\n\t  bar"), "and back");

    set("// a\nb\n", 0, 6);
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("// // a\n// b\n"), "mixed block: everything gets commented");

    set("//a\n  //b\n  // c", 0, 16);
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("a\n  b\n  c"), "uncomment with and without a following space");

    set("a\n\n  \n\r\nb", 0, 9);
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("// a\n\n  \n\r\n// b"), "blank, whitespace-only and CRLF-blank lines untouched");

    set("\n  \n", 0, 4);
    size_t seq = ed.edit_seq;
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("\n  \n") && ed.edit_seq == seq, "only blank lines: nothing changes");

    set("x = 1", 5, 5);
    editor_toggle_line_comment(&ed, "#");
    CHECK(text_is("# x = 1") && ed.cursor == 7, "'#' language, cursor at the end follows");
    editor_toggle_line_comment(&ed, "#");
    CHECK(text_is("x = 1") && ed.cursor == 5, "and back");

    set("a\nbc\nd", 3, 3); /* Cursor zwischen b und c, ohne Selektion: nur diese Zeile */
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("a\n// bc\nd") && ed.cursor == 6, "no selection: cursor line only, cursor stays on 'c'");
    editor_undo(&ed);
    CHECK(text_is("a\nbc\nd"), "one undo step");

    set("a\nb\nc", 0, 4); /* endet auf Spalte 0 von "c" */
    editor_toggle_line_comment(&ed, "//");
    CHECK(text_is("// a\n// b\nc"), "selection ending at column 0 excludes that line");

    set("a", 0, 0);
    editor_toggle_line_comment(&ed, "");
    editor_toggle_line_comment(&ed, NULL);
    CHECK(text_is("a"), "empty/NULL prefix: nothing");

    /* Fuzz: kommentieren und wieder entkommentieren = Original, Selektion auch */
    srand(7);
    long bad = 0, runs = 0;
    for (int it = 0; it < 3000; it++) {
        char buf[256];
        size_t n = 0;
        int nl = 1 + rand() % 6;
        for (int l = 0; l < nl; l++) {
            int ind = rand() % 4;
            for (int k = 0; k < ind; k++) {
                buf[n++] = (rand() % 3) ? ' ' : '\t';
            }
            if (rand() % 4 == 0) {
                memcpy(buf + n, "//", 2);
                n += 2;
                if (rand() % 2) {
                    buf[n++] = ' ';
                }
            }
            int w = rand() % 5;
            for (int k = 0; k < w; k++) {
                buf[n++] = "ab /x"[rand() % 5];
            }
            if (l + 1 < nl || rand() % 2) {
                buf[n++] = '\n';
            }
        }
        buf[n] = 0;
        size_t a = n ? (size_t)rand() % (n + 1) : 0, c = n ? (size_t)rand() % (n + 1) : 0;
        set(buf, a, c);
        size_t first_seq = ed.edit_seq;
        editor_toggle_line_comment(&ed, "//");
        size_t len;
        char *after = editor_copy_all(&ed, &len);
        /* Kommentiert? Dann steht jetzt vor jeder beruehrten nicht-leeren
         * Zeile ein neues "// " - der Rueckweg muss exakt sein. */
        int grew = len > n;
        free(after);
        if (!grew || ed.edit_seq == first_seq) {
            continue;
        }
        runs++;
        editor_toggle_line_comment(&ed, "//");
        if (!text_is(buf) || ed.anchor != a || ed.cursor != c) {
            bad++;
        }
    }
    CHECK(bad == 0 && runs > 500, "fuzz: comment + uncomment = original incl. selection (%ld of %ld bad)", bad, runs);
}

static void test_duplicate(void) {
    set("a\nbb\nc", 3, 3);
    editor_duplicate_lines(&ed);
    CHECK(text_is("a\nbb\nbb\nc") && ed.cursor == 6 && ed.anchor == 6, "duplicate: copy below, cursor into the copy");
    editor_duplicate_lines(&ed);
    CHECK(text_is("a\nbb\nbb\nbb\nc") && ed.cursor == 9, "again: keeps duplicating downwards");
    editor_undo(&ed);
    CHECK(text_is("a\nbb\nbb\nc"), "one undo step");

    set("a\nb", 3, 3);
    editor_duplicate_lines(&ed);
    CHECK(text_is("a\nb\nb") && ed.cursor == 5, "last line without newline");

    set("x\ny\nz\n", 0, 3);
    editor_duplicate_lines(&ed);
    CHECK(text_is("x\ny\nx\ny\nz\n") && ed.anchor == 4 && ed.cursor == 7, "block of lines, selection moves along");

    set("", 0, 0);
    editor_duplicate_lines(&ed);
    CHECK(text_is("\n") && ed.cursor == 1, "empty document");
}

static void test_move(void) {
    set("a\nb\nc", 2, 2);
    editor_move_lines(&ed, 0);
    CHECK(text_is("b\na\nc") && ed.cursor == 0, "up");
    editor_move_lines(&ed, 1);
    editor_move_lines(&ed, 1);
    CHECK(text_is("a\nc\nb") && ed.cursor == 4, "down twice: to the last line (no newline there)");
    size_t seq = ed.edit_seq;
    editor_move_lines(&ed, 1);
    CHECK(text_is("a\nc\nb") && ed.edit_seq == seq, "down at the end: nothing");
    editor_move_lines(&ed, 0);
    CHECK(text_is("a\nb\nc") && ed.cursor == 2, "last line moved up gets its newline");
    editor_undo(&ed);
    CHECK(text_is("a\nc\nb"), "one undo step");

    set("a\nb", 0, 0);
    editor_move_lines(&ed, 1);
    CHECK(text_is("b\na") && ed.cursor == 2, "first line down into a last line without newline");
    seq = ed.edit_seq;
    set("a\nb", 0, 0);
    seq = ed.edit_seq;
    editor_move_lines(&ed, 0);
    CHECK(text_is("a\nb") && ed.edit_seq == seq, "up at the top: nothing");

    set("1\n22\n333\n4\n", 3, 7); /* "22" und "333" teilweise */
    editor_move_lines(&ed, 0);
    CHECK(text_is("22\n333\n1\n4\n") && ed.anchor == 1 && ed.cursor == 5, "block up, selection keeps its place");
    editor_move_lines(&ed, 1);
    editor_move_lines(&ed, 1);
    CHECK(text_is("1\n4\n22\n333\n") && ed.anchor == 5 && ed.cursor == 9, "block down twice");

    /* roh geladene CRLF-Datei (gemischte Zeilenenden): "\r\n" wandert als Ganzes */
    set("a\r\nb\r\nc", 7, 7);
    editor_move_lines(&ed, 0);
    CHECK(text_is("a\r\nc\r\nb"), "CRLF: last line up keeps CRLF, no lone CR");
    set("a\r\nb", 0, 0);
    editor_move_lines(&ed, 1);
    CHECK(text_is("b\r\na"), "CRLF: first line down into the last line");
    set("a\r\nb", 3, 3);
    editor_duplicate_lines(&ed);
    CHECK(text_is("a\r\nb\r\nb") && ed.cursor == 6, "CRLF: duplicate the last line");
    set("a\r\nb\r\n", 0, 0);
    editor_duplicate_lines(&ed);
    CHECK(text_is("a\r\na\r\nb\r\n"), "CRLF: duplicate a middle line");

    set("a\nb\n", 4, 4); /* leere letzte Zeile */
    editor_move_lines(&ed, 0);
    CHECK(text_is("a\n\nb") && ed.cursor == 2, "empty last line up");

    /* Fuzz: runter und wieder hoch (bzw. umgekehrt) = Original */
    srand(11);
    long bad = 0, runs = 0;
    for (int it = 0; it < 4000; it++) {
        char buf[128];
        size_t n = 0;
        int nl = 1 + rand() % 6;
        for (int l = 0; l < nl; l++) {
            int w = rand() % 4;
            for (int k = 0; k < w; k++) {
                buf[n++] = "xyz "[rand() % 4];
            }
            if (l + 1 < nl || rand() % 2) {
                buf[n++] = '\n';
            }
        }
        buf[n] = 0;
        size_t a = n ? (size_t)rand() % (n + 1) : 0, c = n ? (size_t)rand() % (n + 1) : 0;
        int down = rand() % 2;
        size_t hi = a > c ? a : c, lo = a < c ? a : c;
        if (hi > lo && buf[hi - 1] == '\n') {
            continue; /* endet auf Spalte 0 der naechsten Zeile: die zaehlt nicht (Randfall, unten) */
        }
        set(buf, a, c);
        size_t seq0 = ed.edit_seq;
        editor_move_lines(&ed, down);
        if (ed.edit_seq == seq0) {
            continue;
        }
        runs++;
        size_t mid_a = ed.anchor, mid_c = ed.cursor;
        editor_move_lines(&ed, !down);
        if (!text_is(buf) || ed.anchor != a || ed.cursor != c || mid_a > editor_length(&ed) || mid_c > editor_length(&ed)) {
            bad++;
        }
    }
    CHECK(bad == 0 && runs > 1000, "fuzz: move + move back = original incl. selection (%ld of %ld bad)", bad, runs);

    /* Randfall: Selektion bis Spalte 0 der naechsten Zeile - die zaehlt
     * nicht mit, der Block sind die zwei Zeilen darueber */
    set("a\nb\nc\nd", 0, 4);
    editor_move_lines(&ed, 1);
    CHECK(text_is("c\na\nb\nd") && ed.anchor == 2 && ed.cursor == 6, "selection ending at column 0: that line stays out");
}

/* Spalte (in UTF-8-Zeichen) des Markers fuer Byte p. */
static size_t mark_at_col(const char *marks, size_t col, char *out3) {
    size_t i = 0, c = 0;
    while (marks[i] && c < col) {
        i += ((unsigned char)marks[i] < 0x80) ? 1 : ((unsigned char)marks[i] < 0xE0 ? 2 : 3);
        c++;
    }
    size_t l = ((unsigned char)marks[i] < 0x80) ? 1 : ((unsigned char)marks[i] < 0xE0 ? 2 : 3);
    memcpy(out3, marks + i, l);
    out3[l] = 0;
    return l;
}

static size_t char_count(const char *s) {
    size_t n = 0;
    for (; *s; s++) {
        n += ((unsigned char)*s & 0xC0) != 0x80;
    }
    return n;
}

static void test_invisibles(void) {
    char out[256];
    int any;
    build_invisibles((const unsigned char *)"a b\tc", 5, 1, out, &any);
    CHECK(strcmp(out, " \xC2\xB7 \xC2\xBB \xC2\xAC") == 0, "space, tab, line end (%s)", out);
    build_invisibles((const unsigned char *)"\tx", 2, 0, out, &any);
    CHECK(strcmp(out, "\xC2\xBB    ") == 0, "tab fills its columns (then x), no line end mark");
    build_invisibles((const unsigned char *)"", 0, 1, out, &any);
    CHECK(strcmp(out, "\xC2\xAC") == 0 && any, "empty line: just the line end");
    build_invisibles((const unsigned char *)"abc", 3, 0, out, &any);
    CHECK(!any, "nothing to mark: nothing drawn");

    /* Fuzz gegen die Spaltenregel des Editors: jeder Marker genau auf der
     * Spalte seines Zeichens, Gesamtbreite = Breite der Row */
    srand(3);
    const char *pieces[] = { " ", "\t", "a", "\xC3\xA4", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xFF", "\xC3", "\r" };
    long bad = 0;
    for (int it = 0; it < 3000; it++) {
        char row[64];
        size_t n = 0;
        int parts = rand() % 10;
        for (int k = 0; k < parts; k++) {
            const char *p = pieces[rand() % 9];
            memcpy(row + n, p, strlen(p));
            n += strlen(p);
        }
        int line_end = rand() % 2;
        char *marks = malloc(n * 6 + 3);
        build_invisibles((const unsigned char *)row, n, line_end, marks, &any);
        int has = line_end || memchr(row, ' ', n) || memchr(row, '\t', n);
        if (any != has) {
            bad++;
        }
        editor_set_text(&ed, row, n);
        size_t width = editor_visual_column_in_range(&ed, 0, n);
        if (char_count(marks) != width + (size_t)line_end) {
            bad++;
        }
        for (size_t p = 0; p < n; p++) {
            if (row[p] != ' ' && row[p] != '\t') {
                continue;
            }
            char m[4];
            mark_at_col(marks, editor_visual_column_in_range(&ed, 0, p), m);
            if (strcmp(m, row[p] == ' ' ? "\xC2\xB7" : "\xC2\xBB") != 0) {
                bad++;
            }
        }
        free(marks);
    }
    CHECK(bad == 0, "fuzz: markers on the editor's columns (%ld bad)", bad);
}

static void test_menu_glue(void) {
    Editor *e = &g_doc.editor;
    editor_init(e);
    editor_set_text(e, "a\nb", 3);
    g_doc.path = "x.py";
    perform_line_command(BTN_MENU_TOGGLE_COMMENT);
    size_t len;
    char *t = editor_copy_all(e, &len);
    CHECK(len == 5 && memcmp(t, "# a\nb", 5) == 0 && g_beeps == 0, "menu: comment with the file's language");
    free(t);
    g_doc.path = "notes.txt";
    perform_line_command(BTN_MENU_TOGGLE_COMMENT);
    CHECK(g_beeps == 1, "menu: no line comment for the language -> beep");
    g_doc.path = NULL;
    perform_line_command(BTN_MENU_TOGGLE_COMMENT);
    CHECK(g_beeps == 2, "menu: untitled -> beep");
    g_focus = BTN_FOCUS_SEARCH;
    size_t seq = e->edit_seq;
    perform_line_command(BTN_MENU_DUPLICATE_LINES);
    CHECK(g_beeps == 3 && e->edit_seq == seq, "menu: find field focused -> beep, document untouched");
    g_focus = BTN_FOCUS_DOCUMENT;
    perform_line_command(BTN_MENU_DUPLICATE_LINES);
    perform_line_command(BTN_MENU_MOVE_LINES_DOWN);
    t = editor_copy_all(e, &len);
    CHECK(len == 9 && memcmp(t, "# a\nb\n# a", 9) == 0 && e->cursor == 6, "menu: duplicate, then the copy moves down");
    free(t);
    perform_line_command(BTN_MENU_MOVE_LINES_UP);
    t = editor_copy_all(e, &len);
    CHECK(len == 9 && memcmp(t, "# a\n# a\nb", 9) == 0 && e->cursor == 4, "menu: and back up");
    free(t);
    editor_free(e);
}

static void test_comment_prefix(void) {
    CHECK(strcmp(btn_highlight_line_comment(btn_highlight_lang_for_path("a.c")), "//") == 0, "C: //");
    CHECK(strcmp(btn_highlight_line_comment(btn_highlight_lang_for_path("a.swift")), "//") == 0, "Swift: //");
    CHECK(strcmp(btn_highlight_line_comment(btn_highlight_lang_for_path("a.py")), "#") == 0, "Python: #");
    CHECK(strcmp(btn_highlight_line_comment(btn_highlight_lang_for_path("a.ini")), ";") == 0, "INI: ; (Windows INI knows only ;)");
    CHECK(btn_highlight_line_comment(btn_highlight_lang_for_path("web.config")) == NULL, ".config (often XML): none");
    CHECK(strcmp(btn_highlight_line_comment(btn_highlight_lang_for_path("a.sh")), "#") == 0, "Shell: #");
    CHECK(btn_highlight_line_comment(btn_highlight_lang_for_path("a.md")) == NULL, "Markdown: none");
    CHECK(btn_highlight_line_comment(btn_highlight_lang_for_path("a.txt")) == NULL, "unknown: none");
    CHECK(btn_highlight_line_comment(NULL) == NULL, "no language: none");
}

int main(void) {
    editor_init(&ed);
    test_comment();
    test_duplicate();
    test_move();
    test_invisibles();
    test_comment_prefix();
    test_menu_glue();
    editor_free(&ed);
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
