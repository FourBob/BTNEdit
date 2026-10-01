/* Suche ueber alle Tabs ("⧉"-Umschalter): Weitersuchen springt in den
 * naechsten/vorigen Tab mit Treffer, Status zaehlt alle Tabs, "Alle
 * ersetzen" ersetzt in allen Tabs (Binaer-Tabs im Hintergrund nicht),
 * Ersetzen+Weiter ersetzt im richtigen Tab. Funktionen verbatim aus main.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include "editor.h"
#include "eol.h"
#include "filestamp.h"
#include "strings.h"
#include "doc_extracted.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---- Zustand aus main.c ---- */
static Editor g_search_editor, g_replace_editor;
static int g_search_regex = 0, g_search_case_sensitive = 0, g_search_whole_word = 0, g_search_all_tabs = 0;
#include "regex_extracted.h"
#include "expensive_extracted.h"
#include "replsel_extracted.h"
#define MAX_TABS 20
static Document g_docs[MAX_TABS];
static int g_doc_count = 1, g_active_doc = 0;
static char g_search_status[128];
#define BTN_MAX_SEARCH_MATCHES_ 5000
static size_t g_match_starts[BTN_MAX_SEARCH_MATCHES_], g_match_ends[BTN_MAX_SEARCH_MATCHES_];
static size_t g_other_starts[BTN_MAX_SEARCH_MATCHES_], g_other_ends[BTN_MAX_SEARCH_MATCHES_];
static size_t g_match_count = 0, g_match_edit_seq = 0, g_search_anchor = 0;
static int g_switches = 0, g_keep_find = -1, g_redraws = 0;
static struct {
    unsigned id;
    size_t seq, count;
} g_tab_counts[MAX_TABS];
static int g_tab_counts_n = 0;
static char *g_tab_counts_key = NULL;
static size_t g_tab_counts_key_len = 0;
static Document *active_doc(void) { return &g_docs[g_active_doc]; }
static void switch_to_tab_ex(int idx, int keep_find) { g_switches++; g_keep_find = keep_find; g_active_doc = idx; }
static void sync_scroll_to_cursor(void) {}
static void sync_window_state(void) {}
static void btn_app_request_redraw(void) { g_redraws++; }
#include "search_tabs_extracted.h"

static void set_text(int tab, const char *s) {
    editor_set_text(&g_docs[tab].editor, s, strlen(s));
    editor_set_cursor(&g_docs[tab].editor, 0, 0);
}
static void set_query(const char *q) { editor_set_text(&g_search_editor, q, strlen(q)); }
static int text_is(int tab, const char *want) {
    size_t l;
    char *t = editor_copy_all(&g_docs[tab].editor, &l);
    int ok = l == strlen(want) && memcmp(t, want, l) == 0;
    free(t);
    return ok;
}
static int sel_is(size_t a, size_t b) {
    Editor *ed = &active_doc()->editor;
    return editor_selection_start(ed) == a && editor_selection_end(ed) == b;
}

