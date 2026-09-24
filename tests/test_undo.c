/* Undo-Gruppen (Alle ersetzen, Klammer-Umschliessen, einzelnes Ersetzen) und
 * Fuzzing mit simulierten Allokationsfehlern. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "replsel_extracted.h"

static unsigned long rng = 99;
static unsigned rnd(unsigned n) { rng = rng * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)((rng >> 33) % n); }
static unsigned fail_permille = 0;
static long injected = 0;
void *inj_malloc(size_t n) { if (fail_permille && rnd(1000) < fail_permille) { injected++; return NULL; } return malloc(n); }
void *inj_realloc(void *p, size_t n) { if (fail_permille && rnd(1000) < fail_permille) { injected++; return NULL; } return realloc(p, n); }

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 20) { printf("FAIL: " __VA_ARGS__); printf("\n"); } fails++; } } while (0)

static char *text_of(Editor *ed, size_t *len) { return editor_copy_all(ed, len); }
static int eq(Editor *ed, const char *s) { size_t l; char *t = text_of(ed, &l); int r = l == strlen(s) && memcmp(t, s, l) == 0; free(t); return r; }
static void set(Editor *ed, const char *s) { editor_set_text(ed, s, strlen(s)); }
static void type(Editor *ed, const char *s) { editor_insert_text(ed, s, strlen(s)); }

/* Nachbau der perform_replace_all()-Schleife mit den echten Editor-Aufrufen. */
static void replace_all(Editor *ed, const char *needle, const char *repl) {
    size_t len; char *orig = text_of(ed, &len);
    size_t nl = strlen(needle), rl = strlen(repl);
    long delta = 0;
    editor_begin_undo_group(ed);
    for (size_t i = 0; i + nl <= len; ) {
        if (memcmp(orig + i, needle, nl) == 0) {
            editor_set_cursor(ed, (size_t)((long)i + delta), 0);
            editor_set_cursor(ed, (size_t)((long)(i + nl) + delta), 1);
            if (rl == 0) editor_delete_selection(ed); else editor_insert_text(ed, repl, rl);
            delta += (long)rl - (long)nl;
            i += nl;
        } else i++;
    }
    editor_end_undo_group(ed);
    free(orig);
}

static void test_groups(void) {
    Editor ed; editor_init(&ed);
    set(&ed, "a1 a2 a3");
    replace_all(&ed, "a", "\xC3\xA4\xC3\xA4");
    CHECK(eq(&ed, "\xC3\xA4\xC3\xA4" "1 \xC3\xA4\xC3\xA4" "2 \xC3\xA4\xC3\xA4" "3"), "replaced");
    printf("records after 3 replacements: %zu\n", ed.undo.count);
    editor_undo(&ed);
    CHECK(eq(&ed, "a1 a2 a3"), "one Cmd+Z undoes all 3 replacements");
    CHECK(ed.undo.pos == 0, "nothing left to undo");
    editor_redo(&ed);
    CHECK(eq(&ed, "\xC3\xA4\xC3\xA4" "1 \xC3\xA4\xC3\xA4" "2 \xC3\xA4\xC3\xA4" "3"), "one redo restores all");
    CHECK(ed.undo.pos == ed.undo.count, "redo at end");

    /* Tippen nach der Gruppe ist ein eigener Schritt */
    editor_set_cursor(&ed, editor_length(&ed), 0);
    type(&ed, "x"); type(&ed, "y");
    editor_undo(&ed);
    CHECK(eq(&ed, "\xC3\xA4\xC3\xA4" "1 \xC3\xA4\xC3\xA4" "2 \xC3\xA4\xC3\xA4" "3"), "typing after group undone separately");
    editor_undo(&ed);
    CHECK(eq(&ed, "a1 a2 a3"), "then the group");

    /* Tippen VOR der Gruppe wird nicht hineingezogen (Coalescing) */
    set(&ed, "b a");
    editor_set_cursor(&ed, 3, 0);
    type(&ed, "a");               /* "b aa" */
    replace_all(&ed, "a", "c");   /* "b cc" */
    CHECK(eq(&ed, "b cc"), "replace with preceding typing");
    editor_undo(&ed);
    CHECK(eq(&ed, "b aa"), "group undo keeps the earlier typed 'a'");
    editor_undo(&ed);
    CHECK(eq(&ed, "b a"), "typed 'a' is its own step");

    /* Leere Ersetzung (Loeschen) und verschachtelte Gruppen */
    set(&ed, "xaxbxc");
    editor_begin_undo_group(&ed);
    replace_all(&ed, "x", "");    /* verschachtelt: innere Gruppe zaehlt nicht */
    editor_set_cursor(&ed, 0, 0);
    type(&ed, "Z");
    editor_end_undo_group(&ed);
    CHECK(eq(&ed, "Zabc"), "nested result");
    editor_undo(&ed);
    CHECK(eq(&ed, "xaxbxc"), "nested groups undo as one");
    editor_redo(&ed);
    CHECK(eq(&ed, "Zabc"), "nested redo as one");

    /* Zwei Gruppen hintereinander bleiben getrennt */
    set(&ed, "aaa");
    replace_all(&ed, "a", "b");
    replace_all(&ed, "b", "c");
    editor_undo(&ed);
    CHECK(eq(&ed, "bbb"), "adjacent groups separate (1)");
    editor_undo(&ed);
    CHECK(eq(&ed, "aaa"), "adjacent groups separate (2)");

    /* Klammer-Umschliessen einer Selektion: ein Undo-Schritt */
    set(&ed, "x abc y");
    editor_set_cursor(&ed, 2, 0); editor_set_cursor(&ed, 5, 1);
    editor_handle_bracket_key(&ed, '(');
    CHECK(eq(&ed, "x (abc) y") && ed.anchor == 3 && ed.cursor == 6, "wrap result, selection kept");
    editor_undo(&ed);
    CHECK(eq(&ed, "x abc y"), "one Cmd+Z undoes the whole wrap");
    CHECK(ed.undo.pos == 0, "wrap left no extra steps");
    editor_redo(&ed);
    CHECK(eq(&ed, "x (abc) y"), "one redo re-wraps");

    /* Einzelnes Ersetzen / Einfuegen ueber Selektion (echte replace_selection()) */
    set(&ed, "foo bar foo");
    editor_set_cursor(&ed, 0, 0); editor_set_cursor(&ed, 3, 1);
    replace_selection(&ed, "X", 1);
    CHECK(eq(&ed, "X bar foo"), "single replace");
    editor_undo(&ed);
    CHECK(eq(&ed, "foo bar foo") && ed.undo.pos == 0, "single replace is one undo step");
    editor_set_cursor(&ed, 4, 0); editor_set_cursor(&ed, 7, 1);
    replace_selection(&ed, "", 0);
    editor_undo(&ed);
    CHECK(eq(&ed, "foo bar foo"), "empty replacement undo");

    /* Keine Treffer: leere Gruppe hinterlaesst nichts */
    set(&ed, "abc");
    replace_all(&ed, "q", "z");
    CHECK(ed.undo.count == 0, "empty group adds no record");
    editor_free(&ed);
}

