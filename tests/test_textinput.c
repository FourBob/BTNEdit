/* Eingabemethoden: textinput.c (UTF-16 <-> Bytes nach der Zeichenregel,
 * vorlaeufiger Text, Bereiche relativ zum Ursprung vor dem Cursor) und der
 * Klebecode aus main.c, der die NSTextInputClient-Aufrufe umsetzt - mit
 * nachgestellten macOS-Ablaeufen (Tottaste, Pinyin, Akzent-Menue,
 * Emoji-Palette, Eingabe ueber eine Selektion, Suchfeld). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "editor.h"
#include "textinput.h"
#include "gapbuffer.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 25) { printf("FAIL: " __VA_ARGS__); printf("\n"); } fails++; } } while (0)

static unsigned long rng = 777;
static unsigned rnd(unsigned n) { rng = rng * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)((rng >> 33) % n); }

/* ---- Stubs fuer den Klebecode aus main.c ---- */
static Editor g_doc, g_search_editor, g_replace_editor;
static char g_search_status[128];
static BtnMarkedText g_marked;
static int g_live_searches, g_discards, g_redraws;
static char g_last_on_key[64];
static void perform_live_search(void) { g_live_searches++; }
static void sync_window_state(void) {}
static void sync_scroll_to_cursor(void) {}
static void btn_app_request_redraw(void) { g_redraws++; }
static void btn_text_input_discard(void) { g_discards++; }
static char *btn_dup_cstring(const char *s) { return strdup(s); }
static void on_key(const char *chars, unsigned short keycode, unsigned long mods);
static Editor *focused_editor(void);
#include "focus_extracted.h"
static BtnFocus g_focus = BTN_FOCUS_DOCUMENT;
#include "textinput_glue_extracted.h"
static Editor *focused_editor(void) {
    return g_focus == BTN_FOCUS_SEARCH ? &g_search_editor : g_focus == BTN_FOCUS_REPLACE ? &g_replace_editor : &g_doc;
}
/* on_key()s Zeichenpfad: Dokument ueber insert_typed_chars(), Felder direkt */
static void on_key(const char *chars, unsigned short keycode, unsigned long mods) {
    (void)mods;
    CHECK(keycode == KEYCODE_TEXT, "text arrives with KEYCODE_TEXT");
    snprintf(g_last_on_key, sizeof g_last_on_key, "%s", chars);
    if (strcmp(chars, "\r") == 0) return; /* Return: im echten on_key() Zeilenumbruch bzw. Suchen */
    if (g_focus == BTN_FOCUS_DOCUMENT) {
        insert_typed_chars(&g_doc, chars);
    } else {
        editor_insert_text(focused_editor(), chars, strlen(chars));
        if (g_focus == BTN_FOCUS_SEARCH) perform_live_search();
    }
}

static int doc_is(const char *s) {
    size_t l;
    char *t = editor_copy_all(&g_doc, &l);
    int ok = l == strlen(s) && memcmp(t, s, l) == 0;
    if (!ok) printf("   doc: \"%.*s\" want \"%s\"\n", (int)l, t, s);
    free(t);
    return ok;
}
static void reset_doc(const char *s, size_t cursor) {
    editor_set_text(&g_doc, s, strlen(s));
    editor_set_cursor(&g_doc, cursor, 0);
    btn_marked_clear(&g_marked);
    g_focus = BTN_FOCUS_DOCUMENT;
}

static void test_units(void) {
    const char *s = "a\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80\xE4"; /* a ae EUR emoji latin1-ae */
    size_t len = strlen(s);
    CHECK(btn_ti_utf16_len(s, len) == 6, "utf16 len (a=1 ae=1 EUR=1 emoji=2 latin1=1)");
    size_t want[] = { 0, 1, 3, 6, 10, 10, 11, 11 };
    for (size_t u = 0; u < 8; u++) {
        CHECK(btn_ti_utf16_to_bytes(s, len, u) == want[u], "to_bytes(%zu) = %zu want %zu", u, btn_ti_utf16_to_bytes(s, len, u), want[u]);
    }
    BtnMarkedText m = { 0 };
    btn_marked_set(&m, "ni\xE4\xBD\xA0", 5, 2, 1);
    CHECK(m.len == 5 && m.sel_start == 2 && m.sel_end == 5 && m.text[5] == 0, "marked selection in bytes");
    btn_marked_clear(&m);
    CHECK(m.text == NULL && m.len == 0, "marked cleared");
}

