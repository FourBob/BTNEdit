/*
 * Reines C: Applikationslogik und Verdrahtung der Callbacks aus dem
 * ObjC-Shim (shim.m) mit der Editor-Engine (editor.c/gapbuffer.c).
 */
#include "shim.h"
#include "render.h"
#include "editor.h"
#include "strings.h"

#include <ctype.h>
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
    /* Gecachtes, fertig formatiertes Tab-Label (siehe doc_display_name()),
     * damit on_draw() es nicht bei JEDEM Redraw (jedem Tastendruck, da die
     * App ohne Dirty-Region-Tracking das ganze Fenster neu zeichnet) fuer
     * ALLE offenen Tabs neu zusammenbauen muss, auch fuer unveraenderte
     * Hintergrund-Tabs. label_cache_valid=0 erzwingt einen Neuaufbau;
     * label_cache_was_dirty haelt fest, fuer welchen doc_is_dirty()-Zustand
     * der Cache zuletzt gebaut wurde, damit ein Wechsel des Punkt-Praefixes
     * (ungesicherte Aenderung) den Cache verlaesslich invalidiert. */
    char label_cache[300];
    int label_cache_valid;
    int label_cache_was_dirty;
} Document;

#define MAX_TABS 20
static Document g_docs[MAX_TABS];
static int g_doc_count = 0;
static int g_active_doc = 0;

static CGRect g_bounds = { { 0, 0 }, { 900, 600 } };
static int g_dragging = 0;

static char *g_recent_paths[BTN_MAX_RECENT_FILES]; /* [0] = neuester Eintrag */
static int g_recent_count = 0;

/* Suchen/Ersetzen-Leiste. Die beiden Textfelder sind jeweils ein eigener,
 * ganz normaler Editor (siehe editor.h) - kein Wortumbruch/Syntax noetig,
 * aber Cursor/Selektion/Undo kommen dadurch kostenlos aus derselben Logik
 * wie das Hauptdokument, statt sie fuer ein einzeiliges Feld ein zweites
 * Mal nachzubauen. main.c sorgt lediglich dafuer, dass '\n' nie eingefuegt
 * wird (Enter loest Suchen/Ersetzen aus statt eine Zeile einzufuegen). */
typedef enum {
    BTN_FOCUS_DOCUMENT,
    BTN_FOCUS_SEARCH,
    BTN_FOCUS_REPLACE
} BtnFocus;

static int g_find_bar_visible = 0;
static BtnFocus g_focus = BTN_FOCUS_DOCUMENT;
static Editor g_search_editor;
static Editor g_replace_editor;
static int g_search_regex = 0; /* 0 = Literalsuche, 1 = POSIX-Regex (ERE) */
/* Default 0 (Gross-/Kleinschreibung wird ignoriert) - entspricht der
 * Voreinstellung praktisch aller anderen Mac-Sucheingaben (Safari, Xcode,
 * VS Code); der "Aa"-Umschalter schaltet auf exakte Gross-/Kleinschreibung
 * um. */
static int g_search_case_sensitive = 0;
/* Default 0 (Teiltreffer erlaubt) - der "\b"-Umschalter grenzt Treffer auf
 * ganze Woerter ein (siehe compile_search_regex()). */
static int g_search_whole_word = 0;
static char g_search_status[128] = "";

/* Alle Fundstellen der aktuellen Suchanfrage im aktiven Dokument, nach Start
 * aufsteigend sortiert - fuer die Live-Hervorhebung aller Treffer beim
 * Tippen (render.c faerbt sie gelb, siehe btn_render_frame()) und fuer den
 * Trefferzaehler ("3 von 12 Treffern") in der Statusanzeige. Wird bei jeder
 * Aenderung der Suchanfrage (Tippen, Regex-Umschalter, Ausschneiden/
 * Einfuegen im Suchfeld) sowie bei jeder Return-gesteuerten Navigation neu
 * aufgebaut. BTN_MAX_SEARCH_MATCHES deckelt die Kosten pro Tastendruck fuer
 * pathologisch haeufige Muster - analog zu BTN_MAX_HIGHLIGHT_LINE_LEN in
 * highlight.c. */
#define BTN_MAX_SEARCH_MATCHES 5000
/* Obergrenze fuer die Dokumentgroesse, bis zu der perform_live_search() noch
 * bei JEDEM Tastendruck einen vollen Kopie+Regex-Scan macht - anders als die
 * Trefferanzahl (deren Deckel BTN_MAX_SEARCH_MATCHES oben ist) waechst diese
 * Kosten mit der Dokumentgroesse selbst, nicht mit der Trefferzahl, und war
 * vor der Live-Suche schlicht nicht vorhanden (Tippen im Suchfeld war vorher
 * O(1)). Darueber faellt die Live-Hervorhebung/-Suche aus, Suchen
 * funktioniert aber weiterhin ganz normal per Return (perform_find() bleibt
 * unveraendert schnell genug fuer eine einzelne, diskrete Nutzeraktion statt
 * fuer jeden einzelnen Tastendruck). */
#define BTN_LIVE_SEARCH_MAX_DOC_LEN (2 * 1024 * 1024)
static size_t g_match_starts[BTN_MAX_SEARCH_MATCHES];
static size_t g_match_ends[BTN_MAX_SEARCH_MATCHES];
static size_t g_match_count = 0;
/* editor_edit_seq() des aktiven Dokuments zum Zeitpunkt, als g_match_starts/
 * g_match_ends zuletzt aufgebaut wurden - Klick ins Dokument entzieht der
 * Suchleiste nur den Fokus (siehe close_find_bar()-Kommentar bei
 * switch_to_tab()), schliesst sie aber NICHT; tippt der Nutzer danach direkt
 * im Dokument weiter, veraendern sich die Byte-Offsets im Puffer, ohne dass
 * irgendein Suchleisten-Pfad das mitbekommt. on_draw() vergleicht das vor
 * jedem Redraw gegen den aktuellen edit_seq und verwirft veraltete Treffer
 * (siehe invalidate_matches_if_doc_edited()), statt sie gegen den falschen
 * (weil laengst verschobenen) Text zu zeichnen. */
static size_t g_match_edit_seq = 0;

/* Ausgangspunkt fuer die naechste Live-Suche (siehe perform_live_search) -
 * bewusst getrennt von der aktuellen Dokument-Selektion: die Selektion
 * bewegt sich waehrend des Tippens live zum jeweils naechsten Treffer;
 * wuerde sie auch als Ausgangspunkt dienen, wuerde ein laenger werdender
 * Suchbegriff bei jedem Tastendruck ab dem zuletzt gefundenen Treffer
 * weitersuchen statt konsistent denselben Bereich neu zu pruefen. Wird beim
 * Oeffnen der Leiste und nach jeder Return-gesteuerten Navigation
 * aktualisiert. */
static size_t g_search_anchor = 0;

static Document *active_doc(void) {
    return &g_docs[g_active_doc];
}

/* Welcher Editor gerade Tastatur-/Menuebefehle (Cmd+C/X/V/A/Z...) empfangen
 * soll - das Dokument, oder falls die Suchen-Leiste fokussiert ist, deren
 * Suchen- oder Ersetzen-Feld. */
