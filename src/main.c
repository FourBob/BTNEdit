/*
 * Reines C: Applikationslogik und Verdrahtung der Callbacks aus dem
 * ObjC-Shim (shim.m) mit der Editor-Engine (editor.c/gapbuffer.c).
 */
#include "shim.h"
#include "render.h"
#include "editor.h"
#include "strings.h"

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define KEYCODE_LEFT           123
#define KEYCODE_RIGHT          124
#define KEYCODE_DOWN           125
#define KEYCODE_UP             126
#define KEYCODE_HOME           115
#define KEYCODE_END            119
#define KEYCODE_FORWARD_DELETE 117
#define KEYCODE_TAB            48

/* Ein offenes Dokument (ein Tab). Jedes hat seinen eigenen Puffer/Undo-
 * Verlauf/Scroll-Zustand - nur das Fenster, das Menue und die Zwischenablage
 * werden zwischen ihnen geteilt. Der Verschiebe-per-memmove-Ansatz in
 * add_tab()/close_tab() ist sicher: Editor haelt keine Zeiger auf sich
 * selbst, nur auf unabhaengige Heap-Allokationen (GapBuffer.data,
 * UndoStack.records), die von einer Struct-Kopie unberuehrt bleiben. */
typedef struct {
    Editor editor;
    char *path;            /* NULL = unbenanntes, neues Dokument */
    size_t saved_edit_seq;
    long scroll_row;
    double scroll_accum;
} Document;

#define MAX_TABS 20
static Document g_docs[MAX_TABS];
static int g_doc_count = 0;
static int g_active_doc = 0;

static CGRect g_bounds = { { 0, 0 }, { 900, 600 } };
static int g_dragging = 0;

static char *g_recent_paths[BTN_MAX_RECENT_FILES]; /* [0] = neuester Eintrag */
static int g_recent_count = 0;

/* Suchen/Ersetzen-Leiste. Die beiden Textfelder sind bewusst keine Mini-
 * Editoren mit Cursor/Selektion (das waere fast eine zweite editor.c) -
 * nur Anhaengen (Tippen) und Loeschen (Backspace) vom Ende her, das reicht
 * fuer einen Suchbegriff voellig aus. */
typedef enum {
    BTN_FOCUS_DOCUMENT,
    BTN_FOCUS_SEARCH,
    BTN_FOCUS_REPLACE
} BtnFocus;

static int g_find_bar_visible = 0;
static BtnFocus g_focus = BTN_FOCUS_DOCUMENT;
static char g_search_query[256] = "";
static char g_replace_text[256] = "";
static int g_search_regex = 0; /* 0 = Literalsuche, 1 = POSIX-Regex (ERE) */
static char g_search_status[128] = "";

static Document *active_doc(void) {
    return &g_docs[g_active_doc];
}

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int doc_is_dirty(Document *d) {
    /* Bewusst ueber edit_seq statt ueber undo.pos: Undo-Coalescing kann
     * pos unveraendert lassen, obwohl sich der Inhalt geaendert hat (siehe
     * editor.h-Kommentar bei edit_seq). */
    return d->editor.edit_seq != d->saved_edit_seq;
}

static int is_dirty(void) {
    return doc_is_dirty(active_doc());
}

static const char *doc_display_name(Document *d) {
    return d->path ? basename_of(d->path) : btn_tr(BTN_STR_UNTITLED);
}

static void set_doc_path(Document *d, const char *path) {
    /* path darf mit d->path identisch sein (z.B. bei einem erneuten
     * "Sichern" auf denselben Pfad) - deshalb erst die Kopie anlegen und
     * danach erst den alten Speicher freigeben, sonst wuerde
     * btn_dup_cstring aus bereits freigegebenem Speicher lesen. */
    char *copy = path ? btn_dup_cstring(path) : NULL;
    free(d->path);
    d->path = copy;
    if (d == active_doc()) {
        btn_set_window_title(doc_display_name(d));
    }
}

static void sync_window_state(void) {
    btn_app_set_document_edited(is_dirty());
}

/* Persistiert als einfache Zeilenliste unter ~/.btnedit_recent statt in
 * NSUserDefaults - main.c bleibt so komplett Cocoa-frei, der Shim muss dafuer
 * keine neue API bekommen. App-global (nicht pro Tab). */
static char *recent_file_list_path(void) {
    const char *home = getenv("HOME");
    if (!home) {
        return NULL;
    }
    size_t len = strlen(home) + strlen("/.btnedit_recent") + 1;
    char *path = malloc(len);
    snprintf(path, len, "%s/.btnedit_recent", home);
    return path;
}

static void recent_files_refresh_menu(void) {
    const char *paths[BTN_MAX_RECENT_FILES];
    for (int i = 0; i < g_recent_count; i++) {
        paths[i] = g_recent_paths[i];
    }
    btn_app_set_recent_files(paths, g_recent_count);
}

static void save_recent_files(void) {
    char *list_path = recent_file_list_path();
    if (!list_path) {
        return;
    }
    FILE *f = fopen(list_path, "w");
    free(list_path);
    if (!f) {
        return;
    }
    for (int i = 0; i < g_recent_count; i++) {
        fprintf(f, "%s\n", g_recent_paths[i]);
    }
    fclose(f);
}

static void load_recent_files(void) {
    char *list_path = recent_file_list_path();
    if (!list_path) {
        return;
    }
    FILE *f = fopen(list_path, "r");
    free(list_path);
    if (!f) {
        return;
    }
    char line[4096];
    while (g_recent_count < BTN_MAX_RECENT_FILES && fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        /* Eintraege, deren Datei inzwischen verschwunden ist, gar nicht erst
         * ins Menu aufnehmen - sonst haette "Zuletzt geoeffnet" tote
         * Eintraege, die beim Anklicken nur eine Fehlermeldung produzieren. */
        if (len == 0 || access(line, R_OK) != 0) {
            continue;
        }
        g_recent_paths[g_recent_count++] = btn_dup_cstring(line);
    }
    fclose(f);
}

static void add_recent_file(const char *path) {
    for (int i = 0; i < g_recent_count; i++) {
        if (strcmp(g_recent_paths[i], path) == 0) {
            free(g_recent_paths[i]);
            memmove(&g_recent_paths[i], &g_recent_paths[i + 1],
                    (size_t)(g_recent_count - i - 1) * sizeof(char *));
            g_recent_count--;
            break;
        }
    }
    if (g_recent_count == BTN_MAX_RECENT_FILES) {
        free(g_recent_paths[g_recent_count - 1]);
        g_recent_count--;
    }
    memmove(&g_recent_paths[1], &g_recent_paths[0], (size_t)g_recent_count * sizeof(char *));
    g_recent_paths[0] = btn_dup_cstring(path);
    g_recent_count++;

    save_recent_files();
    recent_files_refresh_menu();
}