static void test_origin_ranges(void) {
    Editor ed;
    editor_init(&ed);
    editor_set_text(&ed, "line1\nab\xC3\xA4\xF0\x9F\x98\x80z", 15);
    editor_set_cursor(&ed, 14, 0); /* vor 'z' */
    CHECK(btn_ti_origin(&ed) == 6, "origin = start of cursor line");
    size_t loc, len;
    btn_ti_selection(&ed, &loc, &len);
    CHECK(loc == 5 && len == 0, "caret u16 in line: a b ae emoji(2) = 5 (%zu)", loc);
    size_t s, e;
    CHECK(btn_ti_range_to_bytes(&ed, 3, 2, &s, &e) && s == 10 && e == 14, "range over the emoji (2 units)");
    CHECK(btn_ti_range_to_bytes(&ed, 4, 1, &s, &e) && s == 14 && e == 14, "start inside a surrogate pair rounds up to the next character");
    CHECK(btn_ti_range_to_bytes(&ed, 3, 1, &s, &e) && s == 10 && e == 14, "end inside a surrogate pair covers the whole emoji");
    CHECK(btn_ti_range_to_bytes(&ed, 2, 1, &s, &e) && s == 8 && e == 10, "range over ae");
    CHECK(!btn_ti_range_to_bytes(&ed, 6, 2, &s, &e), "range past document end is rejected");
    size_t aloc, n;
    uint16_t *u = btn_ti_substring(&ed, 0, 100, &aloc, &n);
    uint16_t exp[] = { 'a', 'b', 0xE4, 0xD83D, 0xDE00, 'z' };
    CHECK(u && n == 6 && aloc == 0 && memcmp(u, exp, sizeof exp) == 0, "substring UTF-16 incl. surrogates");
    free(u);
    CHECK(btn_ti_substring(&ed, 50, 1, &aloc, &n) == NULL, "substring beyond end is NULL");
    editor_set_text(&ed, "x\xE4y", 3); /* Latin-1-Byte */
    editor_set_cursor(&ed, 3, 0);
    u = btn_ti_substring(&ed, 0, 3, &aloc, &n);
    CHECK(u && n == 3 && u[1] == 0xE4, "invalid byte decodes as Latin-1, one unit");
    free(u);

    /* sehr lange Zeile: Ursprung hoechstens BTN_TI_WINDOW Zeichen zurueck */
    size_t big = 5000;
    char *long_line = malloc(big);
    memset(long_line, 'q', big);
    editor_set_text(&ed, long_line, big);
    editor_set_cursor(&ed, big, 0);
    CHECK(btn_ti_origin(&ed) == big - BTN_TI_WINDOW, "origin limited to the window");
    btn_ti_selection(&ed, &loc, &len);
    CHECK(loc == BTN_TI_WINDOW, "caret offset within window");
    editor_select_all(&ed);
    btn_ti_selection(&ed, &loc, &len);
    CHECK(loc == 0 && len == big, "selection length");
    free(long_line);
    editor_free(&ed);
}

/* Referenz: Bytes ab Ursprung selbst nach UTF-16 dekodieren und mit
 * substring/range_to_bytes vergleichen. */