static Editor *focused_editor(void) {
    switch (g_focus) {
        case BTN_FOCUS_SEARCH:
            return &g_search_editor;
        case BTN_FOCUS_REPLACE:
            return &g_replace_editor;
        default:
            return &active_doc()->editor;
    }
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
    d->label_cache_valid = 0; /* Basisname fuers Tab-Label hat sich geaendert */
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

/* Persistiert die per Cmd+/Cmd-/Cmd+0 eingestellte Schriftgroesse als
 * einzelne Zahl unter ~/.btnedit_prefs - aus denselben Gruenden wie
 * ~/.btnedit_recent oben (siehe recent_file_list_path()) eine eigene Datei
 * statt NSUserDefaults, damit main.c Cocoa-frei bleibt. Ohne das wuerde
 * jeder Neustart wieder bei BTN_DEFAULT_FONT_SIZE (13pt) anfangen, egal
 * welchen Zoom der Nutzer zuletzt eingestellt hatte. */
static char *prefs_file_path(void) {
    const char *home = getenv("HOME");
    if (!home) {
        return NULL;
    }
    size_t len = strlen(home) + strlen("/.btnedit_prefs") + 1;
    char *path = malloc(len);
    snprintf(path, len, "%s/.btnedit_prefs", home);
    return path;
}

static void save_font_size_pref(void) {
    char *path = prefs_file_path();
    if (!path) {
        return;
    }
    FILE *f = fopen(path, "w");
    free(path);
    if (!f) {
        return;
    }
    fprintf(f, "%.1f\n", btn_render_get_font_size());
    fclose(f);
}

static void load_font_size_pref(void) {
    char *path = prefs_file_path();
    if (!path) {
        return;
    }
    FILE *f = fopen(path, "r");
    free(path);
    if (!f) {
        return;
    }
    double size;
    if (fscanf(f, "%lf", &size) == 1) {
        btn_render_set_font_size(size);
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
    /* Sonst blieben die gelben Treffer-Hervorhebungen (siehe
     * btn_render_frame()) im Dokument sichtbar, obwohl die Leiste, die sie
     * erklaert, gar nicht mehr offen ist. */
    g_match_count = 0;
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
        if (strchr(sel, '\n') == NULL) {
            editor_set_text(&g_search_editor, sel, sel_len);
        }
        free(sel);
    }
    /* Bestehenden Suchtext komplett selektieren (wie Cmd+F in praktisch
     * jeder Mac-App) - Tippen ersetzt ihn dann sofort, statt ihn zu
     * ergaenzen. */
    editor_select_all(&g_search_editor);
    g_find_bar_visible = 1;
    g_focus = BTN_FOCUS_SEARCH;
    g_search_status[0] = '\0';
    /* Ausgangspunkt fuer die Live-Suche (siehe g_search_anchor-Kommentar
     * oben) - der Cursor/Selektionsanfang im Dokument, so wie er JETZT
     * steht, nicht irgendwo mitten in einem spaeteren Live-Treffer. */
    g_search_anchor = editor_selection_start(ed);
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
    d->label_cache_valid = 0;
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
    size_t query_len;
    char *query = editor_copy_all(&g_search_editor, &query_len);
    /* Dynamisch statt eines festen char[512]: das Suchfeld ist seit den
     * Editor-basierten Suchleisten-Feldern unbegrenzt lang (kein 256-Byte-
     * Deckel mehr wie frueher beim rohen char[]-Puffer) - ein fester
     * pattern-Puffer wuerde eine lange Literalsuche mit vielen ERE-
     * Sonderzeichen (regex_escape_literal() verdoppelt im schlimmsten Fall
     * jedes Byte) sonst still auf einen kuerzeren, anderen Suchbegriff
     * kappen, ohne dass der Nutzer davon etwas merkt. *2+1 deckt sowohl den
     * unveraenderten Regex-Modus als auch den maximal verdoppelten
     * Literal-Modus ab. */
    size_t cap = query_len * 2 + 1;
    char *pattern = malloc(cap);
    if (g_search_regex) {
        snprintf(pattern, cap, "%s", query);
    } else {
        regex_escape_literal(query, pattern, cap);
    }
    free(query);

    char *final_pattern = pattern;
    if (g_search_whole_word) {
        /* [[:<:]]/[[:>:]] sind wie REG_STARTEND oben eine BSD/Darwin-
         * Erweiterung fuer nullbreite Wortgrenzen-Anker - anders als ein
         * Wrap mit Zeichenklassen wie "(^|[^[:alnum:]_])...($|[^[:alnum:]_])"
         * aendern sie NICHT den eigentlichen Treffer-Bereich selbst
         * (wichtig, weil dieser Bereich 1:1 fuers Hervorheben/Ersetzen
         * verwendet wird - ein Wrap wuerde die angrenzenden Trennzeichen
         * mit in den Treffer ziehen). */
        size_t plen = strlen(pattern);
        char *wrapped = malloc(plen + 16);
        if (wrapped) {
            snprintf(wrapped, plen + 16, "[[:<:]]%s[[:>:]]", pattern);
            free(pattern);
            final_pattern = wrapped;
        }
        /* Bei fehlgeschlagener Allokation bleibt final_pattern das
         * unveraenderte pattern (kein Leck, da wrapped hier NULL ist und
         * pattern unten regulaer weiterverwendet/freigegeben wird) - die
         * Suche funktioniert dann weiter, nur ohne Ganzes-Wort-Eingrenzung,
         * statt mit NULL an snprintf() abzustuerzen. */
    }

    int cflags = REG_EXTENDED | (g_search_case_sensitive ? 0 : REG_ICASE);
    int ok = regcomp(re, final_pattern, cflags) == 0;
    free(final_pattern);
    return ok;
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
    if (editor_length(&g_search_editor) == 0) {
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

/* Sammelt ALLE (nicht ueberlappenden, von links nach rechts gefundenen)
 * Treffer der aktuellen Suchanfrage in text[0,text_len) - dieselbe
 * Vorwaerts-Scan-Schleife wie der Rueckwaerts-Zweig von find_match() oben,
 * hier aber unabhaengig von einer Ausgangsposition und mit Obergrenze
 * max_out: sowohl fuer die Live-Hervorhebung aller Treffer (render.c) als
 * auch fuer den Trefferzaehler ("3 von 12 Treffern") reicht dieselbe,
 * vollstaendige, aufsteigend sortierte Trefferliste - ein zusaetzlicher
 * find_match()-Aufruf allein fuer den "aktuellen" Treffer waere ein
 * zweiter, redundanter Regex-Durchlauf ueber denselben Text. */
static size_t collect_all_matches(const char *text, size_t text_len,
                                   size_t *out_starts, size_t *out_ends, size_t max_out) {
    if (editor_length(&g_search_editor) == 0) {
        return 0;
    }
    regex_t re;
    if (!compile_search_regex(&re)) {
        return 0;
    }
    size_t count = 0;
    size_t scan = 0;
    regmatch_t m;
    while (scan <= text_len && count < max_out) {
        m.rm_so = (regoff_t)scan;
        m.rm_eo = (regoff_t)text_len;
        if (regexec(&re, text, 1, &m, regexec_flags_for(text, scan)) != 0) {
            break;
        }
        size_t ms = (size_t)m.rm_so, me = (size_t)m.rm_eo;
        out_starts[count] = ms;
        out_ends[count] = me;
        count++;
        scan = (me > ms) ? me : ms + 1; /* Leertreffer: mind. 1 vorruecken */
    }
    regfree(&re);
    return count;
}

/* Wie collect_all_matches(), aber OHNE Obergrenze (waechst per realloc statt
 * in ein Array fester Groesse zu schreiben) - nur fuer perform_replace_all()
 * gedacht: "Alle ersetzen" ist ein einmaliger, expliziter Nutzerbefehl (kein
 * Pro-Tastendruck-Pfad wie die Live-Suche, die BTN_MAX_SEARCH_MATCHES
 * bewusst deckelt) und MUSS auch bei mehr als BTN_MAX_SEARCH_MATCHES
 * Treffern (z.B. jedes Leerzeichen in einer grossen Datei) vollstaendig
 * arbeiten statt den Rest der Datei stillschweigend unveraendert zu lassen.
 * *out_starts/*out_ends sind NULL, wenn 0 zurueckgegeben wird, sonst muss
 * der Aufrufer beide per free() freigeben. */
static size_t collect_all_matches_unbounded(const char *text, size_t text_len,
                                             size_t **out_starts, size_t **out_ends) {
    *out_starts = NULL;
    *out_ends = NULL;
    if (editor_length(&g_search_editor) == 0) {
        return 0;
    }
    regex_t re;
    if (!compile_search_regex(&re)) {
        return 0;
    }
    size_t cap = 0, count = 0;
    size_t *starts = NULL, *ends = NULL;
    size_t scan = 0;
    regmatch_t m;
    while (scan <= text_len) {
        m.rm_so = (regoff_t)scan;
        m.rm_eo = (regoff_t)text_len;
        if (regexec(&re, text, 1, &m, regexec_flags_for(text, scan)) != 0) {
            break;
        }
        size_t ms = (size_t)m.rm_so, me = (size_t)m.rm_eo;
        if (count == cap) {
            size_t new_cap = cap ? cap * 2 : 256;
            /* Direkt zuweisen wuerde bei fehlgeschlagenem realloc() den
             * alten (noch gueltigen) Zeiger verlieren - stattdessen in eine
             * temporaere Variable, bei Fehlschlag mit den bisher
             * gefundenen Treffern abbrechen statt abzustuerzen. */
            size_t *new_starts = realloc(starts, new_cap * sizeof(size_t));
            if (!new_starts) {
                break;
            }
            starts = new_starts;
            size_t *new_ends = realloc(ends, new_cap * sizeof(size_t));
            if (!new_ends) {
                break;
            }
            ends = new_ends;
            cap = new_cap;
        }
        starts[count] = ms;
        ends[count] = me;
        count++;
        scan = (me > ms) ? me : ms + 1; /* Leertreffer: mind. 1 vorruecken */
    }
    regfree(&re);
    *out_starts = starts;
    *out_ends = ends;
    return count;
}

/* Waehlt aus einer bereits gesammelten, nach Start aufsteigend sortierten
 * Trefferliste den "aktuellen" Treffer aus: den ersten bei/nach anchor,
 * sonst (Wrap ans Dokumentende) den allerersten - entspricht find_match()s
 * forward=1,wrap=1-Verhalten, nur ohne erneuten Regex-Durchlauf. Rueckgabe
 * 0 nur bei count==0 (dann bleibt *out_index unveraendert). */
static int pick_current_match(const size_t *starts, size_t count, size_t anchor, size_t *out_index) {
    if (count == 0) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        if (starts[i] >= anchor) {
            *out_index = i;
            return 1;
        }
    }
    *out_index = 0;
    return 1;
}

/* Wie pick_current_match(), aber fuer beide Richtungen: forward=1 verhaelt
 * sich identisch dazu, forward=0 waehlt den letzten Treffer VOR anchor
 * (sonst Wrap zum letzten insgesamt) - entspricht find_match()s
 * forward=0,wrap=1-Verhalten. Deckt beide Richtungen der Return-
 * gesteuerten Navigation (perform_find()) aus DERSELBEN, bereits per
 * collect_all_matches() gesammelten Trefferliste ab, statt dafuer einen
 * zweiten, unabhaengigen Regex-Durchlauf zu brauchen. */
static int pick_match_for_navigation(const size_t *starts, size_t count, size_t anchor,
                                      int forward, size_t *out_index) {
    if (forward) {
        return pick_current_match(starts, count, anchor, out_index);
    }
    if (count == 0) {
        return 0;
    }
    for (size_t i = count; i > 0; i--) {
        if (starts[i - 1] < anchor) {
            *out_index = i - 1;
            return 1;
        }
    }
    *out_index = count - 1;
    return 1;
}

/* Baut den Trefferzaehler-Status ("3 von 12 Treffern") fuer den Treffer bei
 * match_start in der zuletzt via collect_all_matches() befuellten
 * g_match_starts/g_match_count. Falls match_start dort nicht vorkommt (nur
 * im pathologischen Fall eines durch BTN_MAX_SEARCH_MATCHES gekappten
 * Dokuments mit mehr Treffern als die Obergrenze erlaubt), wird 1 gezeigt -
 * kosmetisch ungenau fuer diesen Randfall, aber kein Absturz und kein
 * teurer zweiter Scan nur dafuer. */
static void set_match_count_status(size_t match_start) {
    size_t idx = 0;
    for (size_t i = 0; i < g_match_count; i++) {
        if (g_match_starts[i] == match_start) {
            idx = i;
            break;
        }
    }
    snprintf(g_search_status, sizeof(g_search_status), btn_tr(BTN_STR_FIND_COUNT_FMT),
             (int)(idx + 1), (int)g_match_count);
}

/* Live-Suche: wird bei JEDER Aenderung des Suchtexts aufgerufen (Tippen,
 * Regex-Umschalter, Ausschneiden/Einfuegen/Widerrufen im Suchfeld), nicht
 * erst bei Return - hebt alle Treffer hervor (siehe btn_render_frame()) und
 * springt/scrollt bereits zum naechsten Treffer ab g_search_anchor, damit
 * sich Tippen wie eine echte Live-Suche anfuehlt statt nur nachtraeglich
 * eingefaerbt zu werden. */
static void perform_live_search(void) {
    Document *d = active_doc();
    Editor *ed = &d->editor;

    if (editor_length(&g_search_editor) == 0) {
        g_match_count = 0;
        g_search_status[0] = '\0';
        btn_app_request_redraw();
        return;
    }

    if (editor_length(ed) > BTN_LIVE_SEARCH_MAX_DOC_LEN) {
        /* Siehe BTN_LIVE_SEARCH_MAX_DOC_LEN-Kommentar - fuer ein derart
         * grosses Dokument waere ein voller Kopie+Regex-Scan bei JEDEM
         * Tastendruck spuerbar langsam. Suche funktioniert weiterhin ganz
         * normal per Return (perform_find()), nur ohne die Live-Vorschau. */
        g_match_count = 0;
        snprintf(g_search_status, sizeof(g_search_status), "%s",
                 btn_tr(BTN_STR_FIND_LIVE_SEARCH_TOO_LARGE));
        btn_app_request_redraw();
        return;
    }

    size_t len;
    char *text = editor_copy_all(ed, &len);
    g_match_count = collect_all_matches(text, len, g_match_starts, g_match_ends, BTN_MAX_SEARCH_MATCHES);
    g_match_edit_seq = ed->edit_seq;

    size_t idx;
    if (pick_current_match(g_match_starts, g_match_count, g_search_anchor, &idx)) {
        editor_set_cursor(ed, g_match_starts[idx], 0);
        editor_set_cursor(ed, g_match_ends[idx], 1);
        set_match_count_status(g_match_starts[idx]);
        sync_scroll_to_cursor();
    } else {
        snprintf(g_search_status, sizeof(g_search_status), "%s", btn_tr(BTN_STR_FIND_NOT_FOUND));
    }
    free(text);
    btn_app_request_redraw();
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
    /* Ein einziger Scan liefert sowohl den navigierten Treffer als auch die
     * komplette Liste fuer Live-Hervorhebung/Trefferzaehler - vorher liefen
     * hier find_match() UND collect_all_matches() als zwei unabhaengige
     * Regex-Durchlaeufe ueber denselben Text fuer denselben Tastendruck
     * (z.B. wenn die Leiste per Cmd+F mit vorausgefuellter Selektion oeffnet
     * und der Nutzer sofort Return drueckt, ohne vorher zu tippen). */
    g_match_count = collect_all_matches(text, len, g_match_starts, g_match_ends, BTN_MAX_SEARCH_MATCHES);
    g_match_edit_seq = ed->edit_seq;
    free(text);

    size_t idx;
    int found = pick_match_for_navigation(g_match_starts, g_match_count, from, forward, &idx);

    if (found) {
        size_t match_start = g_match_starts[idx];
        size_t match_end = g_match_ends[idx];
        editor_set_cursor(ed, match_start, 0);
        editor_set_cursor(ed, match_end, 1);
        g_search_anchor = forward ? match_end : match_start;
        set_match_count_status(match_start);
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

#define BTN_MAX_REGEX_GROUPS 10
/* Obergrenze fuer die Groesse des expandierten Ersetzungstexts - ohne die
 * koennte ein Ersetzungstext mit vielen hintereinander wiederholten
 * Rueckreferenzen (z.B. "$1$1$1$1$1$1$1$1$1$1" bei einer Suche wie "(.*)",
 * die praktisch die ganze Zeile faengt) unbegrenzt viel Speicher anfordern.
 * Analog zu BTN_MAX_SEARCH_MATCHES/BTN_MAX_HIGHLIGHT_LINE_LEN: lieber an
 * einer grosszuegigen Grenze abbrechen (das teilweise aufgebaute Ergebnis
 * bis dahin bleibt gueltig und wird verwendet) als unbegrenzt zu wachsen. */
#define BTN_MAX_EXPANDED_REPLACEMENT_LEN (16 * 1024 * 1024)

/* Erkennt, ob text ueberhaupt $0-$9/\0-\9-Rueckreferenzen enthaelt - eine
 * billige Byte-fuer-Byte-Pruefung, um expand_replacement()s teuren zweiten
 * regexec()-Durchlauf (fuer die Gruppen-Bereiche) zu sparen, wenn der
 * Ersetzungstext trotz aktivem Regex-Modus gar keine Rueckreferenz
 * verwendet (z.B. Regex-Suche mit rein literalem Ersetzungstext) - in
 * dem Fall ist der Treffer selbst (match_start/match_end) schon laengst
 * anderweitig bekannt, seine Gruppen werden schlicht nicht gebraucht. */
static int replacement_has_backreferences(const char *raw, size_t raw_len) {
    for (size_t i = 0; i + 1 < raw_len; i++) {
        if ((raw[i] == '$' || raw[i] == '\\') && isdigit((unsigned char)raw[i + 1])) {
            return 1;
        }
    }
    return 0;
}

/* Baut den tatsaechlichen Ersetzungstext aus g_replace_editor auf: im
 * Literal-Modus (g_search_regex == 0) oder ohne Rueckreferenzen (siehe
 * replacement_has_backreferences() oben) unveraendert, sonst mit $1..$9
 * (bzw. \1..\9) ersetzt durch die jeweilige Erfassungsgruppe des Treffers
 * bei [match_start, ...) in text ($0/\0 = kompletter Treffer). Der Treffer
 * wird hier erneut per regexec() ab exakt match_start gesucht (statt
 * match_end als Parameter zu verlangen) - deterministisch dieselbe
 * Fundstelle wie beim ersten Mal, liefert aber zusaetzlich die einzelnen
 * Gruppen-Bereiche, die find_match()/collect_all_matches() (nur Gruppe 0)
 * nicht mit herausreichen. Nicht existierende oder nicht getroffene Gruppen
 * werden durch einen leeren String ersetzt (wie in den meisten Editoren/
 * sed -E ueblich). Caller muss free() aufrufen. */
static char *expand_replacement(const char *text, size_t text_len, size_t match_start, size_t *out_len) {
    size_t raw_len;
    char *raw = editor_copy_all(&g_replace_editor, &raw_len);
    if (!g_search_regex || !replacement_has_backreferences(raw, raw_len)) {
        *out_len = raw_len;
        return raw;
    }

    regex_t re;
    if (!compile_search_regex(&re)) {
        *out_len = raw_len;
        return raw;
    }
    regmatch_t groups[BTN_MAX_REGEX_GROUPS];
    groups[0].rm_so = (regoff_t)match_start;
    groups[0].rm_eo = (regoff_t)text_len;
    int ok = regexec(&re, text, BTN_MAX_REGEX_GROUPS, groups, regexec_flags_for(text, match_start)) == 0;
    regfree(&re);
    if (!ok) {
        *out_len = raw_len;
        return raw;
    }

    size_t cap = raw_len + 1;
    char *out = malloc(cap);
    if (!out) {
        /* Degradiert auf den unveraenderten Ersetzungstext statt mit NULL
         * abzustuerzen - raw ist an dieser Stelle noch ein gueltiger,
         * ungenutzter Puffer (wird sonst erst ganz unten freigegeben), der
         * Aufrufer gibt den zurueckgegebenen Zeiger so oder so per free() frei. */
        *out_len = raw_len;
        return raw;
    }
    size_t o = 0;
    for (size_t i = 0; i < raw_len; i++) {
        if (o >= BTN_MAX_EXPANDED_REPLACEMENT_LEN) {
            /* Obergrenze erreicht (siehe BTN_MAX_EXPANDED_REPLACEMENT_LEN) -
             * das bis hierhin aufgebaute Ergebnis bleibt gueltig und wird
             * verwendet, der Rest des Ersetzungstexts wird abgeschnitten. */
            break;
        }
        char c = raw[i];
        if ((c == '$' || c == '\\') && i + 1 < raw_len && isdigit((unsigned char)raw[i + 1])) {
            int g = raw[i + 1] - '0';
            i++;
            if (g < BTN_MAX_REGEX_GROUPS && groups[g].rm_so >= 0) {
                size_t glen = (size_t)(groups[g].rm_eo - groups[g].rm_so);
                if (o + glen + 1 > cap) {
                    cap = o + glen + 1;
                    char *tmp = realloc(out, cap);
                    if (!tmp) {
                        free(out);
                        *out_len = raw_len;
                        return raw;
                    }
                    out = tmp;
                }
                memcpy(out + o, text + groups[g].rm_so, glen);
                o += glen;
            }
            continue;
        }
        if (o + 2 > cap) {
            cap += 16;
            char *tmp = realloc(out, cap);
            if (!tmp) {
                free(out);
                *out_len = raw_len;
                return raw;
            }
            out = tmp;
        }
        out[o++] = c;
    }
    free(raw);
    *out_len = o;
    return out;
}

/* Prueft, ob die aktuelle Selektion tatsaechlich exakt dem naechsten
 * Treffer der aktuellen Suchanfrage ab ihrem eigenen Anfang entspricht -
 * nicht nur "irgendeine Selektion". Wichtig, weil expand_replacement() im
 * Regex-Modus intern erneut ab editor_selection_start() sucht, um an die
 * Erfassungsgruppen zu kommen: regexec() mit REG_STARTEND ist dabei NICHT
 * an genau diese Position angeankert (dieselbe "Treffer kann spaeter
 * beginnen"-Semantik wie find_match()s eigener Vorwaertszweig weiter oben)
 * - bei einer Selektion, die NICHT von einem echten Treffer stammt (z.B.
 * manuell mit der Maus gewaehlt, waehrend die Suchleiste offen ist), wuerde
 * diese interne Suche stattdessen den naechsten, ganz woanders liegenden
 * Treffer finden und dessen Gruppen fuer eine Ersetzung an der falschen
 * (der eigentlich selektierten) Stelle verwenden - stillschweigend falscher
 * Inhalt an der falschen Stelle, ohne jede Fehlermeldung. */
static int selection_is_current_match(Editor *ed) {
    if (!editor_has_selection(ed)) {
        return 0;
    }
    size_t len;
    char *text = editor_copy_all(ed, &len);
    size_t match_start, match_end;
    int found = find_match(text, len, editor_selection_start(ed), 1, 0, &match_start, &match_end);
    free(text);
    return found && match_start == editor_selection_start(ed) && match_end == editor_selection_end(ed);
}

/* Ersetzt den aktuellen Treffer (sucht erst einen, falls gerade keiner
 * selektiert ist bzw. die bestehende Selektion nicht wirklich der aktuelle
 * Treffer ist) und springt direkt zum naechsten weiter. */
static void perform_replace_current(void) {
    Editor *ed = &active_doc()->editor;
    if (!selection_is_current_match(ed)) {
        /* Rueckgabewert von perform_find() statt erneut editor_has_selection()
         * zu pruefen - ein gefundener, aber leerer Regex-Treffer (z.B. "a*")
         * hinterlaesst cursor==anchor und wuerde von editor_has_selection()
         * faelschlich als "nichts gefunden" gelesen. */
        if (!perform_find(1)) {
            return;
        }
    }
    size_t doc_len;
    char *doc_text = editor_copy_all(ed, &doc_len);
    size_t match_start = editor_selection_start(ed);
    size_t replace_len;
    char *replace_text = expand_replacement(doc_text, doc_len, match_start, &replace_len);
    free(doc_text);
    replace_selection(ed, replace_text, replace_len);
    free(replace_text);
    sync_window_state();
    perform_find(1);
    /* perform_find() oben ueberschreibt g_search_status bereits (Trefferzaehler
     * oder "Nicht gefunden") - die Rueckmeldung fuer DIESEN Befehl (dass gerade
     * ersetzt wurde) ist die eigentliche Antwort auf Return im Ersetzen-Feld
     * und ueberschreibt sie hier bewusst noch einmal, unabhaengig davon, ob
     * danach ein weiterer Treffer gefunden wurde. */
    snprintf(g_search_status, sizeof(g_search_status), btn_tr(BTN_STR_FIND_REPLACED_FMT), 1);
    btn_app_request_redraw();
}

static void perform_replace_all(void) {
    Document *d = active_doc();
    Editor *ed = &d->editor;
    if (editor_length(&g_search_editor) == 0) {
        return;
    }

    /* Nur wenn der Ersetzungstext tatsaechlich Rueckreferenzen enthaelt
     * (siehe replacement_has_backreferences()), unterscheidet sich der
     * tatsaechliche Ersetzungstext von Treffer zu Treffer und muss pro
     * Treffer per expand_replacement() neu aufgebaut werden - sonst reicht
     * (wie vor der Rueckreferenzen-Funktion) eine einzige Kopie vor der
     * Schleife, statt sie bei jedem Treffer erneut zu malloc'en/kopieren. */
    size_t fixed_replace_len = 0;
    char *fixed_replace_text = NULL;
    int per_match_expansion;
    {
        size_t raw_len;
        char *raw = editor_copy_all(&g_replace_editor, &raw_len);
        per_match_expansion = g_search_regex && replacement_has_backreferences(raw, raw_len);
        if (per_match_expansion) {
            free(raw);
        } else {
            fixed_replace_text = raw;
            fixed_replace_len = raw_len;
        }
    }

    /* Alle Treffer EINMAL im unveraenderten Ausgangsdokument sammeln, statt
     * (wie zuvor) bei jedem einzelnen Treffer das inzwischen teils schon
     * ersetzte Dokument komplett neu zu kopieren und erneut zu durchsuchen -
     * das war O(Treffer * Dokumentlaenge), jetzt O(Dokumentlaenge) fuer die
     * Suche plus die ohnehin noetigen Ersetzungen selbst. Treffer sind nicht
     * ueberlappend und aufsteigend sortiert (siehe
     * collect_all_matches_unbounded()) - jede Ersetzung betrifft daher nur
     * den Bereich VOR dem naechsten Treffer, sodass orig_text ab dessen
     * Originalposition weiterhin byte-identisch mit dem Live-Puffer an der
     * um delta verschobenen Position ist. */
    size_t orig_len;
    char *orig_text = editor_copy_all(ed, &orig_len);
    size_t *starts, *ends;
    size_t match_count = collect_all_matches_unbounded(orig_text, orig_len, &starts, &ends);

    long delta = 0;
    for (size_t i = 0; i < match_count; i++) {
        size_t match_start = starts[i];
        size_t match_end = ends[i];

        size_t replace_len;
        char *replace_text;
        if (per_match_expansion) {
            replace_text = expand_replacement(orig_text, orig_len, match_start, &replace_len);
        } else {
            replace_text = fixed_replace_text;
            replace_len = fixed_replace_len;
        }

        size_t live_start = (size_t)((long)match_start + delta);
        size_t live_end = (size_t)((long)match_end + delta);
        editor_set_cursor(ed, live_start, 0);
        editor_set_cursor(ed, live_end, 1);
        replace_selection(ed, replace_text, replace_len);

        delta += (long)replace_len - (long)(match_end - match_start);
        if (per_match_expansion) {
            free(replace_text);
        }
    }
    int count = (int)match_count;
    free(starts);
    free(ends);
    free(orig_text);
    free(fixed_replace_text); /* NULL-sicher, No-Op wenn per_match_expansion galt */

    /* Trefferliste nach dem Ersetzen neu aufbauen - der bisherige Stand
     * (aus der Live-Suche vor diesem Befehl) bezieht sich auf Byte-Offsets
     * im inzwischen veraenderten Dokument und waere sonst falsch platziert
     * oder wuerde laengst ersetzte Treffer weiter gelb hervorheben. */
    size_t len;
    char *text = editor_copy_all(ed, &len);
    g_match_count = collect_all_matches(text, len, g_match_starts, g_match_ends, BTN_MAX_SEARCH_MATCHES);
    g_match_edit_seq = ed->edit_seq;
    free(text);

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

/* Grobe Heuristik: ein eingebettetes NUL-Byte kommt in echten Textdateien
 * praktisch nie vor, in Binaerdateien (Bilder, Binaer-STL, ausfuehrbare
 * Dateien, ...) dagegen sehr haeufig - reicht als einfacher, schneller
 * Anhaltspunkt, ohne eine vollstaendige Content-Type-Erkennung zu brauchen.
 * Seit render.c bei ungueltigem UTF-8 auf ISO-8859-1 zurueckfaellt (siehe
 * dortiger Kommentar), sieht eine solche Datei nicht mehr offensichtlich
 * "kaputt" aus, sondern wie plausibler, wenn auch wirrer Text - ohne diese
 * Warnung koennte ein Nutzer sie versehentlich bearbeiten und mit Cmd+S
 * ueberschreiben. Nur die ersten paar KB werden geprueft: reicht als
 * repraesentative Stichprobe und haelt die Pruefung auch bei sehr grossen
 * Dateien schnell. */
#define BTN_BINARY_SNIFF_LEN 8192

static int looks_binary(const char *data, size_t len) {
    size_t n = len < BTN_BINARY_SNIFF_LEN ? len : BTN_BINARY_SNIFF_LEN;
    for (size_t i = 0; i < n; i++) {
        if (data[i] == '\0') {
            return 1;
        }
    }
    return 0;
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
        if (looks_binary(contents, len) && !btn_show_binary_file_warning(basename_of(path))) {
            free(contents);
            return;
        }
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

    /* Bei jedem Redraw frisch abgefragt (kein Notification-Mechanismus
     * noetig, siehe btn_render_set_dark_mode()-Kommentar in render.h) -
     * MUSS vor jedem render.c-Zeichenaufruf unten stehen, sonst zeichnen
     * Tableiste/Suchleiste/Frame mit dem alten Modus. */
    btn_render_set_dark_mode(btn_app_is_dark_mode());

    const char *labels[MAX_TABS];
    for (int i = 0; i < g_doc_count; i++) {
        Document *d = &g_docs[i];
        int dirty = doc_is_dirty(d);
        if (!d->label_cache_valid || d->label_cache_was_dirty != dirty) {
            if (dirty) {
                snprintf(d->label_cache, sizeof(d->label_cache), "• %s", doc_display_name(d));
            } else {
                snprintf(d->label_cache, sizeof(d->label_cache), "%s", doc_display_name(d));
            }
            d->label_cache_valid = 1;
            d->label_cache_was_dirty = dirty;
        }
        labels[i] = d->label_cache;
    }
    btn_render_tab_bar(ctx, bounds, labels, g_doc_count, g_active_doc);

    if (g_find_bar_visible) {
        int focus_field = (g_focus == BTN_FOCUS_SEARCH) ? 1 : (g_focus == BTN_FOCUS_REPLACE) ? 2 : 0;
        btn_render_find_bar(ctx, bounds, btn_tr(BTN_STR_FIND_SEARCH_LABEL), &g_search_editor,
                             btn_tr(BTN_STR_FIND_REPLACE_LABEL), &g_replace_editor,
                             btn_tr(BTN_STR_REPLACE_ALL_BUTTON),
                             g_search_regex, g_search_case_sensitive, g_search_whole_word,
                             focus_field, g_search_status);
    }

    Document *active = active_doc();
    /* g_match_count ist nur > 0, waehrend die Suchleiste offen ist (siehe
     * close_find_bar()/switch_to_tab(), die sie beim Schliessen bzw.
     * Tabwechsel leeren) und bezieht sich dann garantiert auf genau dieses
     * aktive Dokument (Suche laeuft immer auf active_doc(), siehe
     * perform_live_search()/perform_find()). Klick ins Dokument entzieht
     * der Suchleiste aber nur den Fokus, schliesst sie NICHT (siehe
     * on_mouse()) - tippt der Nutzer danach direkt im Dokument weiter, ohne
     * die Suchleiste erneut zu beruehren, veraltet g_match_starts/g_match_ends
     * gegenueber den jetzt verschobenen Byte-Offsets. g_match_edit_seq
     * (siehe dortiger Kommentar) faengt das ab: weicht es vom aktuellen
     * edit_seq ab, werden 0 Treffer statt der veralteten Bereiche gezeichnet. */
    size_t render_match_count = (g_match_edit_seq == active->editor.edit_seq) ? g_match_count : 0;
    btn_render_frame(ctx, content_bounds(), &active->editor, active->scroll_row,
                      btn_highlight_lang_for_path(active->path),
                      g_match_starts, g_match_ends, render_match_count);
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
 * trifft entweder einen der drei Umschalter (".*"/"Aa"/"\b") oder eines der
 * beiden Felder (setzt den Fokus dorthin) - dieselben x-Positionen wie
 * btn_render_find_bar()'s Zeichnung in render.c, aus denselben render.h-
 * Konstanten berechnet. */
static void handle_find_bar_click(double x) {
    double search_field_x = BTN_FIND_BAR_PADDING + BTN_FIND_LABEL_WIDTH;
    double regex_x = search_field_x + BTN_FIND_FIELD_WIDTH + BTN_FIND_BAR_PADDING;
    double case_x = regex_x + BTN_FIND_REGEX_WIDTH + BTN_FIND_BAR_PADDING;
    double word_x = case_x + BTN_FIND_REGEX_WIDTH + BTN_FIND_BAR_PADDING;
    double replace_label_x = word_x + BTN_FIND_REGEX_WIDTH + BTN_FIND_BAR_PADDING * 2.0;
    double replace_field_x = replace_label_x + BTN_FIND_LABEL_WIDTH;
    double replace_all_x = replace_field_x + BTN_FIND_FIELD_WIDTH + BTN_FIND_BAR_PADDING;

    if (x >= regex_x && x < regex_x + BTN_FIND_REGEX_WIDTH) {
        g_search_regex = !g_search_regex;
        /* Aendert, wie der bestehende Suchtext interpretiert wird - die
         * Live-Hervorhebung/der Trefferzaehler muessen dieselbe neue
         * Interpretation zeigen, nicht erst beim naechsten Tastendruck. */
        perform_live_search();
    } else if (x >= case_x && x < case_x + BTN_FIND_REGEX_WIDTH) {
        g_search_case_sensitive = !g_search_case_sensitive;
        perform_live_search();
    } else if (x >= word_x && x < word_x + BTN_FIND_REGEX_WIDTH) {
        g_search_whole_word = !g_search_whole_word;
        perform_live_search();
    } else if (x >= search_field_x && x < regex_x) {
        g_focus = BTN_FOCUS_SEARCH;
    } else if (x >= replace_all_x && x < replace_all_x + BTN_FIND_REPLACE_ALL_WIDTH) {
        /* "Alle ersetzen"-Knopf - dieselbe Aktion wie Cmd+Return im
         * Ersetzen-Feld (siehe handle_find_bar_key()). Bewusst VOR dem
         * unbegrenzten replace_field_x-Zweig unten geprueft: der Knopf
         * sitzt innerhalb von dessen Bereich, die Reihenfolge entscheidet
         * hier also tatsaechlich, welcher Zweig greift. */
        perform_replace_all();
    } else if (x >= replace_field_x) {
        /* Bewusst unbegrenzt nach rechts (nicht auf das Ersetzen-Feld selbst
         * geklemmt) - ein Klick irgendwo rechts davon (Statustext-Bereich
         * eingeschlossen) soll wie vor dem "Alle ersetzen"-Knopf weiterhin
         * das Ersetzen-Feld fokussieren, nicht ins Leere gehen. */
        g_focus = BTN_FOCUS_REPLACE;
    }
}

/* Tastatureingabe, waehrend die Suchen-Leiste fokussiert ist (Suchen- oder
 * Ersetzen-Feld) - komplett getrennt vom Dokument-Tippen unten in on_key().
 * Escape schliesst die Leiste, Tab wechselt zwischen den beiden Feldern,
 * Return loest je nach Feld Suchen/Ersetzen aus, Cmd+Return im Ersetzen-
 * Feld ersetzt alle Treffer. Fuer alles andere (Cursor-Bewegung, Tippen,
 * Loeschen) ist das fokussierte Feld ein ganz normaler Editor (siehe
 * focused_editor()) - kein Mehrzeilen-Konzept noetig, da BTN_MOVE_DOC_START/
 * END fuer ein Feld ohne '\n' bereits genau Pos1/Ende sind. */
static void handle_find_bar_key(const char *characters, unsigned short keycode, int shift, int option, int command) {
    Editor *ed = (g_focus == BTN_FOCUS_REPLACE) ? &g_replace_editor : &g_search_editor;

    switch (keycode) {
        case KEYCODE_LEFT:
            /* Cmd+Links springt wie im Dokument (move_row_edge()) an den
             * Feldanfang - fuer ein einzeiliges Feld ist das exakt
             * BTN_MOVE_DOC_START. */
            editor_move(ed, command ? BTN_MOVE_DOC_START : (option ? BTN_MOVE_WORD_LEFT : BTN_MOVE_LEFT), shift);
            btn_app_request_redraw();
            return;
        case KEYCODE_RIGHT:
            editor_move(ed, command ? BTN_MOVE_DOC_END : (option ? BTN_MOVE_WORD_RIGHT : BTN_MOVE_RIGHT), shift);
            btn_app_request_redraw();
            return;
        case KEYCODE_HOME:
            editor_move(ed, BTN_MOVE_DOC_START, shift);
            btn_app_request_redraw();
            return;
        case KEYCODE_END:
            editor_move(ed, BTN_MOVE_DOC_END, shift);
            btn_app_request_redraw();
            return;
        case KEYCODE_UP:
        case KEYCODE_DOWN:
            /* Kein Mehrzeilen-Konzept in einem einzeiligen Feld - anders als
             * im Dokument (siehe on_key()) hier ohne Bedeutung. */
            return;
        case KEYCODE_FORWARD_DELETE:
            editor_delete_forward(ed);
            if (g_focus == BTN_FOCUS_SEARCH) {
                perform_live_search();
            } else {
                g_search_status[0] = '\0';
                btn_app_request_redraw();
            }
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
    if (command) {
        /* Cmd+A/C/X/V/Z laufen ueber Menu-Items (siehe on_menu() und
         * focused_editor()), genau wie im Dokument (siehe on_key()). */
        return;
    }

    if (c == 0x7F) {
        editor_delete_backward(ed);
    } else if (c >= 0x20) {
        editor_insert_text(ed, characters, strlen(characters));
    } else {
        return;
    }
    if (g_focus == BTN_FOCUS_SEARCH) {
        perform_live_search();
    } else {
        g_search_status[0] = '\0';
        btn_app_request_redraw();
    }
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
        handle_find_bar_key(characters, keycode, shift, option, command);
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
        /* editor_handle_bracket_key() deckt Auto-Vervollstaendigen/
         * Typdurchlauf fuer Klammern UND Anfuehrungszeichen ab (siehe
         * editor.c) - fuer alles andere normal einfuegen. Beide sind ASCII,
         * characters ist bei einem solchen Byte also garantiert genau
         * dieses eine Zeichen. */
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

/* Zustand fuer den laufenden Druckvorgang: btn_print_pages() (shim.m) ruft
 * on_print_layout()/on_print_page() ohne Userdata-Parameter auf (wie
 * g_draw_cb auch), deshalb hier als Globals wie active_doc()/g_bounds -
 * waehrend eines Drucks kann ohnehin immer nur ein Dokument gedruckt
 * werden, kein Nebenlaeufigkeitsproblem. g_print_line_states[i] ist der
 * Kommentar-Zustand VOR logischer Zeile i (siehe
 * btn_compute_line_comment_states()) - einmal pro Layout-Aufruf komplett
 * neu berechnet, damit on_print_page() nicht pro Seite von Dokumentanfang
 * an rescannen muss (die Druckvorschau zeichnet Seiten nicht zwingend
 * aufsteigend, ein blosses Mitschleppen des letzten Zustands waere dafuer
 * nicht sicher). */
static Editor *g_print_editor = NULL;
static const BtnLangSpec *g_print_lang = NULL;
static BtnRow *g_print_rows = NULL;
static size_t g_print_row_count = 0;
static size_t g_print_rows_per_page = 1;
static int *g_print_line_states = NULL;

static void free_print_layout(void) {
    if (g_print_rows) {
        btn_layout_free(g_print_rows);
        g_print_rows = NULL;
    }
    free(g_print_line_states);
    g_print_line_states = NULL;
}

/* Wird von btn_print_pages() (shim.m) aufgerufen, sobald AppKit die
 * Seitenanzahl fuer page_size braucht - das passiert waehrend der Nutzer im
 * Systemdruckdialog Papierformat/Ausrichtung/Raender waehlt (fuer dessen
 * Live-Vorschau, ggf. mehrfach) und ein letztes Mal nach dessen
 * Bestaetigung. Baut das Wortumbruch-/Seiten-Layout deshalb bei JEDEM
 * Aufruf komplett neu auf, statt es einmalig VOR dem Dialog zu berechnen -
 * sonst wuerde eine im Dialog geaenderte Einstellung nie beim tatsaechlichen
 * Druck ankommen. */
static int on_print_layout(CGSize page_size) {
    Document *doc = active_doc();
    free_print_layout();

    const BtnLangSpec *lang = btn_highlight_lang_for_path(doc->path);
    size_t row_count;
    BtnRow *rows = btn_layout_build(&doc->editor, btn_print_text_width(page_size.width), &row_count);
    size_t rows_per_page = btn_rows_per_page(page_size.height);
    int page_count = (int)((row_count + rows_per_page - 1) / rows_per_page);
    if (page_count < 1) {
        page_count = 1;
    }

    size_t line_count = editor_line_count(&doc->editor);
    int *line_states = malloc(sizeof(int) * (line_count > 0 ? line_count : 1));
    btn_compute_line_comment_states(&doc->editor, lang, line_states);

    g_print_editor = &doc->editor;
    g_print_lang = lang;
    g_print_rows = rows;
    g_print_row_count = row_count;
    g_print_rows_per_page = rows_per_page;
    g_print_line_states = line_states;

    return page_count;
}

static void on_print_page(CGContextRef ctx, CGRect page_rect, int page_index) {
    size_t first_row = (size_t)page_index * g_print_rows_per_page;
    int start_state = 0;
    if (g_print_lang && first_row < g_print_row_count) {
        start_state = g_print_line_states[g_print_rows[first_row].logical_line];
    }
    btn_render_print_page(ctx, page_rect, g_print_editor, g_print_lang,
                           g_print_rows, g_print_row_count, first_row, start_state);
}

static void perform_print(void) {
    btn_print_pages(on_print_layout, on_print_page);

    free_print_layout();
    g_print_editor = NULL;
    g_print_lang = NULL;
    g_print_row_count = 0;
}

/* Zeigt den "Gehe zu Zeile..."-Dialog (Systemdialog, siehe shim.h) und
 * springt bei Bestaetigung an den Anfang der eingegebenen (1-basierten)
 * Zeile - editor_line_bounds() liefert denselben Zeilenanfang, den auch
 * render.c fuers Zeichnen einer logischen Zeile nutzt. */
static void perform_goto_line(void) {
    Editor *ed = &active_doc()->editor;
    long max_line = (long)editor_line_count(ed);
    long line;
    if (!btn_show_goto_line_dialog(max_line, &line)) {
        return;
    }
    size_t start, len;
    editor_line_bounds(ed, (size_t)(line - 1), &start, &len);
    editor_set_cursor(ed, start, 0);
}

/* "Oeffnen mit"/Doppelklick auf eine registrierte Dateiendung/Drag&Drop
 * aufs Dock-Icon (siehe btn_app_set_open_file_callback() in shim.h) - nutzt
 * dieselbe Tab-Auswahl/Lade-Logik wie Datei > Oeffnen..., damit eine schon
 * offene Datei nicht doppelt geladen wird und ein noch unbenutztes leeres
 * Tab wiederverwendet wird. */
static void on_open_file(const char *path) {
    open_path_in_tab(path);
    sync_window_state();
    sync_scroll_to_cursor();
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
            editor_undo(focused_editor());
            break;
        case BTN_MENU_REDO:
            editor_redo(focused_editor());
            break;
        case BTN_MENU_CUT:
            clip = editor_get_selection_text(focused_editor());
            btn_pasteboard_set_string(clip);
            free(clip);
            editor_delete_selection(focused_editor());
            break;
        case BTN_MENU_COPY:
            clip = editor_get_selection_text(focused_editor());
            btn_pasteboard_set_string(clip);
            free(clip);
            break;
        case BTN_MENU_PASTE: {
            size_t clip_len;
            clip = btn_pasteboard_copy_string(&clip_len);
            /* Kein manuelles Saeubern von '\n'/'\r'/'\t' mehr noetig hier -
             * editor_insert_text() macht das jetzt zentral fuer jeden
             * einzeiligen Editor (siehe editor_set_single_line() in main(),
             * editor.h/.c), egal ueber welchen Weg Text eingefuegt wird.
             * replace_selection() statt editor_insert_text() direkt: die
             * Zwischenablage kann eine gueltige, aber leere Zeichenkette
             * liefern (z.B. wenn sie kein Textformat enthaelt) - dann wuerde
             * editor_insert_text()s len==0-Guard eine bestehende Selektion
             * stehen lassen statt sie zu ersetzen. */
            replace_selection(focused_editor(), clip, clip_len);
            free(clip);
            break;
        }
        case BTN_MENU_SELECT_ALL:
            editor_select_all(focused_editor());
            break;
        case BTN_MENU_FIND:
            open_find_bar();
            break;
        case BTN_MENU_GOTO_LINE:
            perform_goto_line();
            break;
        case BTN_MENU_PRINT:
            perform_print();
            break;
        case BTN_MENU_HELP:
            btn_show_help_alert();
            break;
        case BTN_MENU_ZOOM_IN:
            btn_render_zoom_in();
            save_font_size_pref();
            break;
        case BTN_MENU_ZOOM_OUT:
            btn_render_zoom_out();
            save_font_size_pref();
            break;
        case BTN_MENU_ZOOM_RESET:
            btn_render_zoom_reset();
            save_font_size_pref();
            break;
        default:
            break;
    }
    /* Deckt sowohl textaendernde Menuebefehle im Suchfeld ab (Widerrufen/
     * Wiederholen/Ausschneiden/Einfuegen - Tippen selbst laeuft bereits
     * ueber handle_find_bar_key(), nicht hierueber) als auch das Oeffnen
     * der Leiste per Cmd+F (BTN_MENU_FIND, siehe open_find_bar() oben):
     * fuer eine vorausgefuellte Selektion muss der Trefferzaehler/die
     * Live-Hervorhebung sofort stimmen, nicht erst nach dem naechsten
     * Tastendruck. Bewusst auf genau diese Tags eingegrenzt (statt bei
     * JEDEM Befehl zu feuern, solange das Suchfeld fokussiert ist) - ein
     * voller Dokument-Kopie+Regex-Scan (siehe perform_live_search()) als
     * Nebeneffekt von z.B. Zoomen oder Drucken waere bei grossen Dokumenten
     * spuerbar und mit diesen Befehlen inhaltlich nicht verwandt. */
    if (g_focus == BTN_FOCUS_SEARCH &&
        (tag == BTN_MENU_FIND || tag == BTN_MENU_UNDO || tag == BTN_MENU_REDO ||
         tag == BTN_MENU_CUT || tag == BTN_MENU_PASTE)) {
        perform_live_search();
    }
    sync_window_state();
    sync_scroll_to_cursor();
    btn_app_request_redraw();
}

int main(void) {
    add_tab(); /* erster, leerer Tab - g_active_doc ist bereits 0 */
    editor_init(&g_search_editor);
    editor_init(&g_replace_editor);
    editor_set_single_line(&g_search_editor, 1);
    editor_set_single_line(&g_replace_editor, 1);

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
    btn_app_set_open_file_callback(on_open_file);
    btn_app_build_menu();
    load_recent_files();
    recent_files_refresh_menu();
    /* Muss vor btn_app_run() stehen, damit der allererste Redraw schon mit
     * der zuletzt eingestellten Schriftgroesse zeichnet, statt kurz bei
     * BTN_DEFAULT_FONT_SIZE aufzublitzen. */
    load_font_size_pref();
    btn_app_run();

    for (int i = 0; i < g_doc_count; i++) {
        doc_free(&g_docs[i]);
    }
    editor_free(&g_search_editor);
    editor_free(&g_replace_editor);
    return 0;
}
