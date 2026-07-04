/*
 * Reines C: Applikationslogik, Platzhalter-Buffer und Verdrahtung der
 * Callbacks aus dem ObjC-Shim (shim.m). Der Buffer hier ist bewusst simpel
 * (nur Anhaengen/Backspace am Ende) - er dient nur dazu, das Fenster in
 * Meilenstein 1 testbar zu machen. Die eigentliche Text-Engine (Cursor an
 * beliebiger Position, Selektion, Undo/Redo) folgt in Meilenstein 2.
 */
#include "shim.h"
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAP 4096

static char *g_buf = NULL;
static size_t g_len = 0;
static size_t g_cap = 0;

static void ensure_cap(size_t extra) {
    if (g_len + extra + 1 > g_cap) {
        size_t new_cap = g_cap == 0 ? INITIAL_CAP : g_cap * 2;
        while (new_cap < g_len + extra + 1) {
            new_cap *= 2;
        }
        g_buf = realloc(g_buf, new_cap);
        g_cap = new_cap;
    }
}

static void buffer_append(const char *s) {
    size_t n = strlen(s);
    ensure_cap(n);
    memcpy(g_buf + g_len, s, n);
    g_len += n;
    g_buf[g_len] = '\0';
}

static void buffer_backspace(void) {
    if (g_len > 0) {
        g_len--;
        g_buf[g_len] = '\0';
    }
}

static void buffer_clear(void) {
    g_len = 0;
    if (g_buf) {
        g_buf[0] = '\0';
    }
}

static void on_draw(CGContextRef ctx, CGRect bounds) {
    btn_render_frame(ctx, bounds, g_buf ? g_buf : "");
}

static void on_key(const char *characters, unsigned short keycode, unsigned long modifierFlags) {
    (void)keycode;
    (void)modifierFlags;
    if (!characters) {
        return;
    }

    unsigned char c = (unsigned char)characters[0];
    if (c == '\r') {
        buffer_append("\n");
    } else if (c == 0x7F) {
        buffer_backspace();
    } else if (c >= 0x20) {
        buffer_append(characters);
    }

    btn_app_request_redraw();
}

static void on_resize(CGSize size) {
    (void)size;
    btn_app_request_redraw();
}

static void on_menu(int tag) {
    switch (tag) {
        case BTN_MENU_NEW:
            buffer_clear();
            btn_app_request_redraw();
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
}

int main(void) {
    ensure_cap(0);
    g_buf[0] = '\0';

    btn_app_init();
    btn_app_set_draw_callback(on_draw);
    btn_app_set_key_callback(on_key);
    btn_app_set_resize_callback(on_resize);
    btn_app_set_menu_callback(on_menu);
    btn_app_build_menu();
    btn_app_run();

    free(g_buf);
    return 0;
}