/* Content-Flaeche ist g_bounds abzueglich der Tableiste (und, falls
 * sichtbar, der Suchen-Leiste) oben - dieselbe Rueckgabe geht an
 * btn_render_frame/btn_hit_test/btn_layout_build, sodass Zeichnen, Scrollen
 * und Klick-Trefferpruefung nie auseinanderlaufen. Da nur die Hoehe
 * verkleinert wird (Ursprung bleibt (0,0)), bleiben absolute y-Koordinaten
 * unterhalb der Leisten unveraendert gueltig - eine gesonderte
 * Koordinatentransformation fuer Mausklicks ist nicht noetig. */
static CGRect content_bounds(void) {
    CGRect r = g_bounds;
    r.size.height -= BTN_TAB_BAR_HEIGHT;
    if (g_find_bar_visible) {
        r.size.height -= BTN_FIND_BAR_HEIGHT;
    }
    if (r.size.height < 0) {
        r.size.height = 0;
    }
    return r;
}

static long visible_line_capacity(void) {
    double content_height = content_bounds().size.height - BTN_FOOTER_HEIGHT;
    long n = (long)(content_height / BTN_LINE_HEIGHT);
    return n > 0 ? n : 1;
}

/* Baut das aktuelle Zeilenumbruch-Layout fuer die momentane Fensterbreite;
 * caller muss btn_layout_free(*out_rows) aufrufen. */
static size_t build_current_rows(BtnRow **out_rows) {
    double width = btn_layout_text_width(content_bounds());
    size_t row_count;
    *out_rows = btn_layout_build(&active_doc()->editor, width, &row_count);
    return row_count;
}

/* Klemmt scroll_row des aktiven Dokuments auf [0, row_count - Sichtkapazitaet]
 * - row_count wird uebergeben statt selbst neu gebaut, damit Aufrufer, die
 * das Layout schon haben (z.B. sync_scroll_to_cursor), es nicht ein zweites
 * Mal fuers selbe Aktion aufbauen muessen. */
static void clamp_scroll_to_row_count(long row_count) {
    Document *d = active_doc();
    if (d->scroll_row < 0) {
        d->scroll_row = 0;
    }
    long max_scroll = row_count - visible_line_capacity();
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (d->scroll_row > max_scroll) {
        d->scroll_row = max_scroll;
    }
}

static void clamp_scroll(void) {
    BtnRow *rows;
    long row_count = (long)build_current_rows(&rows);
    btn_layout_free(rows);
    clamp_scroll_to_row_count(row_count);
}

/* Scrollt automatisch nach, damit der Cursor immer sichtbar bleibt -
 * bei Tastatur-Navigation gibt es sonst keinen anderen Weg, ihn wieder
 * ins Bild zu bekommen. */
static void sync_scroll_to_cursor(void) {
    BtnRow *rows;
    size_t row_count = build_current_rows(&rows);
    Document *d = active_doc();
    long cur_row = (long)btn_layout_row_for_offset(rows, row_count, d->editor.cursor);
    btn_layout_free(rows);

    long capacity = visible_line_capacity();
    if (cur_row < d->scroll_row) {
        d->scroll_row = cur_row;
    } else if (cur_row >= d->scroll_row + capacity) {
        d->scroll_row = cur_row - capacity + 1;
    }
    clamp_scroll_to_row_count((long)row_count);
}

static void close_find_bar(void) {
    if (!g_find_bar_visible) {
        return;
    }
    g_find_bar_visible = 0;
    g_focus = BTN_FOCUS_DOCUMENT;
    /* Content-Flaeche wird wieder groesser (siehe content_bounds()) -
     * ohne Neuberechnung koennte der Cursor jetzt in der bisher von der
     * Leiste verdeckten Zeile stehen, ohne dass eine Bewegung stattfand. */
    sync_scroll_to_cursor();
    btn_app_request_redraw();
}

/* Oeffnet die Leiste (oder holt sie einfach wieder in den Fokus, falls
 * schon offen) und uebernimmt eine vorhandene einzeilige Selektion als
 * Suchtext - genau wie Cmd+F das in so gut wie jedem macOS-Editor tut.
 * Mehrzeilige Selektionen werden ignoriert (Zeilenumbrueche/Regex-
 * Sonderzeichen darin ergeben selten einen sinnvollen Suchbegriff). */
static void open_find_bar(void) {
    Editor *ed = &active_doc()->editor;
    if (editor_has_selection(ed)) {
        char *sel = editor_get_selection_text(ed);
        size_t sel_len = strlen(sel);
        if (sel_len < sizeof(g_search_query) && strchr(sel, '\n') == NULL) {
            memcpy(g_search_query, sel, sel_len + 1);
        }
        free(sel);
    }
    g_find_bar_visible = 1;
    g_focus = BTN_FOCUS_SEARCH;
    g_search_status[0] = '\0';
    sync_scroll_to_cursor();
    btn_app_request_redraw();
}

/* Macht idx zum aktiven Tab und bringt Fenstertitel/Ungesichert-Indikator/
 * Scroll-Position auf den Stand dieses Dokuments - noetig, weil waehrend ein
 * anderer Tab aktiv war, weder sein Titel/Dirty-Status im Fenster sichtbar
 * war noch sich seine Scroll-Position an eine zwischenzeitliche
 * Fenstergroessenaenderung angepasst haben kann. Schliesst nebenbei eine
 * offene Suchen-Leiste - deren Zustand (Selektion als aktueller Treffer)
 * bezieht sich sonst auf ein Dokument, das gerade nicht mehr sichtbar ist. */
static void switch_to_tab(int idx) {
    close_find_bar();
    g_active_doc = idx;
    btn_set_window_title(doc_display_name(active_doc()));
    sync_window_state();
    sync_scroll_to_cursor();
}

static void doc_free(Document *d) {
    editor_free(&d->editor);
    free(d->path);
}

/* Rueckgabe: Index des neuen Tabs, oder -1 wenn MAX_TABS erreicht (macht
 * bewusst nicht selbst aktiv - Aufrufer entscheidet, ob/wann umgeschaltet
 * wird). */
static int add_tab(void) {
    if (g_doc_count >= MAX_TABS) {
        return -1;
    }
    Document *d = &g_docs[g_doc_count];
    editor_init(&d->editor);
    d->path = NULL;
    d->saved_edit_seq = d->editor.edit_seq;
    d->scroll_row = 0;
    d->scroll_accum = 0.0;
    return g_doc_count++;
}

/* Ein frisches, noch nie benutztes "Unbenannt"-Dokument - New/Open fuellen
 * so ein bereits offenes leeres Tab statt bei jedem Aufruf ein weiteres
 * anzuhaeufen (entspricht dem, was z.B. VS Code/Windows-Notepad tun). */
