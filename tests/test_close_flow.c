/* Schliessen/Beenden: erst alle Speichern-Aktionen, dann "Nicht sichern" -
 * ein Abbruch laesst nichts verloren gehen (should_close aus main.c). */
#include <stdio.h>
#include <stddef.h>

#include "editor.h"
#include "eol.h"

/* Echtes Document samt doc_is_dirty()/mark_doc_saved() aus main.c, dazu
 * Stubs fuer den Rest dessen, was should_close() anfasst. */
#include "filestamp.h"
#include "doc_extracted.h"
#define MAX_TABS 20

static Document g_docs[MAX_TABS];
static int g_doc_count = 0;
static int g_active_doc = 0;
static void switch_to_tab(int idx) { g_active_doc = idx; }
static int g_commits = 0;
static void commit_marked(void) { g_commits++; } /* laufende Eingabe festschreiben */
static void btn_app_request_redraw(void) {}
static const char *doc_display_name(Document *d) { (void)d; return "doc"; }
static int g_discards = 0;
static void discard_recovery(Document *d) { (void)d; g_discards++; } /* Wiederherstellungsdatei loeschen */

/* Skript: Antwort des Ungesichert-Dialogs pro Tab, Ergebnis des Speicherns pro Tab. */
static int g_alert_choice[MAX_TABS];
static int g_save_result[MAX_TABS];
static int g_save_calls = 0;

static int btn_show_unsaved_changes_alert(const char *name) {
    (void)name;
    return g_alert_choice[g_active_doc];
}
static int perform_save_doc(Document *d, int force) {
    (void)force;
    g_save_calls++;
    int idx = (int)(d - g_docs);
    if (g_save_result[idx]) {
        mark_doc_saved(d);
    }
    return g_save_result[idx];
}

#include "close_extracted.h"   /* should_close() verbatim aus main.c */

static int failures = 0;
static void check(int cond, const char *label) {
    printf("%s [%s]\n", cond ? "ok  " : "FAIL", label);
    if (!cond) failures++;
}

static void setup_two_dirty(void) {
    g_doc_count = 2;
    g_active_doc = 0;
    g_docs[0].editor.edit_seq = 5; g_docs[0].saved_edit_seq = 0;
    g_docs[1].editor.edit_seq = 7; g_docs[1].saved_edit_seq = 0;
    g_save_calls = 0;
}

int main(void) {
    /* Fix 4, Kernszenario: A = Nicht sichern, B = Sichern -> Abbruch.
     * Vorher: A bereits als sauber markiert, obwohl das Fenster offen bleibt. */
    setup_two_dirty();
    g_alert_choice[0] = 2; g_alert_choice[1] = 1;
    g_save_result[1] = 0;
    int r = should_close();
    check(r == 0, "S1 cancelled save -> should_close returns 0");
    check(doc_is_dirty(&g_docs[0]), "S1 'Don't Save' tab A is STILL dirty (not silently marked clean)");
    check(g_active_doc == 0, "S1 active tab restored");
    check(g_discards == 0, "S1 cancelled close keeps the recovery files");

    /* Beide erfolgreich: A markiert, B gespeichert */
    setup_two_dirty();
    g_alert_choice[0] = 2; g_alert_choice[1] = 1;
    g_save_result[1] = 1;
    r = should_close();
    check(r == 1, "S2 all ok -> 1");
    check(g_discards == 2, "S2 closing: recovery files of both tabs removed");
    check(!doc_is_dirty(&g_docs[0]), "S2 'Don't Save' tab A marked clean");
    check(!doc_is_dirty(&g_docs[1]), "S2 saved tab B clean");

    /* Nutzer bricht schon den Dialog von A ab: nichts passiert */
    setup_two_dirty();
    g_alert_choice[0] = 0;
    r = should_close();
    check(r == 0 && doc_is_dirty(&g_docs[0]) && doc_is_dirty(&g_docs[1]) && g_save_calls == 0,
          "S3 cancel in dialog -> nothing saved, nothing marked");
    check(g_discards == 2, "S3 cancelled: recovery files kept");

    /* Reihenfolge umgekehrt: A = Sichern (scheitert), B = Nicht sichern */
    setup_two_dirty();
    g_alert_choice[0] = 1; g_alert_choice[1] = 2;
    g_save_result[0] = 0;
    r = should_close();
    check(r == 0 && doc_is_dirty(&g_docs[1]), "S4 failing save first -> later 'Don't Save' tab still dirty");

    /* Kein dirty Tab: sofort 1 */
    g_doc_count = 1; g_docs[0].editor.edit_seq = 3; g_docs[0].saved_edit_seq = 3;
    check(should_close() == 1, "S5 clean doc -> 1 without dialog");
    check(g_commits > 0, "S5 should_close commits a running input-method composition first");

    /* Nur das Zeilenende umgestellt (Inhalt unveraendert): gilt als
     * ungesichert, "Nicht sichern" markiert es sauber. */
    g_doc_count = 1; g_active_doc = 0; g_save_calls = 0;
    g_docs[0].eol = BTN_EOL_CRLF; g_docs[0].saved_eol = BTN_EOL_LF;
    check(doc_is_dirty(&g_docs[0]), "S6 line-ending change alone makes the doc dirty");
    g_alert_choice[0] = 2;
    check(should_close() == 1 && !doc_is_dirty(&g_docs[0]), "S6 'Don't Save' marks the line-ending change clean");
    g_docs[0].eol_raw = 0; g_docs[0].saved_eol_raw = 1;
    check(doc_is_dirty(&g_docs[0]), "S7 converting a mixed file (raw -> uniform) makes the doc dirty");

    printf(failures ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
