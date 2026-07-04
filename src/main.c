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

static void on_draw(CGContextRef ctx, CGRect bounds) {
    g_bounds = bounds;
    btn_render_frame(ctx, bounds, &g_editor);
}

static void on_key(const char *characters, unsigned short keycode, unsigned long modifierFlags) {
    int shift = (modifierFlags & BTN_MOD_SHIFT) != 0;
    int option = (modifierFlags & BTN_MOD_OPTION) != 0;
    int command = (modifierFlags & BTN_MOD_COMMAND) != 0;

    switch (keycode) {
        case KEYCODE_LEFT:
            editor_move(&g_editor, command ? BTN_MOVE_LINE_START : (option ? BTN_MOVE_WORD_LEFT : BTN_MOVE_LEFT), shift);
            btn_app_request_redraw();
            return;
        case KEYCODE_RIGHT:
            editor_move(&g_editor, command ? BTN_MOVE_LINE_END : (option ? BTN_MOVE_WORD_RIGHT : BTN_MOVE_RIGHT), shift);
            btn_app_request_redraw();
            return;
        case KEYCODE_UP:
            editor_move(&g_editor, command ? BTN_MOVE_DOC_START : BTN_MOVE_UP, shift);
            btn_app_request_redraw();
            return;
        case KEYCODE_DOWN:
            editor_move(&g_editor, command ? BTN_MOVE_DOC_END : BTN_MOVE_DOWN, shift);
            btn_app_request_redraw();
            return;
        case KEYCODE_HOME:
            editor_move(&g_editor, BTN_MOVE_LINE_START, shift);
            btn_app_request_redraw();
            return;
        case KEYCODE_END:
            editor_move(&g_editor, BTN_MOVE_LINE_END, shift);
            btn_app_request_redraw();
            return;
        case KEYCODE_FORWARD_DELETE:
            editor_delete_forward(&g_editor);
            btn_app_request_redraw();
            return;
        case KEYCODE_TAB:
            editor_insert_text(&g_editor, "\t", 1);
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

    btn_app_request_redraw();
}

static void on_resize(CGSize size) {
    g_bounds = CGRectMake(0, 0, size.width, size.height);
    btn_app_request_redraw();
}

static void on_mouse(btn_mouse_phase phase, double x, double y, int clickCount, unsigned long modifierFlags) {
    int shift = (modifierFlags & BTN_MOD_SHIFT) != 0;

    switch (phase) {
        case BTN_MOUSE_DOWN: {
            size_t offset = btn_hit_test(&g_editor, g_bounds, x, y);
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
                size_t offset = btn_hit_test(&g_editor, g_bounds, x, y);
                editor_set_cursor(&g_editor, offset, 1);
            }
            break;
        case BTN_MOUSE_UP:
            g_dragging = 0;
            break;
    }

    btn_app_request_redraw();
}

static void on_menu(int tag) {
    char *clip;
    switch (tag) {
        case BTN_MENU_NEW:
            editor_free(&g_editor);
            editor_init(&g_editor);
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
        case BTN_MENU_OPEN:
        case BTN_MENU_SAVE:
        case BTN_MENU_SAVE_AS:
        case BTN_MENU_CLOSE:
        case BTN_MENU_PRINT:
        case BTN_MENU_FIND:
            fprintf(stderr, "BTNEdit: Menu-Aktion %d noch nicht implementiert\n", tag);
            break;
        default:
            break;
    }
    btn_app_request_redraw();
}

int main(void) {
    editor_init(&g_editor);

    btn_app_init();
    btn_app_set_draw_callback(on_draw);
    btn_app_set_key_callback(on_key);
    btn_app_set_resize_callback(on_resize);
    btn_app_set_mouse_callback(on_mouse);
    btn_app_set_menu_callback(on_menu);
    btn_app_build_menu();
    btn_app_run();

    editor_free(&g_editor);
    return 0;
}