static int doc_is_blank(Document *d) {
    return d->path == NULL && !doc_is_dirty(d) && editor_length(&d->editor) == 0;
}

/* Gemeinsame Logik fuer Cmd+N und den "+"-Knopf in der Tableiste. */
static void new_tab_or_reuse_blank(void) {
    if (doc_is_blank(active_doc())) {
        return;
    }
    int idx = add_tab();
    if (idx < 0) {
        fprintf(stderr, "BTNEdit: maximale Anzahl Tabs (%d) erreicht\n", MAX_TABS);
        return;
    }
    switch_to_tab(idx);
}

/* Escaped alle ERE-Sonderzeichen in src, damit eine Literalsuche ueber
 * dieselbe Regex-Engine laufen kann wie eine echte Regex-Suche - vermeidet
 * eine komplett zweite (Vorwaerts-/Rueckwaerts-)Suchimplementierung fuer
 * den Nicht-Regex-Fall. */
static void regex_escape_literal(const char *src, char *out, size_t out_cap) {
    static const char *special = ".^$*+?()[]{}|\\";
    size_t o = 0;
    for (const char *p = src; *p && o + 2 < out_cap; p++) {
        if (strchr(special, *p)) {
            out[o++] = '\\';
        }
        out[o++] = *p;
    }
    out[o] = '\0';
}

static int compile_search_regex(regex_t *re) {
    char pattern[512];
    if (g_search_regex) {
        snprintf(pattern, sizeof(pattern), "%s", g_search_query);
    } else {
        regex_escape_literal(g_search_query, pattern, sizeof(pattern));
    }
    return regcomp(re, pattern, REG_EXTENDED) == 0;
}

/* Sucht in text[0,text_len) ab Position from. forward=1 durchsucht
 * [from,text_len) und wickelt bei wrap=1 zu [0,text_len) zurueck, falls
 * nichts gefunden wurde. forward=0 (Rueckwaertssuche) sammelt alle Treffer
 * im ganzen Dokument einmal vorwaerts (POSIX regexec kennt keine
 * Rueckwaertssuche) und nimmt den letzten VOR from, oder bei wrap=1 den
 * letzten insgesamt (Wrap ans Ende). REG_STARTEND (BSD/Darwin-Erweiterung)
 * erlaubt Start/Ende direkt vorzugeben, ohne Teilstrings zu kopieren. */

/* REG_STARTEND mit einem rm_so > 0 impliziert NICHT automatisch REG_NOTBOL -
 * ohne REG_NOTBOL wuerde "^" in einer Regex an JEDER Suchstartposition
 * matchen, egal ob davor wirklich ein Zeilenumbruch steht. Deshalb hier
 * selbst pruefen: "^" darf nur matchen, wenn offset==0 ist oder das Byte
 * direkt davor ein '\n' ist - alle anderen Faelle bekommen REG_NOTBOL. */
static int regexec_flags_for(const char *text, size_t offset) {
    if (offset > 0 && text[offset - 1] != '\n') {
        return REG_STARTEND | REG_NOTBOL;
    }
    return REG_STARTEND;
}

static int find_match(const char *text, size_t text_len, size_t from, int forward, int wrap,
                       size_t *out_start, size_t *out_end) {
    if (g_search_query[0] == '\0') {
        return 0;
    }
    regex_t re;
    if (!compile_search_regex(&re)) {
        return 0;
    }

    regmatch_t m;
    int found = 0;
    size_t found_start = 0, found_end = 0;

    if (forward) {
        m.rm_so = (regoff_t)from;
        m.rm_eo = (regoff_t)text_len;
        if (regexec(&re, text, 1, &m, regexec_flags_for(text, from)) == 0) {
            found = 1;
            found_start = (size_t)m.rm_so;
            found_end = (size_t)m.rm_eo;
        } else if (wrap && from > 0) {
            m.rm_so = 0;
            m.rm_eo = (regoff_t)text_len;
            if (regexec(&re, text, 1, &m, REG_STARTEND) == 0) {
                found = 1;
                found_start = (size_t)m.rm_so;
                found_end = (size_t)m.rm_eo;
            }
        }
    } else {
        size_t scan = 0;
        int any_found = 0;
        size_t last_start = 0, last_end = 0;
        int has_before = 0;
        size_t before_start = 0, before_end = 0;

        while (scan <= text_len) {
            m.rm_so = (regoff_t)scan;
            m.rm_eo = (regoff_t)text_len;
            if (regexec(&re, text, 1, &m, regexec_flags_for(text, scan)) != 0) {
                break;
            }
            size_t ms = (size_t)m.rm_so, me = (size_t)m.rm_eo;
            any_found = 1;
            last_start = ms;
            last_end = me;
            if (ms < from) {
                has_before = 1;
                before_start = ms;
                before_end = me;
            }
            scan = (me > ms) ? me : ms + 1; /* Leertreffer: mind. 1 vorruecken */
        }

        if (has_before) {
            found = 1;
            found_start = before_start;
            found_end = before_end;
        } else if (wrap && any_found) {
            found = 1;
            found_start = last_start;
            found_end = last_end;
        }
    }

    regfree(&re);
    if (found) {
        *out_start = found_start;
        *out_end = found_end;
    }
    return found;
}

/* Sucht den naechsten/vorigen Treffer ab der aktuellen Selektion (oder dem
 * Cursor, falls keine besteht) und selektiert ihn - editor_set_cursor()
 * zweimal (erst ohne, dann mit extend) baut die neue Selektion sauber auf,
 * genau wie es editor.c's eigene Selektionsfunktionen tun. Gibt zurueck, ob
 * ein Treffer gefunden wurde - editor_has_selection() waere hierfuer NICHT
 * zuverlaessig, weil ein leerer Regex-Treffer (z.B. "a*" oder "^") cursor==
 * anchor hinterlaesst, obwohl durchaus etwas gefunden wurde. */
static int perform_find(int forward) {
    Document *d = active_doc();
    Editor *ed = &d->editor;
    size_t len;
    char *text = editor_copy_all(ed, &len);

    size_t from = forward ? editor_selection_end(ed) : editor_selection_start(ed);
    size_t match_start, match_end;
    int found = find_match(text, len, from, forward, 1, &match_start, &match_end);
    free(text);

    if (found) {
        editor_set_cursor(ed, match_start, 0);
        editor_set_cursor(ed, match_end, 1);
        g_search_status[0] = '\0';
    } else {
        snprintf(g_search_status, sizeof(g_search_status), "%s", btn_tr(BTN_STR_FIND_NOT_FOUND));
    }
    sync_scroll_to_cursor();
    btn_app_request_redraw();
    return found;
}

