/*
 * Reines C: Applikationslogik und Verdrahtung der Callbacks aus dem
 * ObjC-Shim (shim.m) mit der Editor-Engine (editor.c/gapbuffer.c).
 */
#include "shim.h"
#include "render.h"
#include "editor.h"
#include "strings.h"

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

/* Content-Flaeche ist g_bounds abzueglich der Tableiste oben - dieselbe
 * Rueckgabe geht an btn_render_frame/btn_hit_test/btn_layout_build, sodass
 * Zeichnen, Scrollen und Klick-Trefferpruefung nie auseinanderlaufen. Da nur
 * die Hoehe verkleinert wird (Ursprung bleibt (0,0)), bleiben absolute
 * y-Koordinaten unterhalb der Tableiste unveraendert gueltig - eine
 * gesonderte Koordinatentransformation fuer Mausklicks ist nicht noetig. */
static CGRect content_bounds(void) {
    CGRect r = g_bounds;
    r.size.height -= BTN_TAB_BAR_HEIGHT;
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

/* Macht idx zum aktiven Tab und bringt Fenstertitel/Ungesichert-Indikator/
 * Scroll-Position auf den Stand dieses Dokuments - noetig, weil waehrend ein
 * anderer Tab aktiv war, weder sein Titel/Dirty-Status im Fenster sichtbar
 * war noch sich seine Scroll-Position an eine zwischenzeitliche
 * Fenstergroessenaenderung angepasst haben kann. */
static void switch_to_tab(int idx) {
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

/* Oeffnet path im aktiven Tab, falls der noch unbenutzt/leer ist, sonst in
 * einem neuen Tab - Open muss (anders als frueher ohne Tabs) nie mehr
 * ungesicherte Aenderungen verwerfen, weil es notfalls einfach daneben ein
 * weiteres Tab aufmacht. */
static void open_path_in_tab(const char *path) {
    if (!doc_is_blank(active_doc())) {
        int idx = add_tab();
        if (idx < 0) {
            fprintf(stderr, "BTNEdit: maximale Anzahl Tabs (%d) erreicht\n", MAX_TABS);
            return;
        }
        switch_to_tab(idx);
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
    if (!confirm_discard_doc(&g_docs[idx])) {
        return;
    }

    doc_free(&g_docs[idx]);
    memmove(&g_docs[idx], &g_docs[idx + 1], (size_t)(g_doc_count - idx - 1) * sizeof(Document));
    g_doc_count--;

    if (g_active_doc > idx) {
        g_active_doc--;
    } else if (g_active_doc == idx) {
        if (g_active_doc >= g_doc_count) {
            g_active_doc = g_doc_count - 1;
        }
        switch_to_tab(g_active_doc);
    }
    sync_window_state();
    btn_app_request_redraw();
}

/* Wird vom Shim sowohl beim Klick auf den roten Schliessen-Knopf als auch
 * bei Cmd+Q/"Beende" aufgerufen (windowShouldClose:/applicationShouldTerminate:)
 * - zentral hier statt separat pro Aufrufer, damit keiner dieser beiden
 * System-Wege den Ungesichert-Dialog umgehen kann. Prueft ALLE offenen Tabs,
 * nicht nur den aktiven - das Fenster/die App zu schliessen wuerde sonst
 * ungesicherte Aenderungen in Hintergrund-Tabs stillschweigend verwerfen. */
static int should_close(void) {
    for (int i = 0; i < g_doc_count; i++) {
        if (!confirm_discard_doc(&g_docs[i])) {
            return 0;
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

    Document *active = active_doc();
    btn_render_frame(ctx, content_bounds(), &active->editor, active->scroll_row,
                      btn_highlight_lang_for_path(active->path));
}

/* Klick irgendwo in der Tableiste (y schon vom Aufrufer geprueft): trifft
 * entweder den "+"-Knopf nach dem letzten Tab, das Schliessen-"x" eines
 * Tabs, oder den Tab-Koerper selbst (Tab wechseln). */
static void handle_tab_bar_click(double x) {
    double new_x = (double)g_doc_count * BTN_TAB_ITEM_WIDTH;
    if (x >= new_x && x < new_x + BTN_TAB_NEW_WIDTH) {
        new_tab_or_reuse_blank();
        return;
    }

    int index = (int)(x / BTN_TAB_ITEM_WIDTH);
    if (index < 0 || index >= g_doc_count) {
        return;
    }
    double tab_x = (double)index * BTN_TAB_ITEM_WIDTH;
    int close_clicked = (x >= tab_x + BTN_TAB_ITEM_WIDTH - BTN_TAB_CLOSE_WIDTH);
    if (close_clicked) {
        close_tab(index);
    } else if (index != g_active_doc) {
        switch_to_tab(index);
    }
}

static void on_key(const char *characters, unsigned short keycode, unsigned long modifierFlags) {
    int shift = (modifierFlags & BTN_MOD_SHIFT) != 0;
    int option = (modifierFlags & BTN_MOD_OPTION) != 0;
    int command = (modifierFlags & BTN_MOD_COMMAND) != 0;
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
        editor_insert_text(ed, characters, strlen(characters));
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
            if (y < BTN_FOOTER_HEIGHT) {
                return;
            }
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
            editor_insert_text(&active_doc()->editor, clip, clip_len);
            free(clip);
            break;
        }
        case BTN_MENU_SELECT_ALL:
            editor_select_all(&active_doc()->editor);
            break;
        case BTN_MENU_PRINT:
        case BTN_MENU_FIND:
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
