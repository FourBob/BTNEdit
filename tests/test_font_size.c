/* Schriftgroesse klemmen: NaN/inf aus einer handeditierten Prefs-Datei
 * (btn_render_set_font_size aus render.c) - und die Zeilenhoehe waechst mit
 * dem Zoom, sonst ueberlappten ab ~16pt die Zeilen. */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#define BTN_MIN_FONT_SIZE 8.0
#define BTN_MAX_FONT_SIZE 32.0
#define BTN_DEFAULT_FONT_SIZE 13.0
#define BTN_DEFAULT_LINE_HEIGHT 18.0
typedef const void *CFTypeRef;
typedef const void *CTFontRef;
typedef const void *CFDictionaryRef;
static int inval = 0;
static void CFRelease(CFTypeRef p) { (void)p; }
static void invalidate_style_cache(void) { inval++; }
#include "fontsize_extracted.h"
int main(void) {
    struct { const char *in; double want; } c[] = { { "nan", 8.0 }, { "-nan", 8.0 }, { "inf", 32.0 }, { "-inf", 8.0 },
        { "13", 13.0 }, { "7.9", 8.0 }, { "32.5", 32.0 }, { "8", 8.0 }, { "32", 32.0 }, { "1e308", 32.0 } };
    int fails = 0;
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        double v; sscanf(c[i].in, "%lf", &v);           /* wie main.c's fscanf */
        g_doc_fonts.size = 13.0; btn_render_set_font_size(v);
        int ok = g_doc_fonts.size == c[i].want && !isnan(g_doc_fonts.size);
        printf("%-6s -> %g %s\n", c[i].in, g_doc_fonts.size, ok ? "ok" : "FAIL");
        fails += !ok;
    }
    /* Zeilenhoehe: 18 bei 13pt, waechst mit; immer mindestens ~1.35 x Schrift */
    struct { double size, lh; } h[] = { { 13, 18 }, { 8, 11 }, { 16, 22 }, { 24, 33 }, { 32, 44 } };
    for (size_t i = 0; i < sizeof h / sizeof h[0]; i++) {
        btn_render_set_font_size(h[i].size);
        int ok = btn_render_line_height() == h[i].lh;
        printf("%gpt -> line %g %s\n", h[i].size, btn_render_line_height(), ok ? "ok" : "FAIL");
        fails += !ok;
    }
    for (double sz = BTN_MIN_FONT_SIZE; sz <= BTN_MAX_FONT_SIZE; sz += 1.0) {
        btn_render_set_font_size(sz);
        if (btn_render_line_height() < sz * 1.3) {
            printf("%gpt: line height %g too small FAIL\n", sz, btn_render_line_height());
            fails++;
        }
    }
    btn_render_set_font_size(32);
    int before = inval;
    btn_render_set_font_size(32);
    if (inval != before) { printf("same size: no cache flush FAIL\n"); fails++; }
    printf("%s\n", fails ? "FAILED" : "ALL PASSED"); return fails;
}