int main(void) {
    editor_init(&g_search_editor);
    editor_init(&g_replace_editor);
    editor_set_single_line(&g_search_editor, 1);
    editor_set_single_line(&g_replace_editor, 1);
    for (int i = 0; i < MAX_TABS; i++) {
        editor_init(&g_docs[i].editor);
        g_docs[i].recovery_id = (unsigned)i + 1;
    }
    g_doc_count = 4;
    set_text(0, "foo bar foo");
    set_text(1, "nothing here");
    set_text(2, "a foo");
    set_text(3, "foo foo foo");
    set_query("foo");

    /* aus: Weitersuchen laeuft im aktiven Tab herum */
    CHECK(perform_find(1) && sel_is(0, 3) && g_active_doc == 0, "first match");
    CHECK(perform_find(1) && sel_is(8, 11), "second match");
    CHECK(perform_find(1) && sel_is(0, 3) && g_active_doc == 0 && g_switches == 0, "off: wraps within the tab");
    CHECK(strcmp(g_search_status, "Match 1 of 2") == 0, "off: status only for this tab (%s)", g_search_status);

    /* an: am Ende in den naechsten Tab mit Treffer (Tab 1 hat keinen) */
    g_search_all_tabs = 1;
    CHECK(perform_find(1) && sel_is(8, 11) && g_active_doc == 0, "on: still the next match in this tab first");
    char want[128];
    snprintf(want, sizeof want, "Match 2 of 2%s", " · 6 total");
    CHECK(strcmp(g_search_status, want) == 0, "status counts all tabs (%s)", g_search_status);
    CHECK(perform_find(1) && g_active_doc == 2 && g_keep_find == 1 && sel_is(2, 5), "end of tab: jumps to the next tab with a match, keeps the find bar");
    CHECK(g_match_count == 1 && g_match_starts[0] == 2, "match list belongs to the new tab");
    CHECK(perform_find(1) && g_active_doc == 3 && sel_is(0, 3), "next tab, its first match");
    perform_find(1);
    perform_find(1);
    CHECK(perform_find(1) && g_active_doc == 0 && sel_is(0, 3), "after the last tab: around to the first");
    /* rueckwaerts: in den vorigen Tab, dort der letzte Treffer */
    CHECK(perform_find(0) && g_active_doc == 3 && sel_is(8, 11), "backwards: previous tab, its last match");
    CHECK(perform_find(0) && g_active_doc == 3 && sel_is(4, 7), "backwards within the tab");

    /* kein Treffer im aktiven Tab */
    g_active_doc = 1;
    editor_set_cursor(&g_docs[1].editor, 3, 0);
    CHECK(perform_find(1) && g_active_doc == 2, "no match here: next tab with one");
    /* nur im aktiven Tab Treffer: Umlauf wie bisher */
    set_query("nothing");
    g_active_doc = 1;
    editor_set_cursor(&g_docs[1].editor, 5, 0);
    int switches = g_switches;
    CHECK(perform_find(1) && g_active_doc == 1 && sel_is(0, 7) && g_switches == switches, "matches only here: wraps within the tab");
    set_query("zzz");
    CHECK(!perform_find(1) && g_active_doc == 1 && strcmp(g_search_status, "Not found · 0 total") == 0,
          "nowhere: not found (%s)", g_search_status);

    /* ein Tab: keine Summe, kein Wechsel */
    g_doc_count = 1;
    g_active_doc = 0;
    set_query("foo");
    editor_set_cursor(&g_docs[0].editor, 9, 0);
    CHECK(perform_find(1) && sel_is(0, 3) && strcmp(g_search_status, "Match 1 of 2") == 0, "single tab: as before (%s)", g_search_status);
    g_doc_count = 4;

    /* Live-Suche wechselt nie den Tab, zeigt aber die Summe */
    g_active_doc = 1;
    switches = g_switches;
    set_query("foo");
    perform_live_search();
    CHECK(g_active_doc == 1 && g_switches == switches && strcmp(g_search_status, "Not found · 6 total") == 0,
          "live search: stays, shows where else (%s)", g_search_status);

    /* Aenderung in einem Hintergrund-Tab: gemerkte Trefferzahl gilt nicht mehr */
    set_text(2, "a foo foo");
    perform_live_search();
    CHECK(strcmp(g_search_status, "Not found · 7 total") == 0, "background tab edited: counted again (%s)", g_search_status);
    set_text(2, "a foo");
    /* Live-Suche: andere Tabs zusammen ueber 2 MB -> ohne Summe */
    size_t big = BTN_LIVE_SEARCH_MAX_DOC_LEN + 1;
    char *huge = malloc(big + 1);
    memset(huge, 'x', big);
    huge[big] = 0;
    set_text(4, huge);
    g_doc_count = 5;
    perform_live_search();
    CHECK(strcmp(g_search_status, "Not found") == 0, "live search: no total when the other tabs are too big (%s)", g_search_status);
    editor_set_cursor(&g_docs[1].editor, 0, 0);
    g_active_doc = 1;
    perform_find(1);
    CHECK(strstr(g_search_status, " · 6 total") != NULL, "Return still shows it (%s)", g_search_status);
    g_doc_count = 4;
    free(huge);
    /* Binaer-Tab im Hintergrund: weder angesprungen noch gezaehlt */
    g_docs[2].binary = 1;
    g_active_doc = 1;
    editor_set_cursor(&g_docs[1].editor, 0, 0);
    CHECK(perform_find(1) && g_active_doc == 3 && strstr(g_search_status, " · 5 total"),
          "binary background tab skipped and not counted (%s)", g_search_status);
    g_docs[2].binary = 0;

    /* Ersetzen + Weiter: der Treffer liegt in einem anderen Tab */
    editor_set_text(&g_replace_editor, "X", 1);
    g_active_doc = 1;
    editor_set_cursor(&g_docs[1].editor, 0, 0);
    perform_replace_current();
    CHECK(text_is(2, "a X") && text_is(1, "nothing here") && g_active_doc == 3 && sel_is(0, 3),
          "replace: in the tab with the match (not the old one), then on to the next match in the next tab");

    /* Alle ersetzen in allen Tabs, Binaer-Tab im Hintergrund nicht */
    set_text(2, "a foo");
    g_docs[3].binary = 1;
    g_active_doc = 0;
    perform_replace_all();
    CHECK(text_is(0, "X bar X") && text_is(2, "a X") && text_is(3, "foo foo foo") && text_is(1, "nothing here"),
          "replace all: every tab except the binary one");
    CHECK(strcmp(g_search_status, "3 replaced in 2 tabs") == 0, "status (%s)", g_search_status);
    editor_undo(&g_docs[0].editor);
    CHECK(text_is(0, "foo bar foo") && text_is(2, "a X"), "one undo step per tab");
    g_docs[3].binary = 0;
    g_search_all_tabs = 0;
    set_text(2, "a foo");
    perform_replace_all();
    CHECK(text_is(0, "X bar X") && text_is(2, "a foo") && strcmp(g_search_status, "2 replaced") == 0,
          "off: only the active tab (%s)", g_search_status);

    /* Mehr Treffer als die Liste fasst (BTN_MAX_SEARCH_MATCHES): Weitersuchen
     * erreicht auch die dahinter und springt nicht zurueck */
    {
        size_t n = 6000;
        char *many = malloc(2 * n);
        for (size_t i = 0; i < n; i++) {
            many[2 * i] = 'x';
            many[2 * i + 1] = ' ';
        }
        g_search_all_tabs = 0;
        g_active_doc = 0;
        editor_set_text(&g_docs[0].editor, many, 2 * n);
        free(many);
        set_query("x");
        editor_set_cursor(&g_docs[0].editor, 11000, 0); /* bei Treffer 5501 */
        CHECK(perform_find(1) && sel_is(11000, 11001), "beyond the cap: the match at the cursor");
        CHECK(strcmp(g_search_status, "Match after the first 5000") == 0, "status beyond the cap (%s)", g_search_status);
        CHECK(perform_find(1) && sel_is(11002, 11003), "beyond the cap: next");
        CHECK(perform_find(0) && sel_is(11000, 11001), "beyond the cap: previous");
        editor_set_cursor(&g_docs[0].editor, 2 * n, 0);
        CHECK(perform_find(1) && sel_is(0, 1), "past the last: around to the first");
        CHECK(strcmp(g_search_status, "Match 1 of 5000+") == 0, "status in the capped list (%s)", g_search_status);
        CHECK(perform_find(0) && sel_is(11998, 11999), "before the first: around to the very last (not #5000)");
        g_search_all_tabs = 1;
        editor_set_cursor(&g_docs[0].editor, 0, 0);
        set_text(3, "x");
        g_active_doc = 3;
        editor_set_cursor(&g_docs[3].editor, 0, 0);
        CHECK(perform_find(0), "all tabs backwards into a capped tab");
        CHECK(g_active_doc == 0 && sel_is(11998, 11999), "lands on its very last match (tab %d)", g_active_doc);
        g_search_all_tabs = 0;
        /* leere Treffer hinter dem Deckel: 6000 Zeilen "a", Regex ^ */
        many = malloc(2 * n);
        for (size_t i = 0; i < n; i++) {
            many[2 * i] = 'a';
            many[2 * i + 1] = '\n';
        }
        g_active_doc = 0;
        editor_set_text(&g_docs[0].editor, many, 2 * n);
        free(many);
        g_search_regex = 1;
        set_query("^");
        editor_set_cursor(&g_docs[0].editor, 11000, 0);
        CHECK(perform_find(1) && sel_is(11002, 11002), "^ beyond the cap: on to the next line start");
        CHECK(perform_find(1) && sel_is(11004, 11004), "and the next");
        g_search_regex = 0;
        set_text(0, "foo bar foo");
        set_text(3, "foo foo foo");
    }

    /* Leere Regex-Treffer: Weitersuchen kommt voran (Zeilenanfaenge 0, 2, 4) */
    g_search_regex = 1;
    set_query("^");
    g_active_doc = 0;
    set_text(0, "a\nb\nc");
    editor_set_cursor(&g_docs[0].editor, 0, 0);
    CHECK(perform_find(1) && sel_is(2, 2), "^: from a line start on to the next one");
    CHECK(perform_find(1) && sel_is(4, 4), "^: and the next");
    CHECK(perform_find(1) && sel_is(0, 0), "^: wraps to the first");
    CHECK(perform_find(0) && sel_is(4, 4), "^ backwards: the last");
    CHECK(perform_find(0) && sel_is(2, 2), "^ backwards: the previous");
    set_query("$");
    editor_set_cursor(&g_docs[0].editor, 0, 0);
    CHECK(perform_find(1) && sel_is(1, 1) && perform_find(1) && sel_is(3, 3) && perform_find(1) && sel_is(5, 5),
          "$: every line end in turn");
    set_query("^$");
    set_text(0, "\nx");
    set_text(1, "a\n\nb");
    editor_set_cursor(&g_docs[0].editor, 0, 0);
    g_search_all_tabs = 1;
    g_active_doc = 1;
    editor_set_cursor(&g_docs[1].editor, 2, 0);
    perform_find(1);
    CHECK(g_active_doc == 0 && sel_is(0, 0), "all tabs: the only empty match here moves on to the next tab (tab %d)", g_active_doc);
    g_search_all_tabs = 0;
    g_search_regex = 0;

    for (int i = 0; i < MAX_TABS; i++) {
        editor_free(&g_docs[i].editor);
    }
    editor_free(&g_search_editor);
    editor_free(&g_replace_editor);
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
