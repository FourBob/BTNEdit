/* Tabelle vollstaendig (kein NULL = keine fehlende Spalte), Footer-Formate
 * pro Sprache gueltig, und der Validator lehnt kaputte Formate ab. */
#include <stdio.h>
#include <string.h>
#include "strings.h"
int btn_footer_format_ok(const char *fmt, int n);
#include "footer_fmt_extracted.h"
int main(void) {
    int fails = 0, checks = 0;
    for (int l = 0; l < BTN_LANG_COUNT; l++) {
        btn_strings_set_language((BtnUiLang)l);
        int present = 0;
        for (int id = 0; id < BTN_STR_COUNT; id++) { checks++; if (btn_tr((BtnStringId)id)) present++; else { fails++; printf("lang %d id %d NULL\n", l, id); } }
        printf("lang %d: %d/%d strings\n", l, present, BTN_STR_COUNT);
        checks += 2;
        if (!btn_footer_format_ok(btn_tr(BTN_STR_FOOTER_POS_FMT), 2)) { fails++; printf("lang %d pos fmt bad\n", l); }
        if (!btn_footer_format_ok(btn_tr(BTN_STR_FOOTER_STATS_FMT), 3)) { fails++; printf("lang %d stats fmt bad\n", l); }
        char buf[160];
        snprintf(buf, sizeof buf, btn_tr(BTN_STR_FOOTER_POS_FMT), (size_t)12, (size_t)3);
        printf("   %s   ", buf);
        snprintf(buf, sizeof buf, btn_tr(BTN_STR_FOOTER_STATS_FMT), (size_t)100, (size_t)250, (size_t)1234);
        printf("%s\n   too-large: %s\n", buf, btn_tr(BTN_STR_FIND_LIVE_SEARCH_TOO_LARGE));
        /* Nachbarn verschoben? Letzter alter Eintrag darf kein Footer-Format sein. */
        checks++;
        if (strstr(btn_tr(BTN_STR_FIND_LIVE_SEARCH_TOO_LARGE), "%")) { fails++; printf("shifted\n"); }
    }
    struct { const char *f; int n; int want; } v[] = {
        { "Line %zu, Column %zu", 2, 1 }, { "%zu%%", 1, 1 }, { "%d %zu", 2, 0 }, { "%zu %s", 1, 0 },
        { "%zu", 2, 0 }, { "%zu %zu %zu", 2, 0 }, { "%", 0, 0 }, { NULL, 0, 0 }, { "plain", 0, 1 }, { "%z", 0, 0 },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        checks++;
        if (btn_footer_format_ok(v[i].f, v[i].n) != v[i].want) { fails++; printf("validator case %zu wrong\n", i); }
    }
    printf("%s: %d checks, %d failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
