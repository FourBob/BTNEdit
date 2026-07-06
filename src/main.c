/*
 * Reines C: Applikationslogik und Verdrahtung der Callbacks aus dem
 * ObjC-Shim (shim.m) mit der Editor-Engine (editor.c/gapbuffer.c).
 */
#include "shim.h"
#include "render.h"
#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEYCODE_LEFT           123
#define KEYCODE_RIGHT          124
#define KEYCODE_DOWN           125
#define KEYCODE_UP             126
#define KEYCODE_HOME           115
#define KEYCODE_END            119
#define KEYCODE_FORWARD_DELETE 117
#define KEYCODE_TAB            48

static Editor g_editor;
static CGRect g_bounds = { { 0, 0 }, { 900, 600 } };
static int g_dragging = 0;

static long g_scroll_row = 0;
static double g_scroll_accum = 0.0;

static char *g_current_path = NULL; /* NULL = unbenanntes, neues Dokument */
static size_t g_saved_edit_seq = 0;

static char *dup_string(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    memcpy(copy, s, len);
    return copy;
}

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int is_dirty(void) {
    /* Bewusst ueber edit_seq statt ueber undo.pos: Undo-Coalescing kann
     * pos unveraendert lassen, obwohl sich der Inhalt geaendert hat (siehe
     * editor.h-Kommentar bei edit_seq). */
    return g_editor.edit_seq != g_saved_edit_seq;
}

static void set_current_path(const char *path) {
    /* path darf mit g_current_path identisch sein (z.B. bei einem
     * erneuten "Sichern" auf denselben Pfad) - deshalb erst die Kopie
     * anlegen und danach erst den alten Speicher freigeben, sonst wuerde
     * dup_string aus bereits freigegebenem Speicher lesen. */
    char *copy = path ? dup_string(path) : NULL;
    free(g_current_path);
    g_current_path = copy;
    btn_set_window_title(g_current_path ? basename_of(g_current_path) : "Unbenannt");
}

static void sync_window_state(void) {
    btn_app_set_document_edited(is_dirty());
}

static long visible_line_capacity(void) {
    double content_height = g_bounds.size.height - BTN_FOOTER_HEIGHT;
    long n = (long)(content_height / BTN_LINE_HEIGHT);
    return n > 0 ? n : 1;
}

/* Baut das aktuelle Zeilenumbruch-Layout fuer die momentane Fensterbreite;
 * caller muss btn_layout_free(*out_rows) aufrufen. */
static size_t build_current_rows(BtnRow **out_rows) {
    double width = btn_layout_text_width(g_bounds);
    size_t row_count;
    *out_rows = btn_layout_build(&g_editor, width, &row_count);
    return row_count;
}

static void clamp_scroll(void) {
    if (g_scroll_row < 0) {
        g_scroll_row = 0;
    }
    BtnRow *rows;
    long row_count = (long)build_current_rows(&rows);
    btn_layout_free(rows);

    long max_scroll = row_count - visible_line_capacity();
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (g_scroll_row > max_scroll) {
        g_scroll_row = max_scroll;
    }
}

/* Scrollt automatisch nach, damit der Cursor immer sichtbar bleibt -
 * bei Tastatur-Navigation gibt es sonst keinen anderen Weg, ihn wieder
 * ins Bild zu bekommen. */
static void sync_scroll_to_cursor(void) {
    BtnRow *rows;
    size_t row_count = build_current_rows(&rows);
    long cur_row = (long)btn_layout_row_for_offset(rows, row_count, g_editor.cursor);
    btn_layout_free(rows);

    long capacity = visible_line_capacity();
    if (cur_row < g_scroll_row) {
        g_scroll_row = cur_row;
    } else if (cur_row >= g_scroll_row + capacity) {
        g_scroll_row = cur_row - capacity + 1;
    }
    clamp_scroll();
}

/* Wortumbruch-bewusste vertikale Bewegung (Auf/Ab bewegen sich um eine
 * visuelle Zeile, nicht um eine logische) - editor_move kennt das nicht,
 * weil der Umbruch von der Fensterbreite abhaengt. Nutzt ed->desired_col
 * genau wie editor.c es fuer die (jetzt entfernte) logische Variante tat. */
static void move_visual_row(int direction, int extend) {
    BtnRow *rows;
    size_t row_count = build_current_rows(&rows);
    size_t cur_row = btn_layout_row_for_offset(rows, row_count, g_editor.cursor);

    size_t col = (g_editor.desired_col != (size_t)-1)
                     ? g_editor.desired_col
                     : editor_visual_column_in_range(&g_editor, rows[cur_row].start, g_editor.cursor);

    size_t new_offset;
    if (direction < 0 && cur_row == 0) {
        new_offset = 0;
    } else if (direction > 0 && cur_row + 1 >= row_count) {
        new_offset = editor_length(&g_editor);
    } else {
        size_t target_row = (direction < 0) ? cur_row - 1 : cur_row + 1;
        new_offset = editor_offset_for_column_in_range(&g_editor, rows[target_row].start, rows[target_row].len, col);
    }
    btn_layout_free(rows);

    g_editor.cursor = new_offset;
    if (!extend) {
        g_editor.anchor = new_offset;
    }
    g_editor.desired_col = col;
}