/* Undo-all + Redo-all muss exakt den aktuellen Stand wiederherstellen. */
static void check_history(Editor *ed, const char *what) {
    size_t l0; char *before = text_of(ed, &l0);
    size_t pos0 = ed->undo.pos, n0 = ed->undo.count;
    unsigned saved = fail_permille; fail_permille = 0;   /* Pruefung selbst ohne Fehler */
    while (ed->undo.pos > 0) editor_undo(ed);
    while (ed->undo.pos < ed->undo.count) editor_redo(ed);
    while (ed->undo.pos > pos0) editor_undo(ed);
    fail_permille = saved;
    size_t l1; char *after = text_of(ed, &l1);
    CHECK(l0 == l1 && memcmp(before, after, l0) == 0 && ed->undo.count == n0, "history consistent after %s", what);
    free(before); free(after);
}

static const char *snips[] = { "a", "b", " ", "\n", "\xC3\xA4", "\xE2\x82\xAC", "(", "hello", "\xF0\x9F\x98\x80", "\t" };
static void fuzz(unsigned permille, int iters, unsigned long seed) {
    rng = seed;
    Editor ed; editor_init(&ed);
    fail_permille = permille;
    int depth = 0;
    for (int it = 0; it < iters; it++) {
        unsigned op = rnd(12);
        size_t len = editor_length(&ed);
        const char *what = "?";
        switch (op) {
        case 0: case 1: case 2: type(&ed, snips[rnd(10)]); what = "insert"; break;
        case 3: editor_delete_backward(&ed); what = "backspace"; break;
        case 4: editor_delete_forward(&ed); what = "delete"; break;
        case 5: editor_set_cursor(&ed, len ? rnd((unsigned)len + 1) : 0, 0); editor_set_cursor(&ed, editor_utf8_seq_start(&ed, ed.cursor), 0); what = "move"; break;
        case 6: editor_set_cursor(&ed, len ? editor_utf8_seq_start(&ed, rnd((unsigned)len + 1)) : 0, 1); what = "select"; break;
        case 7: editor_undo(&ed); what = "undo"; break;
        case 8: editor_redo(&ed); what = "redo"; break;
        case 9: if (depth < 3) { editor_begin_undo_group(&ed); depth++; } what = "begin"; break;
        case 10: if (depth > 0) { editor_end_undo_group(&ed); depth--; } what = "end"; break;
        case 11: editor_handle_bracket_key(&ed, "([{\"'"[rnd(5)]); what = "bracket"; break;
        }
        check_history(&ed, what);
    }
    while (depth-- > 0) editor_end_undo_group(&ed);
    fail_permille = 0;
    editor_free(&ed);
}

int main(void) {
    test_groups();
    printf("groups: %ld checks, %ld failures\n", checks, fails);
    for (unsigned long s = 1; s <= 40; s++) fuzz(0, 400, s);
    printf("fuzz without injection: %ld checks, %ld failures\n", checks, fails);
    for (unsigned long s = 1; s <= 200; s++) fuzz(s % 2 ? 30 : 150, 400, s * 7919);
    printf("fuzz with injected allocation failures (%ld injected): %ld checks, %ld failures\n", injected, checks, fails);
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
