/* Sichtbare Rows: die letzte gezaehlte Row liegt immer ganz ueber der
 * Statuszeile (btn_visible_row_capacity). */
#include <stdio.h>
#define BTN_LINE_HEIGHT 18.0
#define BTN_FOOTER_HEIGHT 22.0
#define LINE_HEIGHT BTN_LINE_HEIGHT
#define TOP_PADDING 8.0
#include "cap_extracted.h"
/* Alte Formel zum Vergleich */
static long old_cap(double h) { long n = (long)((h - BTN_FOOTER_HEIGHT) / BTN_LINE_HEIGHT); return n > 0 ? n : 1; }
int main(void) {
    long fails = 0, checks = 0, old_bad = 0;
    for (double h = 0.0; h <= 2400.0; h += 0.25) {
        long cap = btn_visible_row_capacity(h);
        checks++;
        if (cap < 1) { fails++; printf("cap<1 at %.2f\n", h); }
        double top_last = h - TOP_PADDING - (double)cap * LINE_HEIGHT;       /* Row cap-1 */
        double top_next = h - TOP_PADDING - (double)(cap + 1) * LINE_HEIGHT; /* Row cap */
        /* Letzte gezaehlte Row voll ueber dem Footer (ausser im Mindest-1-Fall bei winzigem Fenster) */
        if (h - TOP_PADDING - BTN_FOOTER_HEIGHT >= LINE_HEIGHT && top_last < BTN_FOOTER_HEIGHT) { fails++; printf("last row clipped h=%.2f\n", h); }
        /* maximal: die naechste Row passt nicht mehr */
        if (top_next >= BTN_FOOTER_HEIGHT) { fails++; printf("not maximal h=%.2f\n", h); }
        long oc = old_cap(h);
        if (oc >= 1 && h - TOP_PADDING - (double)oc * LINE_HEIGHT < BTN_FOOTER_HEIGHT && h > 100) old_bad++;
    }
    printf("600pt: cap=%ld (old %ld)\n", btn_visible_row_capacity(600), old_cap(600));
    printf("old formula clipped last row at %ld of the heights\n", old_bad);
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
