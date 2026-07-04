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

static long g_scroll_line = 0;
static double g_scroll_accum = 0.0;

static char *g_current_path = NULL; /* NULL = unbenanntes, neues Dokument */
static size_t g_saved_undo_pos = 0;

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
    return g_editor.undo.pos != g_saved_undo_pos;
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

static void clamp_scroll(void) {
    if (g_scroll_line < 0) {
        g_scroll_line = 0;
    }
    long max_scroll = (long)editor_line_count(&g_editor) - visible_line_capacity();
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (g_scroll_line > max_scroll) {
        g_scroll_line = max_scroll;
    }
}

/* Scrollt automatisch nach, damit der Cursor immer sichtbar bleibt -
 * bei Tastatur-Navigation gibt es sonst keinen anderen Weg, ihn wieder
 * ins Bild zu bekommen. */
static void sync_scroll_to_cursor(void) {
    long cur_line = (long)editor_offset_to_line(&g_editor, g_editor.cursor);
    long capacity = visible_line_capacity();

    if (cur_line < g_scroll_line) {
        g_scroll_line = cur_line;
    } else if (cur_line >= g_scroll_line + capacity) {
        g_scroll_line = cur_line - capacity + 1;
    }
    clamp_scroll();
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
        g_saved_undo_pos = g_editor.undo.pos;
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

static void on_draw(CGContextRef ctx, CGRect bounds) {
    g_bounds = bounds;
    btn_render_frame(ctx, bounds, &g_editor, g_scroll_line);
}

static void on_key(const char *characters, unsigned short keycode, unsigned long modifierFlags) {
    int shift = (modifierFlags & BTN_MOD_SHIFT) != 0;
    int option = (modifierFlags & BTN_MOD_OPTION) != 0;
    int command = (modifierFlags & BTN_MOD_COMMAND) != 0;

    switch (keycode) {
        case KEYCODE_LEFT:
            editor_move(&g_editor, command ? BTN_MOVE_LINE_START : (option ? BTN_MOVE_WORD_LEFT : BTN_MOVE_LEFT), shift);
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_RIGHT:
            editor_move(&g_editor, command ? BTN_MOVE_LINE_END : (option ? BTN_MOVE_WORD_RIGHT : BTN_MOVE_RIGHT), shift);
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_UP:
            editor_move(&g_editor, command ? BTN_MOVE_DOC_START : BTN_MOVE_UP, shift);
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_DOWN:
            editor_move(&g_editor, command ? BTN_MOVE_DOC_END : BTN_MOVE_DOWN, shift);
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_HOME:
            editor_move(&g_editor, BTN_MOVE_LINE_START, shift);
            sync_scroll_to_cursor();
            btn_app_request_redraw();
            return;
        case KEYCODE_END:
            editor_move(&g_editor, BTN_MOVE_LINE_END, shift);
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
            size_t offset = btn_hit_test(&g_editor, g_bounds, x, y, g_scroll_line);
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
                size_t offset = btn_hit_test(&g_editor, g_bounds, x, y, g_scroll_line);
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
    g_scroll_line -= lines;
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
                g_saved_undo_pos = g_editor.undo.pos;
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
                        g_saved_undo_pos = g_editor.undo.pos;
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
            if (confirm_discard_if_dirty()) {
                btn_app_close_window();
            }
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
    btn_app_build_menu();
    btn_app_run();

    editor_free(&g_editor);
    return 0;
}
