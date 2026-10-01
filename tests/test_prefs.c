/* Einstellungen (~/.btnedit_prefs aus main.c): Schriftgroesse, unsichtbare
 * Zeichen und Zeilenumbruch - Sichern, Laden und aeltere Dateien mit
 * weniger Zeilen. HOME zeigt auf ein Testverzeichnis. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* render.c / shim.m */
static double g_font = 13.0;
static int g_r_invisibles = -1, g_r_wrap = -1, g_m_invisibles = -1, g_m_wrap = -1;
static double btn_render_get_font_size(void) { return g_font; }
static void btn_render_set_font_size(double s) { g_font = s; }
static void btn_render_set_show_invisibles(int on) { g_r_invisibles = on; }
static void btn_app_set_show_invisibles_menu(int on) { g_m_invisibles = on; }
static void btn_render_set_wrap(int on) { g_r_wrap = on; }
static void btn_app_set_wrap_menu(int on) { g_m_wrap = on; }
#include "prefs_extracted.h"

static char g_path[512];

static void write_file(const char *text) {
    FILE *f = fopen(g_path, "w");
    fputs(text, f);
    fclose(f);
}

static void reset(void) {
    g_font = 13.0;
    g_show_invisibles = 0;
    g_wrap = 1;
    g_r_invisibles = g_r_wrap = g_m_invisibles = g_m_wrap = -1;
}

int main(void) {
    char dir[] = "/tmp/btn_prefs_XXXXXX";
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return 1;
    }
    setenv("HOME", dir, 1);
    snprintf(g_path, sizeof g_path, "%s/.btnedit_prefs", dir);

    reset();
    load_prefs();
    CHECK(g_wrap == 1 && g_r_wrap == -1 && g_font == 13.0, "no file: defaults, nothing applied");

    g_font = 16.0;
    apply_show_invisibles(1);
    apply_wrap(0);
    CHECK(g_r_wrap == 0 && g_m_wrap == 0 && g_wrap == 0, "apply_wrap sets renderer, menu checkmark and state");
    save_prefs();
    reset();
    load_prefs();
    CHECK(g_font == 16.0 && g_show_invisibles == 1 && g_wrap == 0 && g_r_wrap == 0 && g_m_wrap == 0,
          "round trip: size 16, invisibles on, wrap off (%.1f %d %d)", g_font, g_show_invisibles, g_wrap);

    apply_wrap(1);
    save_prefs();
    reset();
    g_wrap = 0;
    load_prefs();
    CHECK(g_wrap == 1 && g_r_wrap == 1 && g_m_wrap == 1, "wrap on is saved too");

    write_file("14.0\n1\n");
    reset();
    load_prefs();
    CHECK(g_font == 14.0 && g_show_invisibles == 1 && g_wrap == 1 && g_r_wrap == -1,
          "older file with two lines: wrap stays at its default");
    write_file("15.0\n");
    reset();
    load_prefs();
    CHECK(g_font == 15.0 && g_r_invisibles == -1 && g_r_wrap == -1, "one line: only the font size");
    write_file("12.0\n0\n7\n");
    reset();
    load_prefs();
    CHECK(g_wrap == 1 && g_r_wrap == 1, "any non-zero value means wrap on");

    unlink(g_path);
    rmdir(dir);
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