static void test_fuzz(void) {
    static const char *pieces[] = { "a", "\n", "\xC3\xA4", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xE4", "\xC3", " " };
    Editor ed;
    editor_init(&ed);
    char doc[256];
    for (int iter = 0; iter < 20000; iter++) {
        size_t len = 0, k = rnd(20);
        for (size_t i = 0; i < k; i++) { const char *p = pieces[rnd(8)]; memcpy(doc + len, p, strlen(p)); len += strlen(p); }
        editor_set_text(&ed, doc, len);
        size_t cur = editor_utf8_seq_start(&ed, rnd((unsigned)len + 1));
        editor_set_cursor(&ed, cur, 0);
        size_t origin = btn_ti_origin(&ed);
        CHECK(origin <= cur && (origin == 0 || doc[origin - 1] == '\n'), "origin at line start (iter %d)", iter);
        for (size_t i = origin; i < cur; i++) CHECK(doc[i] != '\n', "no newline between origin and cursor");
        /* Referenz-Einheiten ab Ursprung */
        size_t total_u = btn_ti_utf16_len(doc + origin, len - origin);
        size_t loc = rnd((unsigned)total_u + 2), l = rnd(5);
        size_t s, e;
        int ok = btn_ti_range_to_bytes(&ed, loc, l, &s, &e);
        CHECK(ok == (loc + l <= total_u), "range validity (iter %d)", iter);
        if (ok) {
            CHECK(editor_utf8_seq_start(&ed, s) == s && (e == len || editor_utf8_seq_start(&ed, e) == e), "range on char boundaries");
            CHECK(btn_ti_utf16_len(doc + origin, s - origin) >= loc && btn_ti_utf16_len(doc + origin, s - origin) <= loc + 1, "start units");
        }
        size_t aloc, n;
        uint16_t *u = btn_ti_substring(&ed, loc, l, &aloc, &n);
        if (loc <= total_u) {
            CHECK(u != NULL, "substring exists (iter %d)", iter);
            if (u) CHECK(n <= l + 1 && aloc >= loc, "substring size");
        }
        free(u);
    }
    editor_free(&ed);
}

static void test_glue(void) {
    size_t sl; long a, b, c, d;

    /* Tottaste: ´ (vorlaeufig), dann e -> e mit Akut */
    reset_doc("x", 1);
    ti_set_marked_text("\xC2\xB4", 1, 0, -1, 0);
    CHECK(doc_is("x") && g_marked.len == 2, "dead key: marked text not in buffer");
    ti_query(&a, &b, &c, &d);
    CHECK(c == 1 && d == 1 && a == 2 && b == 0, "markedRange (1,1), selectedRange after it");
    ti_insert_text("\xC3\xA9", -1, 0);
    CHECK(doc_is("x\xC3\xA9") && g_marked.len == 0 && g_doc.cursor == 3, "dead key + e = e acute");
    ti_query(&a, &b, &c, &d);
    CHECK(c == -1 && a == 2, "no marked text afterwards, caret after it");

    /* Pinyin: n, ni, ni h, dann Kandidat -> ein Undo-Schritt */
    reset_doc("", 0);
    ti_set_marked_text("n", 1, 0, -1, 0);
    ti_set_marked_text("ni", 2, 0, -1, 0);
    ti_set_marked_text("ni h", 4, 0, -1, 0);
    CHECK(doc_is("") && g_marked.sel_start == 4, "composition stays out of the buffer");
    ti_insert_text("\xE4\xBD\xA0\xE5\xA5\xBD", -1, 0); /* ni hao */
    CHECK(doc_is("\xE4\xBD\xA0\xE5\xA5\xBD"), "committed hanzi");
    editor_undo(&g_doc);
    CHECK(doc_is(""), "one undo removes the committed text");

    /* Abbruch (Escape in der Eingabemethode): leerer vorlaeufiger Text */
    reset_doc("ab", 1);
    ti_set_marked_text("k", 1, 0, -1, 0);
    ti_set_marked_text("", 0, 0, -1, 0);
    CHECK(doc_is("ab") && g_marked.len == 0, "cancelled composition leaves the buffer alone");

    /* Eingabe ueber eine Selektion ersetzt sie */
    reset_doc("abc", 1);
    editor_set_cursor(&g_doc, 2, 1);
    ti_set_marked_text("\xC2\xB4", 1, 0, -1, 0);
    CHECK(doc_is("ac") && g_doc.cursor == 1, "composition deletes the selection");
    ti_insert_text("\xC3\xA9", -1, 0);
    CHECK(doc_is("a\xC3\xA9" "c"), "and the committed text takes its place");

    /* Akzent-Menue beim Gedrueckthalten: e tippen, dann e-Akut ersetzt das Zeichen davor */
    reset_doc("q", 1);
    ti_insert_text("e", -1, 0);
    ti_query(&a, &b, &c, &d);
    CHECK(a == 2, "caret after typed e");
    ti_insert_text("\xC3\xA9", a - 1, 1);
    CHECK(doc_is("q\xC3\xA9") && g_doc.cursor == 3, "press-and-hold replaces the previous character");
    ti_insert_text("!", 99, 1);
    CHECK(doc_is("q\xC3\xA9!"), "invalid replacement range is ignored");

    /* Emoji-Palette: 4 Bytes, 2 UTF-16-Einheiten */
    reset_doc("", 0);
    ti_insert_text("\xF0\x9F\x98\x80", -1, 0);
    ti_query(&a, &b, &c, &d);
    CHECK(doc_is("\xF0\x9F\x98\x80") && a == 2, "emoji palette insert, caret at unit 2");

    /* unmarkText schreibt fest; commit_marked sagt der Eingabemethode Bescheid */
    reset_doc("", 0);
    ti_set_marked_text("ka", 2, 0, -1, 0);
    ti_unmark_text();
    CHECK(doc_is("ka") && g_marked.len == 0, "unmark commits the marked text");
    g_discards = 0;
    ti_set_marked_text("x", 1, 0, -1, 0);
    commit_marked();
    CHECK(doc_is("kax") && g_discards == 1, "commit_marked inserts and discards the input context");
    commit_marked();
    CHECK(g_discards == 1, "commit_marked without marked text does nothing");

    /* Klammer-Automatik nur fuer genau ein Zeichen */
    reset_doc("", 0);
    ti_insert_text("(abc", -1, 0);
    CHECK(doc_is("(abc"), "multi-char text starting with a bracket is inserted completely");
    reset_doc("", 0);
    ti_insert_text("(", -1, 0);
    CHECK(doc_is("()"), "single bracket still auto-pairs");

    /* Suchfeld: Eingabe landet im Feld, Live-Suche laeuft */
    reset_doc("doc", 3);
    g_focus = BTN_FOCUS_SEARCH;
    g_live_searches = 0;
    ti_set_marked_text("\xC2\xA8", 1, 0, -1, 0);
    ti_insert_text("\xC3\xBC", -1, 0);
    size_t fl;
    char *f = editor_copy_all(&g_search_editor, &fl);
    CHECK(fl == 2 && memcmp(f, "\xC3\xBC", 2) == 0 && g_live_searches >= 1 && doc_is("doc"), "input goes to the focused search field");
    free(f);
    g_focus = BTN_FOCUS_DOCUMENT;

    /* Diktat "neue Zeile" und Text mit Steuerzeichen am Anfang */
    reset_doc("a", 1);
    g_last_on_key[0] = 0;
    ti_insert_text("\n", -1, 0);
    CHECK(strcmp(g_last_on_key, "\r") == 0 && doc_is("a"), "dictated newline goes through on_key as Return");
    g_last_on_key[0] = 0;
    ti_insert_text("\tx\ny", -1, 0);
    CHECK(doc_is("a\tx\ny") && g_last_on_key[0] == 0, "text starting with a control character is inserted completely");

    /* Textabfrage mit vorlaeufigem Text: "X" + "ni" (vorlaeufig) + "YZ" */
    reset_doc("XYZ", 1);
    ti_set_marked_text("ni", 2, 0, -1, 0);
    long al2;
    size_t n2;
    uint16_t *v = ti_substring(0, 5, &al2, &n2);
    CHECK(v && n2 == 5 && v[0] == 'X' && v[1] == 'n' && v[2] == 'i' && v[3] == 'Y' && v[4] == 'Z', "substring sees marked text at the caret");
    free(v);
    v = ti_substring(1, 2, &al2, &n2);
    CHECK(v && n2 == 2 && v[0] == 'n' && v[1] == 'i', "substring of markedRange returns the marked text");
    free(v);
    v = ti_substring(2, 3, &al2, &n2);
    CHECK(v && n2 == 3 && v[0] == 'i' && v[1] == 'Y' && v[2] == 'Z', "substring across the end of the marked text");
    free(v);
    v = ti_substring(3, 2, &al2, &n2);
    CHECK(v && n2 == 2 && v[0] == 'Y' && v[1] == 'Z', "text after the caret is shifted by the marked length");
    free(v);
    CHECK(ti_substring(6, 1, &al2, &n2) == NULL, "beyond the end: NULL");
    ti_insert_text("\xE4\xBD\xA0", -1, 0);

    /* substring ueber den Klebecode */
    reset_doc("hello", 5);
    long al;
    uint16_t *u = ti_substring(1, 3, &al, &sl);
    CHECK(u && sl == 3 && al == 1 && u[0] == 'e' && u[2] == 'l', "ti_substring");
    free(u);
    CHECK(ti_substring(-1, 3, &al, &sl) == NULL, "NSNotFound location -> NULL");
}

int main(void) {
    editor_init(&g_doc);
    editor_init(&g_search_editor);
    editor_init(&g_replace_editor);
    editor_set_single_line(&g_search_editor, 1);
    editor_set_single_line(&g_replace_editor, 1);
    test_units();
    test_origin_ranges();
    test_fuzz();
    test_glue();
    btn_marked_clear(&g_marked);
    editor_free(&g_doc);
    editor_free(&g_search_editor);
    editor_free(&g_replace_editor);
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