/* Ersetzt die aktuelle Selektion durch text/len - anders als
 * editor_insert_text() direkt loescht das die Selektion auch dann, wenn
 * text leer ist ("Ersetzen" durch nichts, also Treffer entfernen):
 * editor_insert_text() selbst kehrt bei len==0 sofort zurueck (Guard gegen
 * No-Op-Inserts), was die Selektion in genau diesem Fall stehen liesse. */
static void replace_selection(Editor *ed, const char *text, size_t len) {
    if (len == 0) {
        editor_delete_selection(ed);
    } else {
        editor_insert_text(ed, text, len);
    }
}

/* Ersetzt den aktuellen Treffer (sucht erst einen, falls gerade keiner
 * selektiert ist) und springt direkt zum naechsten weiter. */
static void perform_replace_current(void) {
    Editor *ed = &active_doc()->editor;
    if (!editor_has_selection(ed)) {
        /* Rueckgabewert von perform_find() statt erneut editor_has_selection()
         * zu pruefen - ein gefundener, aber leerer Regex-Treffer (z.B. "a*")
         * hinterlaesst cursor==anchor und wuerde von editor_has_selection()
         * faelschlich als "nichts gefunden" gelesen. */
        if (!perform_find(1)) {
            return;
        }
    }
    replace_selection(ed, g_replace_text, strlen(g_replace_text));
    sync_window_state();
    perform_find(1);
}

static void perform_replace_all(void) {
    Document *d = active_doc();
    Editor *ed = &d->editor;
    if (g_search_query[0] == '\0') {
        return;
    }
    size_t replace_len = strlen(g_replace_text);
    size_t from = 0;
    int count = 0;

    while (1) {
        size_t len;
        char *text = editor_copy_all(ed, &len);
        if (from > len) {
            free(text);
            break;
        }
        size_t match_start, match_end;
        /* wrap=0: sonst wuerde die Schleife, sobald sie einmal das
         * Dokumentende erreicht, wieder vorne anfangen und bereits
         * ersetzte Treffer erneut finden - eine Endlosschleife. */
        int found = find_match(text, len, from, 1, 0, &match_start, &match_end);
        free(text);
        if (!found) {
            break;
        }
        editor_set_cursor(ed, match_start, 0);
        editor_set_cursor(ed, match_end, 1);
        replace_selection(ed, g_replace_text, replace_len);
        /* Bei leerem Treffer UND leerem Ersetzungstext wuerde from sonst
         * nicht vorruecken (Leertreffer an derselben Stelle immer wieder
         * "ersetzt") - mindestens 1 Byte Fortschritt erzwingen. */
        size_t advance = replace_len;
        if (advance == 0 && match_end == match_start) {
            advance = 1;
        }
        from = match_start + advance;
        count++;
    }

    snprintf(g_search_status, sizeof(g_search_status), btn_tr(BTN_STR_FIND_REPLACED_FMT), count);
    sync_window_state();
    sync_scroll_to_cursor();
    btn_app_request_redraw();
}

/* Gemeinsamer Abschluss aller Cursor-Bewegungen unten: Cursor setzen,
 * Anchor nur ohne Selektion mitziehen, desired_col fuer die naechste
 * Auf/Ab-Bewegung merken (oder mit (size_t)-1 zuruecksetzen), und
 * suppress_coalesce setzen - sonst wuerde Tippen nach Auf/Ab/Pos1/Ende
 * wieder faelschlich mit einem alten Undo-Schritt zusammengefasst,
 * genau der Bug, den suppress_coalesce in editor.c beheben soll. */
static void commit_cursor(size_t new_offset, int extend, size_t desired_col) {
    Editor *ed = &active_doc()->editor;
    ed->cursor = new_offset;
    if (!extend) {
        ed->anchor = new_offset;
    }
    ed->desired_col = desired_col;
    editor_mark_cursor_moved(ed);
}

/* Wortumbruch-bewusste vertikale Bewegung (Auf/Ab bewegen sich um eine
 * visuelle Zeile, nicht um eine logische) - editor_move kennt das nicht,
 * weil der Umbruch von der Fensterbreite abhaengt. Nutzt ed->desired_col
 * genau wie editor.c es fuer die (jetzt entfernte) logische Variante tat. */
static void move_visual_row(int direction, int extend) {
    Editor *ed = &active_doc()->editor;
    BtnRow *rows;
    size_t row_count = build_current_rows(&rows);
    size_t cur_row = btn_layout_row_for_offset(rows, row_count, ed->cursor);

    size_t col = (ed->desired_col != (size_t)-1)
                     ? ed->desired_col
                     : editor_visual_column_in_range(ed, rows[cur_row].start, ed->cursor);

    size_t new_offset;
    size_t new_col;
    if (direction < 0 && cur_row == 0) {
        new_offset = 0;
        new_col = 0;
    } else if (direction > 0 && cur_row + 1 >= row_count) {
        new_offset = editor_length(ed);
        new_col = editor_visual_column_in_range(ed, rows[cur_row].start, new_offset);
    } else {
        size_t target_row = (direction < 0) ? cur_row - 1 : cur_row + 1;
        new_offset = editor_offset_for_column_in_range(ed, rows[target_row].start, rows[target_row].len, col);
        new_col = col;
    }
    btn_layout_free(rows);
    commit_cursor(new_offset, extend, new_col);
}

/* Pos1/Ende und Cmd+Links/Rechts springen an Anfang/Ende der aktuellen
 * visuellen Zeile (nach Umbruch) - das entspricht dem nativen macOS-
 * Verhalten (nicht der logischen, evtl. umgebrochenen Zeile). to_end
 * waehlt zwischen den beiden Row-Grenzen. */
static void move_row_edge(int to_end, int extend) {
    Editor *ed = &active_doc()->editor;
    BtnRow *rows;
    size_t row_count = build_current_rows(&rows);
    size_t cur_row = btn_layout_row_for_offset(rows, row_count, ed->cursor);
    size_t new_offset = to_end ? rows[cur_row].start + rows[cur_row].len : rows[cur_row].start;
    btn_layout_free(rows);
    commit_cursor(new_offset, extend, (size_t)-1);
}

static char *read_file_contents(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    size_t read_n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read_n] = '\0';
    *out_len = read_n;
    return buf;
}

static int write_file_contents(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return written == len;
}

/* Gemeinsame Ladelogik fuer Datei > Oeffnen... und Klicks im "Zuletzt
 * geoeffnet"-Untermenue - beide muessen dieselbe Reihenfolge (lesen, Editor
 * fuellen, Pfad/Dirty-Status/Recent-Liste synchronisieren) einhalten. Laedt
 * in EIN bestimmtes Dokument d (siehe open_path_in_tab() fuer die
 * Tab-Auswahl/-Erzeugung davor). */
