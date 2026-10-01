/* Standard-Tastenkuerzel aus main.c: Weitersuchen (Cmd+G / Shift+Cmd+G),
 * Auswahl fuer Suche (Cmd+E, auch Vorbelegung bei Cmd+F) und Tab-Wechsel
 * (Ctrl+Tab, Shift+Cmd+] / [) - mit Stubs fuer Suche und Fenster. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "eol.h"
#include "filestamp.h"
#include "doc_extracted.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

#define MAX_TABS 20
static Document g_docs[MAX_TABS];
static int g_doc_count = 1, g_active_doc = 0;
static Editor g_search_editor;
static int g_find_bar_visible = 0;
static size_t g_match_count = 0;
static char g_search_status[128];
static int g_open_calls, g_find_calls, g_find_forward, g_find_result, g_beeps, g_live, g_switches, g_switch_to;
static Document *active_doc(void) { return &g_docs[g_active_doc]; }
static void open_find_bar(void) { g_open_calls++; }
static int perform_find(int forward) { g_find_calls++; g_find_forward = forward; g_match_count = 7; return g_find_result; }
static void btn_beep(void) { g_beeps++; }
static void perform_live_search(void) { g_live++; }
static void switch_to_tab(int idx) { g_switches++; g_switch_to = idx; g_active_doc = idx; }
#include "shortcuts_extracted.h"

static int search_is(const char *s, size_t n) {
    size_t l;
    char *t = editor_copy_all(&g_search_editor, &l);
    int ok = l == n && memcmp(t, s, n) == 0;
    free(t);
    return ok;
}

int main(void) {
    editor_init(&g_search_editor);
    editor_set_single_line(&g_search_editor, 1);
    for (int i = 0; i < MAX_TABS; i++) editor_init(&g_docs[i].editor);
    Editor *doc = &g_docs[0].editor;

    /* Tab-Umlauf */
    CHECK(next_tab_index(0, 3, 1) == 1 && next_tab_index(2, 3, 1) == 0 && next_tab_index(0, 3, -1) == 2, "wrap-around");
    CHECK(next_tab_index(0, 1, 1) == 0 && next_tab_index(5, 0, 1) == 0, "one tab / no tab");
    g_doc_count = 1;
    cycle_tab(1);
    CHECK(g_switches == 0, "single tab: nothing switched (find bar stays)");
    g_doc_count = 3; g_active_doc = 2;
    cycle_tab(1);
    CHECK(g_switches == 1 && g_switch_to == 0, "next tab wraps to first");
    cycle_tab(-1);
    CHECK(g_switch_to == 2, "previous tab wraps to last");
    g_active_doc = 0; g_doc_count = 1;

    /* Cmd+E: einzeilige Selektion, auch mit NUL-Byte */
    editor_set_text(doc, "foo a\0b bar", 11);
    editor_set_cursor(doc, 4, 0);
    editor_set_cursor(doc, 7, 1);
    use_selection_for_find();
    CHECK(search_is("a\0b", 3) && g_beeps == 0, "Cmd+E takes the selection incl. NUL byte");
    CHECK(g_live == 0, "find bar closed: no live search");
    g_find_bar_visible = 1;
    editor_set_cursor(doc, 0, 0);
    editor_set_cursor(doc, 3, 1);
    use_selection_for_find();
    CHECK(search_is("foo", 3) && g_live == 1 && editor_has_selection(&g_search_editor), "find bar open: live search, field selected");
    g_find_bar_visible = 0;
    editor_set_text(doc, "x\ny", 3);
    editor_set_cursor(doc, 0, 0);
    editor_set_cursor(doc, 3, 1);
    use_selection_for_find();
    CHECK(search_is("foo", 3) && g_beeps == 1, "multi-line selection: rejected with beep, search text kept");
    editor_set_cursor(doc, 1, 0);
    use_selection_for_find();
    CHECK(g_beeps == 2, "no selection: beep");

    /* Cmd+G */
    editor_set_text(&g_search_editor, "", 0);
    find_next_from_menu(1);
    CHECK(g_open_calls == 1 && g_find_calls == 0, "empty search text: open the find bar");
    editor_set_text(&g_search_editor, "x", 1);
    g_find_result = 1;
    find_next_from_menu(0);
    CHECK(g_find_calls == 1 && g_find_forward == 0 && g_beeps == 2, "Shift+Cmd+G searches backwards");
    CHECK(g_match_count == 0, "find bar closed: no match highlighting");
    g_find_bar_visible = 1;
    find_next_from_menu(1);
    CHECK(g_find_forward == 1 && g_match_count == 7, "find bar open: highlighting stays");
    g_find_result = 0;
    find_next_from_menu(1);
    CHECK(g_beeps == 3, "not found: beep");

    for (int i = 0; i < MAX_TABS; i++) editor_free(&g_docs[i].editor);
    editor_free(&g_search_editor);
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
