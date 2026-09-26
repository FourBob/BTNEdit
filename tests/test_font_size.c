/* Schriftgroesse klemmen: NaN/inf aus einer handeditierten Prefs-Datei
 * (btn_render_set_font_size aus render.c). */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#define BTN_MIN_FONT_SIZE 8.0
#define BTN_MAX_FONT_SIZE 32.0
typedef const void *CFTypeRef;
static double g_font_size = 13.0; static void *g_font = NULL; static double g_char_width = 0.0;
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
        g_font_size = 13.0; btn_render_set_font_size(v);
        int ok = g_font_size == c[i].want && !isnan(g_font_size);
        printf("%-6s -> %g %s\n", c[i].in, g_font_size, ok ? "ok" : "FAIL");
        fails += !ok;
    }
    (void)g_font; (void)g_char_width;
    printf("%s\n", fails ? "FAILED" : "ALL PASSED"); return fails;
}