static void open_file_path(Document *d, const char *path) {
    size_t len;
    char *contents = read_file_contents(path, &len);
    if (contents) {
        editor_set_text(&d->editor, contents, len);
        free(contents);
        set_doc_path(d, path);
        d->saved_edit_seq = d->editor.edit_seq;
        /* d->path statt path: set_doc_path() dupliziert path selbst dann
         * sauber, wenn path zufaellig mit dem *alten* d->path identisch war
         * (und dieser Speicher dabei freigegeben wird) - path waere in dem
         * Fall hier bereits ein haengender Zeiger. */
        add_recent_file(d->path);
    } else {
        fprintf(stderr, "BTNEdit: Datei konnte nicht gelesen werden: %s\n", path);
    }
}

/* Ist path bereits in einem offenen Tab geladen? Rueckgabe: dessen Index,
 * oder -1. Verhindert, dass dieselbe Datei in zwei unabhaengigen
 * Document-Instanzen landet - sonst wuerde spaeteres Sichern in einem der
 * beiden Tabs die Aenderungen des anderen unbemerkt ueberschreiben. */
static int find_tab_for_path(const char *path) {
    for (int i = 0; i < g_doc_count; i++) {
        if (g_docs[i].path && strcmp(g_docs[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

/* Oeffnet path im aktiven Tab, falls der noch unbenutzt/leer ist, sonst in
 * einem neuen Tab - Open muss (anders als frueher ohne Tabs) nie mehr
 * ungesicherte Aenderungen verwerfen, weil es notfalls einfach daneben ein
 * weiteres Tab aufmacht. Ist die Datei schon in einem anderen Tab offen,
 * wird dorthin gewechselt statt sie ein zweites Mal zu laden. */
static void open_path_in_tab(const char *path) {
    int existing = find_tab_for_path(path);
    if (existing >= 0) {
        switch_to_tab(existing);
        return;
    }
    if (!doc_is_blank(active_doc())) {
        int idx = add_tab();
        if (idx < 0) {
            fprintf(stderr, "BTNEdit: maximale Anzahl Tabs (%d) erreicht\n", MAX_TABS);
            return;
        }
        switch_to_tab(idx);
    } else {
        /* switch_to_tab() (oben) schliesst die Suchen-Leiste schon selbst -
         * beim Wiederverwenden des aktiven leeren Tabs (dieser Zweig) findet
         * kein Tab-Wechsel statt, also muss hier explizit dasselbe passieren:
         * sonst bliebe eine offene Suche/Selektion auf Inhalt zeigen, der
         * gleich durch die geladene Datei ersetzt wird. */
        close_find_bar();
    }
    open_file_path(active_doc(), path);
}

/* force_save_as: immer den Sichern-Dialog zeigen, auch wenn schon ein Pfad
 * bekannt ist. Rueckgabe: 1 = gesichert, 0 = abgebrochen/fehlgeschlagen.
 * Nimmt bewusst ein Document*, nicht implizit den aktiven Tab: confirm_
 * discard_doc() ruft das auch fuers Sichern eines im Hintergrund
 * geschlossenen Tabs auf. */
static int perform_save_doc(Document *d, int force_save_as) {
    char *path = NULL;
    int must_free_path = 0;

    if (force_save_as || !d->path) {
        path = btn_show_save_panel(d->path);
        if (!path) {
            return 0;
        }
        must_free_path = 1;
    } else {
        path = d->path;
    }

    size_t len;
    char *contents = editor_copy_all(&d->editor, &len);
    int ok = write_file_contents(path, contents, len);
    free(contents);

    if (ok) {
        set_doc_path(d, path);
        d->saved_edit_seq = d->editor.edit_seq;
        /* d->path statt path: siehe Begruendung in open_file_path(). */
        add_recent_file(d->path);
    } else {
        fprintf(stderr, "BTNEdit: Datei konnte nicht geschrieben werden: %s\n", path);
    }

    if (must_free_path) {
        free(path);
    }
    return ok;
}

static int perform_save(int force_save_as) {
    return perform_save_doc(active_doc(), force_save_as);
}

/* Prueft EIN bestimmtes Dokument auf ungesicherte Aenderungen und fragt ggf.
 * nach. Rueckgabe: 1 = darf geschlossen werden, 0 = Abbrechen. */
static int confirm_discard_doc(Document *d) {
    if (!doc_is_dirty(d)) {
        return 1;
    }
    int choice = btn_show_unsaved_changes_alert(doc_display_name(d));
    if (choice == 0) {
        return 0;
    }
    if (choice == 1) {
        return perform_save_doc(d, 0);
    }
    /* choice == 2, "Nicht sichern" - saved_edit_seq trotzdem synchronisieren,
     * sonst bliebe doc_is_dirty() weiterhin wahr und der Dialog wuerde beim
     * naechsten should_close()-Aufruf (z.B. windowShouldClose: gefolgt von
     * applicationShouldTerminate: in derselben Schliessen-Kette) ueberraschend
     * ein zweites Mal erscheinen. */
    d->saved_edit_seq = d->editor.edit_seq;
    return 1;
}

/* Schliesst Tab idx (Cmd+W, das "x" eines beliebigen Tabs - nicht nur des
 * aktiven). Der letzte verbleibende Tab wird nie einzeln geschlossen,
 * sondern wie der native Fenster-Schliessen-Weg behandelt: btn_app_close_
 * window() loest windowShouldClose:/should_close() aus, das denselben
 * Ungesichert-Check schon fuers (hier: einzige) Dokument macht - ein
 * eigener Check hier wuerde bei "Nicht sichern" den Dialog doppelt zeigen. */
static void close_tab(int idx) {
    if (idx < 0 || idx >= g_doc_count) {
        return;
    }
    if (g_doc_count == 1) {
        btn_app_close_window();
        return;
    }

    int original_active = g_active_doc;
    if (idx != original_active) {
        /* Erst sichtbar machen, DANN fragen - sonst zeigt der Ungesichert-
         * Dialog auf ein Dokument, das gerade gar nicht auf dem Bildschirm
         * zu sehen ist (z.B. beim Klick auf das "x" eines Hintergrund-Tabs). */
        switch_to_tab(idx);
        btn_app_request_redraw();
    }
    if (!confirm_discard_doc(&g_docs[idx])) {
        if (idx != original_active) {
            switch_to_tab(original_active);
            btn_app_request_redraw();
        }
        return;
    }

    doc_free(&g_docs[idx]);
    memmove(&g_docs[idx], &g_docs[idx + 1], (size_t)(g_doc_count - idx - 1) * sizeof(Document));
    g_doc_count--;

    /* Nach dem Entfernen wieder zum urspruenglich aktiven Tab zurueck, statt
     * bei einem Hintergrund-Tab-Schluss einfach dort zu bleiben, wo idx kurz
     * zum Vorschau-Zweck aktiv war - der Nutzer arbeitete ja am urspruenglichen
     * Tab weiter, nicht an dem gerade geschlossenen. War idx selbst der
     * urspruenglich aktive Tab, kommt stattdessen der Tab dran, der an seine
     * Stelle nachgerueckt ist (oder der letzte, falls idx der letzte war). */
    int new_active;
    if (original_active == idx) {
        new_active = (idx < g_doc_count) ? idx : g_doc_count - 1;
    } else if (original_active > idx) {
        new_active = original_active - 1;
    } else {
        new_active = original_active;
    }
    switch_to_tab(new_active);

    sync_window_state();
    btn_app_request_redraw();
}

/* Wird vom Shim sowohl beim Klick auf den roten Schliessen-Knopf als auch
 * bei Cmd+Q/"Beende" aufgerufen (windowShouldClose:/applicationShouldTerminate:)
 * - zentral hier statt separat pro Aufrufer, damit keiner dieser beiden
 * System-Wege den Ungesichert-Dialog umgehen kann. Prueft ALLE offenen Tabs,
 * nicht nur den aktiven - das Fenster/die App zu schliessen wuerde sonst
 * ungesicherte Aenderungen in Hintergrund-Tabs stillschweigend verwerfen.
 *
 * Zwei Phasen statt confirm_discard_doc() direkt in einer Schleife
 * aufzurufen: erst ALLE Dialoge einholen, OHNE etwas anzuwenden (Phase 1);
 * erst wenn wirklich jeder Tab bestaetigt hat, die Entscheidungen ausfuehren
 * (Phase 2). Sonst koennte "Sichern" fuer Tab 0 schon auf die Festplatte
 * schreiben, bevor der Nutzer bei Tab 1 "Abbrechen" waehlt und den ganzen
 * Vorgang abbricht - der bereits geschriebene Tab 0 liesse sich dann nicht
 * mehr zurueckholen. */
static int should_close(void) {
    int original_active = g_active_doc;
    int choices[MAX_TABS]; /* -1 = sauber, sonst der Alert-Rueckgabewert */

    for (int i = 0; i < g_doc_count; i++) {
        if (!doc_is_dirty(&g_docs[i])) {
            choices[i] = -1;
            continue;
        }
        if (i != g_active_doc) {
            /* Sichtbar machen, BEVOR der Dialog erscheint - sonst fragt er
             * nach einem Dokument, das gerade gar nicht auf dem Bildschirm
             * zu sehen ist. */
            switch_to_tab(i);
            btn_app_request_redraw();
        }
        int choice = btn_show_unsaved_changes_alert(doc_display_name(&g_docs[i]));
        if (choice == 0) {
            switch_to_tab(original_active);
            return 0;
        }
        choices[i] = choice;
    }
    switch_to_tab(original_active);

    for (int i = 0; i < g_doc_count; i++) {
        if (choices[i] == 1) {
            if (!perform_save_doc(&g_docs[i], 0)) {
                return 0;
            }
        } else if (choices[i] == 2) {
            g_docs[i].saved_edit_seq = g_docs[i].editor.edit_seq;
        }
    }
    return 1;
}

static void on_draw(CGContextRef ctx, CGRect bounds) {
    g_bounds = bounds;

    const char *labels[MAX_TABS];
    char label_bufs[MAX_TABS][300];
    for (int i = 0; i < g_doc_count; i++) {
        Document *d = &g_docs[i];
        if (doc_is_dirty(d)) {
            snprintf(label_bufs[i], sizeof(label_bufs[i]), "• %s", doc_display_name(d));
        } else {
            snprintf(label_bufs[i], sizeof(label_bufs[i]), "%s", doc_display_name(d));
        }
        labels[i] = label_bufs[i];
    }
    btn_render_tab_bar(ctx, bounds, labels, g_doc_count, g_active_doc);

    if (g_find_bar_visible) {
        btn_render_find_bar(ctx, bounds, btn_tr(BTN_STR_FIND_SEARCH_LABEL), g_search_query,
                             btn_tr(BTN_STR_FIND_REPLACE_LABEL), g_replace_text,
                             g_search_regex, g_focus == BTN_FOCUS_SEARCH, g_search_status);
    }

    Document *active = active_doc();
    btn_render_frame(ctx, content_bounds(), &active->editor, active->scroll_row,
                      btn_highlight_lang_for_path(active->path));
}

/* Klick irgendwo in der Tableiste (y schon vom Aufrufer geprueft): trifft
 * entweder den "+"-Knopf nach dem letzten Tab, das Schliessen-"x" eines
 * Tabs, oder den Tab-Koerper selbst (Tab wechseln). */
static void handle_tab_bar_click(double x) {
    /* btn_tab_width_for() statt der festen BTN_TAB_ITEM_WIDTH: sobald so
     * viele Tabs offen sind, dass sie nicht mehr in voller Breite ins
     * Fenster passen, schrumpft render.c sie beim Zeichnen gleichmaessig -
     * dieselbe Funktion hier haelt die Klick-Trefferpruefung synchron dazu. */
    double tab_width = btn_tab_width_for(g_doc_count, g_bounds.size.width);
    double new_x = (double)g_doc_count * tab_width;
    if (x >= new_x && x < new_x + BTN_TAB_NEW_WIDTH) {
        new_tab_or_reuse_blank();
        return;
    }

    int index = (int)(x / tab_width);
    if (index < 0 || index >= g_doc_count) {
        return;
    }
    double tab_x = (double)index * tab_width;
    int close_clicked = (x >= tab_x + tab_width - BTN_TAB_CLOSE_WIDTH);
    if (close_clicked) {
        close_tab(index);
    } else if (index != g_active_doc) {
        switch_to_tab(index);
    }
}

/* Klick irgendwo in der Suchen-Leiste (y schon vom Aufrufer geprueft):
 * trifft entweder den ".*"-Regex-Umschalter oder eines der beiden Felder
 * (setzt den Fokus dorthin) - dieselben x-Positionen wie
 * btn_render_find_bar()'s Zeichnung in render.c, aus denselben render.h-
 * Konstanten berechnet. */
static void handle_find_bar_click(double x) {
    double search_field_x = BTN_FIND_BAR_PADDING + BTN_FIND_LABEL_WIDTH;
    double regex_x = search_field_x + BTN_FIND_FIELD_WIDTH + BTN_FIND_BAR_PADDING;
    double replace_label_x = regex_x + BTN_FIND_REGEX_WIDTH + BTN_FIND_BAR_PADDING * 2.0;
    double replace_field_x = replace_label_x + BTN_FIND_LABEL_WIDTH;

    if (x >= regex_x && x < regex_x + BTN_FIND_REGEX_WIDTH) {
        g_search_regex = !g_search_regex;
    } else if (x >= search_field_x && x < regex_x) {
        g_focus = BTN_FOCUS_SEARCH;
    } else if (x >= replace_field_x) {
        g_focus = BTN_FOCUS_REPLACE;
    }
}

/* Tastatureingabe, waehrend die Suchen-Leiste fokussiert ist (Suchen- oder
 * Ersetzen-Feld) - komplett getrennt vom Dokument-Tippen unten in on_key().
 * Escape schliesst die Leiste, Tab wechselt zwischen den beiden Feldern,
 * Return loest je nach Feld Suchen/Ersetzen aus, Cmd+Return im Ersetzen-
 * Feld ersetzt alle Treffer. Die Felder selbst erlauben nur Anhaengen/
 * Loeschen vom Ende her (siehe Kommentar bei den globalen Puffern oben). */
static void handle_find_bar_key(const char *characters, unsigned short keycode, int shift, int command) {
    switch (keycode) {
        case KEYCODE_LEFT:
        case KEYCODE_RIGHT:
        case KEYCODE_UP:
        case KEYCODE_DOWN:
        case KEYCODE_HOME:
        case KEYCODE_END:
        case KEYCODE_FORWARD_DELETE:
            /* Die Suchfelder erlauben nur Anhaengen/Loeschen vom Ende her
             * (kein Mini-Editor) - diese Tasten haben hier keine Bedeutung.
             * Ohne diesen Filter wuerden sie ueber ihre NSEvent.characters
             * (Unicode Private-Use-Area, z.B. U+F702 fuer Pfeil-links) als
             * Muell-Bytes im Suchtext landen, siehe naechster Absatz. */
            return;
        default:
            break;
    }
    if (!characters) {
        return;
    }
    unsigned char c = (unsigned char)characters[0];

    if (c == 0x1B) {
        close_find_bar();
        return;
    }
    if (c == '\t') {
        g_focus = (g_focus == BTN_FOCUS_SEARCH) ? BTN_FOCUS_REPLACE : BTN_FOCUS_SEARCH;
        btn_app_request_redraw();
        return;
    }
    if (c == '\r') {
        if (g_focus == BTN_FOCUS_REPLACE) {
            if (command) {
                perform_replace_all();
            } else {
                perform_replace_current();
            }
        } else {
            perform_find(!shift);
        }
        return;
    }

    char *buf = (g_focus == BTN_FOCUS_REPLACE) ? g_replace_text : g_search_query;
    size_t cap = sizeof(g_search_query); /* beide Puffer gleich gross */

    if (c == 0x7F) {
        size_t len = strlen(buf);
        if (len > 0) {
            /* Ein UTF-8-Zeichen zurueck, nicht nur ein Byte - sonst wuerde
             * Backspace mehrbytige Zeichen (Umlaute etc.) haeppchenweise
             * zerlegen statt sie als Ganzes zu entfernen. */
            size_t cut = len - 1;
            while (cut > 0 && ((unsigned char)buf[cut] & 0xC0) == 0x80) {
                cut--;
            }
            buf[cut] = '\0';
        }
    } else if (c >= 0x20) {
        size_t len = strlen(buf);
        size_t add_len = strlen(characters);
        if (len + add_len < cap) {
            memcpy(buf + len, characters, add_len + 1);
        }
    } else {
        return;
    }
    g_search_status[0] = '\0';
    btn_app_request_redraw();
}

static void on_key(const char *characters, unsigned short keycode, unsigned long modifierFlags) {
    int shift = (modifierFlags & BTN_MOD_SHIFT) != 0;
    int option = (modifierFlags & BTN_MOD_OPTION) != 0;
    int command = (modifierFlags & BTN_MOD_COMMAND) != 0;

    /* Escape schliesst eine sichtbare Suchen-Leiste immer, auch wenn der
     * Fokus (z.B. durch einen Klick ins Dokument) inzwischen wieder auf dem
     * Dokument liegt - sonst gaebe es keinen Weg mehr, sie zu schliessen,
     * ausser erneut Cmd+F zu druecken. */
    if (g_find_bar_visible && characters && (unsigned char)characters[0] == 0x1B) {
        close_find_bar();
        return;
    }

    if (g_focus != BTN_FOCUS_DOCUMENT) {
        handle_find_bar_key(characters, keycode, shift, command);
        return;
    }

    Editor *ed = &active_doc()->editor;

    switch (keycode) {
        case KEYCODE_LEFT:
            if (command) {
                move_row_edge(0, shift);
            } else {
                editor_move(ed, option ? BTN_MOVE_WORD_LEFT : BTN_MOVE_LEFT, shift);
            }
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_RIGHT:
            if (command) {
                move_row_edge(1, shift);
            } else {
                editor_move(ed, option ? BTN_MOVE_WORD_RIGHT : BTN_MOVE_RIGHT, shift);
            }
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_UP:
            if (command) {
                editor_move(ed, BTN_MOVE_DOC_START, shift);
            } else {
                move_visual_row(-1, shift);
            }
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_DOWN:
            if (command) {
                editor_move(ed, BTN_MOVE_DOC_END, shift);
            } else {
                move_visual_row(1, shift);
            }
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_HOME:
            move_row_edge(0, shift);
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_END:
            move_row_edge(1, shift);
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_FORWARD_DELETE:
            editor_delete_forward(ed);
            sync_window_state();
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_TAB:
            editor_insert_text(ed, "\t", 1);
            sync_window_state();
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        default:
            break;
    }

    if (command) {
        /* Sonstige Cmd-Kombinationen laufen ueber Menu-Items (siehe on_menu). */
        return;
    }

    if (!characters) {
        return;
    }

    unsigned char c = (unsigned char)characters[0];
    if (c == '\r') {
        editor_insert_text(ed, "\n", 1);
    } else if (c == 0x7F) {
        editor_delete_backward(ed);
    } else if (c >= 0x20) {
        /* editor_handle_bracket_key() deckt Klammern-Auto-Vervollstaendigen
         * und Typdurchlauf ab (siehe editor.c) - fuer alles andere normal
         * einfuegen. Klammern sind ASCII, characters ist bei einem
         * Klammer-Byte also garantiert genau dieses eine Zeichen. */
        if (!editor_handle_bracket_key(ed, (char)c)) {
            editor_insert_text(ed, characters, strlen(characters));
        }
    } else {
        return;
    }

    sync_window_state();
    sync_scroll_to_cursor();
    btn_app_request_redraw();
}

static void on_resize(CGSize size) {
    g_bounds = CGRectMake(0, 0, size.width, size.height);
    /* sync_scroll_to_cursor() ruft clamp_scroll() intern mit auf - reines
     * Clamping reicht hier nicht: Verkleinern des Fensters kann den Text
     * neu umbrechen und die Cursor-Zeile weit aus dem sichtbaren Bereich
     * schieben, ohne dass eine Cursor-Bewegung stattfand, die das sonst
     * erkennen wuerde. */
    sync_scroll_to_cursor();
    btn_app_request_redraw();
}

static void on_mouse(btn_mouse_phase phase, double x, double y, int clickCount, unsigned long modifierFlags) {
    int shift = (modifierFlags & BTN_MOD_SHIFT) != 0;

    switch (phase) {
        case BTN_MOUSE_DOWN: {
            if (y >= g_bounds.size.height - BTN_TAB_BAR_HEIGHT) {
                handle_tab_bar_click(x);
                btn_app_request_redraw();
                return;
            }
            if (g_find_bar_visible && y >= g_bounds.size.height - BTN_TAB_BAR_HEIGHT - BTN_FIND_BAR_HEIGHT) {
                handle_find_bar_click(x);
                btn_app_request_redraw();
                return;
            }
            if (y < BTN_FOOTER_HEIGHT) {
                return;
            }
            /* Klick im Dokument entzieht der Suchen-Leiste den Fokus (die
             * Leiste selbst bleibt offen) - genau wie das Anklicken von
             * irgendwas anderem ein fokussiertes Textfeld sonst auch
             * de-fokussiert. */
            g_focus = BTN_FOCUS_DOCUMENT;
            Document *doc = active_doc();
            size_t offset = btn_hit_test(&doc->editor, content_bounds(), x, y, doc->scroll_row);
            if (clickCount >= 3) {
                editor_select_line_at(&doc->editor, offset);
                g_dragging = 0;
            } else if (clickCount == 2) {
                editor_select_word_at(&doc->editor, offset);
                g_dragging = 0;
            } else {
                editor_set_cursor(&doc->editor, offset, shift);
                g_dragging = 1;
            }
            break;
        }
        case BTN_MOUSE_DRAGGED:
            if (g_dragging) {
                Document *doc = active_doc();
                size_t offset = btn_hit_test(&doc->editor, content_bounds(), x, y, doc->scroll_row);
                editor_set_cursor(&doc->editor, offset, 1);
            }
            break;
        case BTN_MOUSE_UP:
            g_dragging = 0;
            break;
    }

    sync_scroll_to_cursor();
    btn_app_request_redraw();
}

static void on_scroll(double delta_y) {
    Document *doc = active_doc();
    doc->scroll_accum += delta_y;
    long lines = (long)(doc->scroll_accum / BTN_LINE_HEIGHT);
    if (lines == 0) {
        return;
    }
    doc->scroll_accum -= (double)lines * BTN_LINE_HEIGHT;
    doc->scroll_row -= lines;
    clamp_scroll();
    btn_app_request_redraw();
}

static void on_menu(int tag) {
    char *clip;

    if (tag >= BTN_MENU_RECENT_BASE) {
        int index = tag - BTN_MENU_RECENT_BASE;
        if (index < g_recent_count) {
            /* Vorher duplizieren: open_path_in_tab() ruft ueber add_recent_
             * file() etwas auf, das g_recent_paths[] ummordnet/freigibt -
             * ein Zeiger direkt ins Array waere spaetestens dann nicht mehr
             * gueltig. */
            char *path = btn_dup_cstring(g_recent_paths[index]);
            open_path_in_tab(path);
            free(path);
        }
        sync_window_state();
        sync_scroll_to_cursor();
        btn_app_request_redraw();
        return;
    }

    switch (tag) {
        case BTN_MENU_NEW:
            new_tab_or_reuse_blank();
            break;
        case BTN_MENU_OPEN: {
            char *path = btn_show_open_panel();
            if (path) {
                open_path_in_tab(path);
                free(path);
            }
            break;
        }
        case BTN_MENU_SAVE:
            perform_save(0);
            break;
        case BTN_MENU_SAVE_AS:
            perform_save(1);
            break;
        case BTN_MENU_CLOSE:
            close_tab(g_active_doc);
            break;
        case BTN_MENU_UNDO:
            editor_undo(&active_doc()->editor);
            break;
        case BTN_MENU_REDO:
            editor_redo(&active_doc()->editor);
            break;
        case BTN_MENU_CUT:
            clip = editor_get_selection_text(&active_doc()->editor);
            btn_pasteboard_set_string(clip);
            free(clip);
            editor_delete_selection(&active_doc()->editor);
            break;
        case BTN_MENU_COPY:
            clip = editor_get_selection_text(&active_doc()->editor);
            btn_pasteboard_set_string(clip);
            free(clip);
            break;
        case BTN_MENU_PASTE: {
            size_t clip_len;
            clip = btn_pasteboard_copy_string(&clip_len);
            /* replace_selection() statt editor_insert_text() direkt: die
             * Zwischenablage kann eine gueltige, aber leere Zeichenkette
             * liefern (z.B. wenn sie kein Textformat enthaelt) - dann wuerde
             * editor_insert_text()s len==0-Guard eine bestehende Selektion
             * stehen lassen statt sie zu ersetzen. */
            replace_selection(&active_doc()->editor, clip, clip_len);
            free(clip);
            break;
        }
        case BTN_MENU_SELECT_ALL:
            editor_select_all(&active_doc()->editor);
            break;
        case BTN_MENU_FIND:
            open_find_bar();
            break;
        case BTN_MENU_PRINT:
            fprintf(stderr, "BTNEdit: Menu-Aktion %d noch nicht implementiert\n", tag);
            break;
        default:
            break;
    }
    sync_window_state();
    sync_scroll_to_cursor();
    btn_app_request_redraw();
}

int main(void) {
    add_tab(); /* erster, leerer Tab - g_active_doc ist bereits 0 */

    btn_app_init();
    /* Bediensprache folgt der Systemeinstellung (kein eigener
     * Sprachumschalter im Menue) - muss vor btn_app_build_menu() gesetzt
     * sein, das die Menuetitel bereits in der aktiven Sprache aufbaut. */
    btn_strings_set_language(btn_app_detect_system_language());
    btn_app_set_draw_callback(on_draw);
    btn_app_set_key_callback(on_key);
    btn_app_set_resize_callback(on_resize);
    btn_app_set_mouse_callback(on_mouse);
    btn_app_set_scroll_callback(on_scroll);
    btn_app_set_menu_callback(on_menu);
    btn_app_set_should_close_callback(should_close);
    btn_app_build_menu();
    load_recent_files();
    recent_files_refresh_menu();
    btn_app_run();

    for (int i = 0; i < g_doc_count; i++) {
        doc_free(&g_docs[i]);
    }
    return 0;
}