/* Pos1/Ende und Cmd+Links/Rechts springen an Anfang/Ende der aktuellen
 * visuellen Zeile (nach Umbruch) - das entspricht dem nativen macOS-
 * Verhalten (nicht der logischen, evtl. umgebrochenen Zeile). */
static void move_row_start(int extend) {
    BtnRow *rows;
    size_t row_count = build_current_rows(&rows);
    size_t cur_row = btn_layout_row_for_offset(rows, row_count, g_editor.cursor);
    size_t new_offset = rows[cur_row].start;
    btn_layout_free(rows);

    g_editor.cursor = new_offset;
    if (!extend) {
        g_editor.anchor = new_offset;
    }
    g_editor.desired_col = (size_t)-1;
}

static void move_row_end(int extend) {
    BtnRow *rows;
    size_t row_count = build_current_rows(&rows);
    size_t cur_row = btn_layout_row_for_offset(rows, row_count, g_editor.cursor);
    size_t new_offset = rows[cur_row].start + rows[cur_row].len;
    btn_layout_free(rows);

    g_editor.cursor = new_offset;
    if (!extend) {
        g_editor.anchor = new_offset;
    }
    g_editor.desired_col = (size_t)-1;
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

/* force_save_as: immer den Sichern-Dialog zeigen, auch wenn schon ein Pfad
 * bekannt ist. Rueckgabe: 1 = gesichert, 0 = abgebrochen/fehlgeschlagen. */
static int perform_save(int force_save_as) {
    char *path = NULL;
    int must_free_path = 0;

    if (force_save_as || !g_current_path) {
        path = btn_show_save_panel(g_current_path);
        if (!path) {
            return 0;
        }
        must_free_path = 1;
    } else {
        path = g_current_path;
    }

    size_t len;
    char *contents = editor_copy_all(&g_editor, &len);
    int ok = write_file_contents(path, contents, len);
    free(contents);

    if (ok) {
        set_current_path(path);
        g_saved_edit_seq = g_editor.edit_seq;
    } else {
        fprintf(stderr, "BTNEdit: Datei konnte nicht geschrieben werden: %s\n", path);
    }

    if (must_free_path) {
        free(path);
    }
    return ok;
}

/* Rueckgabe: 1 = Aktion darf fortgesetzt werden, 0 = Abbrechen. */
static int confirm_discard_if_dirty(void) {
    if (!is_dirty()) {
        return 1;
    }
    int choice = btn_show_unsaved_changes_alert(g_current_path ? basename_of(g_current_path) : "Unbenannt");
    if (choice == 0) {
        return 0;
    }
    if (choice == 1) {
        return perform_save(0);
    }
    return 1;
}

/* Wird vom Shim sowohl beim Klick auf den roten Schliessen-Knopf als auch
 * bei Cmd+Q/"Beende" aufgerufen (windowShouldClose:/applicationShouldTerminate:)
 * - zentral hier statt separat pro Aufrufer, damit keiner dieser beiden
 * System-Wege den Ungesichert-Dialog umgehen kann. */
static int should_close(void) {
    return confirm_discard_if_dirty();
}

static void on_draw(CGContextRef ctx, CGRect bounds) {
    g_bounds = bounds;
    btn_render_frame(ctx, bounds, &g_editor, g_scroll_row, btn_highlight_lang_for_path(g_current_path));
}

static void on_key(const char *characters, unsigned short keycode, unsigned long modifierFlags) {
    int shift = (modifierFlags & BTN_MOD_SHIFT) != 0;
    int option = (modifierFlags & BTN_MOD_OPTION) != 0;
    int command = (modifierFlags & BTN_MOD_COMMAND) != 0;

    switch (keycode) {
        case KEYCODE_LEFT:
            if (command) {
                move_row_start(shift);
            } else {
                editor_move(&g_editor, option ? BTN_MOVE_WORD_LEFT : BTN_MOVE_LEFT, shift);
            }
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_RIGHT:
            if (command) {
                move_row_end(shift);
            } else {
                editor_move(&g_editor, option ? BTN_MOVE_WORD_RIGHT : BTN_MOVE_RIGHT, shift);
            }
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_UP:
            if (command) {
                editor_move(&g_editor, BTN_MOVE_DOC_START, shift);
            } else {
                move_visual_row(-1, shift);
            }
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_DOWN:
            if (command) {
                editor_move(&g_editor, BTN_MOVE_DOC_END, shift);
            } else {
                move_visual_row(1, shift);
            }
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_HOME:
            move_row_start(shift);
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_END:
            move_row_end(shift);
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_FORWARD_DELETE:
            editor_delete_forward(&g_editor);
            sync_window_state();
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_TAB:
            editor_insert_text(&g_editor, "\t", 1);
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
        editor_insert_text(&g_editor, "\n", 1);
    } else if (c == 0x7F) {
        editor_delete_backward(&g_editor);
    } else if (c >= 0x20) {
        editor_insert_text(&g_editor, characters, strlen(characters));
    } else {
        return;
    }

    sync_window_state();
    sync_scroll_to_cursor();
    btn_app_request_redraw();
}

static void on_resize(CGSize size) {
    g_bounds = CGRectMake(0, 0, size.width, size.height);
    clamp_scroll();
    btn_app_request_redraw();
}

static void on_mouse(btn_mouse_phase phase, double x, double y, int clickCount, unsigned long modifierFlags) {
    int shift = (modifierFlags & BTN_MOD_SHIFT) != 0;

    switch (phase) {
        case BTN_MOUSE_DOWN: {
            if (y < BTN_FOOTER_HEIGHT) {
                return;
            }
            size_t offset = btn_hit_test(&g_editor, g_bounds, x, y, g_scroll_row);
            if (clickCount >= 3) {
                editor_select_line_at(&g_editor, offset);
                g_dragging = 0;
            } else if (clickCount == 2) {
                editor_select_word_at(&g_editor, offset);
                g_dragging = 0;
            } else {
                editor_set_cursor(&g_editor, offset, shift);
                g_dragging = 1;
            }
            break;
        }
        case BTN_MOUSE_DRAGGED:
            if (g_dragging) {
                size_t offset = btn_hit_test(&g_editor, g_bounds, x, y, g_scroll_row);
                editor_set_cursor(&g_editor, offset, 1);
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
    g_scroll_accum += delta_y;
    long lines = (long)(g_scroll_accum / BTN_LINE_HEIGHT);
    if (lines == 0) {
        return;
    }
    g_scroll_accum -= (double)lines * BTN_LINE_HEIGHT;
    g_scroll_row -= lines;
    clamp_scroll();
    btn_app_request_redraw();
}

static void on_menu(int tag) {
    char *clip;
    switch (tag) {
        case BTN_MENU_NEW:
            if (confirm_discard_if_dirty()) {
                editor_free(&g_editor);
                editor_init(&g_editor);
                set_current_path(NULL);
                g_saved_edit_seq = g_editor.edit_seq;
            }
            break;
        case BTN_MENU_OPEN:
            if (confirm_discard_if_dirty()) {
                char *path = btn_show_open_panel();
                if (path) {
                    size_t len;
                    char *contents = read_file_contents(path, &len);
                    if (contents) {
                        editor_set_text(&g_editor, contents, len);
                        free(contents);
                        set_current_path(path);
                        g_saved_edit_seq = g_editor.edit_seq;
                    } else {
                        fprintf(stderr, "BTNEdit: Datei konnte nicht gelesen werden: %s\n", path);
                    }
                    free(path);
                }
            }
            break;
        case BTN_MENU_SAVE:
            perform_save(0);
            break;
        case BTN_MENU_SAVE_AS:
            perform_save(1);
            break;
        case BTN_MENU_CLOSE:
            /* Kein confirm_discard_if_dirty() hier: [g_window close] loest
             * windowShouldClose: aus, das denselben Check zentral macht (und
             * damit auch den roten Schliessen-Knopf abdeckt) - ein zweiter
             * Check hier wuerde bei "Nicht sichern" den Dialog doppelt zeigen. */
            btn_app_close_window();
            break;
        case BTN_MENU_UNDO:
            editor_undo(&g_editor);
            break;
        case BTN_MENU_REDO:
            editor_redo(&g_editor);
            break;
        case BTN_MENU_CUT:
            clip = editor_get_selection_text(&g_editor);
            btn_pasteboard_set_string(clip);
            free(clip);
            editor_delete_selection(&g_editor);
            break;
        case BTN_MENU_COPY:
            clip = editor_get_selection_text(&g_editor);
            btn_pasteboard_set_string(clip);
            free(clip);
            break;
        case BTN_MENU_PASTE:
            clip = btn_pasteboard_copy_string();
            editor_insert_text(&g_editor, clip, strlen(clip));
            free(clip);
            break;
        case BTN_MENU_SELECT_ALL:
            editor_select_all(&g_editor);
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
    editor_init(&g_editor);

    btn_app_init();
    btn_app_set_draw_callback(on_draw);
    btn_app_set_key_callback(on_key);
    btn_app_set_resize_callback(on_resize);
    btn_app_set_mouse_callback(on_mouse);
    btn_app_set_scroll_callback(on_scroll);
    btn_app_set_menu_callback(on_menu);
    btn_app_set_should_close_callback(should_close);
    btn_app_build_menu();
    btn_app_run();

    editor_free(&g_editor);
    return 0;
}
