/* Live-Suche: Komplexitaetsdeckel fuer {n,m}-Wiederholungen
 * (regex_too_expensive_for_live_search aus main.c). */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "expensive_extracted.h"
int main(void) {
    struct { const char *p; int want; } c[] = {
        /* muessen live bleiben */
        { "([0-9]{1,3}\\.){3}[0-9]{1,3}", 0 }, { "[0-9]{1,3}(\\.[0-9]{1,3}){3}", 0 },
        { "([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", 0 }, { "(\\d{3}-){2}\\d{4}", 0 }, { "a{2}{2}", 0 },
        { "a{64}", 0 }, { "a{255}", 0 }, { "a{2,64}", 0 }, { "[{]{3}", 0 }, { "\\{100}", 0 }, { "(ab){3}(cd){4}", 0 },
        { "[[:digit:]]{4}-[[:digit:]]{2}", 0 }, { "(a{2})*", 0 }, { "x{", 0 }, { "{5}", 0 }, { "[]{]{5}", 0 },
        { "[^]a]{5}", 0 }, { "(a)(b)(c)", 0 }, { "foo.*bar", 0 }, { "^\\s+$", 0 }, { "", 0 }, { "a{2", 0 },
        { "(a{2}", 0 }, { "a{2})){3}", 0 }, { "a{30}b{30}c{30}d{30}", 0 }, { "(a{30}|b){30}", 0 },
        { "(a{10}){10}{10}", 0 },
        /* muessen raus */
        { "((a{255}){255}){255}", 1 }, { "a{1001}", 1 }, { "a{99999999999}", 1 }, { "a{64}{64}{64}", 1 },
        { "a{60}*{60}*{60}*{60}", 1 }, { "(((a{60})+{60})+{60})+{60}", 1 }, { "a{,255}{,255}", 1 },
        { "(a{,255}){,255}", 1 }, { "((a{,200}){,200}){,200}", 1 }, { "(a{40}){40}", 1 }, { "((a{2})b){600}", 1 },
        { "(x(a{40}))(y){40}", 0 }, { "(x(a{40})y){40}", 1 },
        { "((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((a)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))", 1 },
    };
    int fails = 0;
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        int got = regex_too_expensive_for_live_search(c[i].p, strlen(c[i].p));
        if (got != c[i].want) { printf("FAIL %s -> %d\n", c[i].p, got); fails++; }
    }
    printf("%s: %zu cases\n", fails ? "FAILED" : "ALL TESTS PASSED", sizeof c / sizeof c[0]);
    return fails != 0;
}
