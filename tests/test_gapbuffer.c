/* Gap-Buffer: gb_copy_range ueber die Luecke hinweg gegen eine Referenz. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gapbuffer.h"

/* gb_copy_range (memcpy) gegen eine unabhaengige Referenz, die sich den
 * Inhalt separat in einem normalen Array merkt - ueber zufaellige
 * Einfuege-/Loeschfolgen, damit die Luecke an allen moeglichen Stellen
 * relativ zum kopierten Bereich liegt. */
int main(void) {
    unsigned long rng = 99;
    GapBuffer gb;
    gb_init(&gb, 8);
    char ref[20000];
    size_t ref_len = 0;
    long failures = 0, checks = 0;

    for (int step = 0; step < 20000; step++) {
        rng = rng * 6364136223846793005UL + 1442695040888963407UL;
        unsigned r = (unsigned)(rng >> 33);
        if ((r % 3) != 0 && ref_len < 15000) {
            size_t pos = ref_len ? (r / 3) % (ref_len + 1) : 0;
            char ins[8];
            size_t n = 1 + (r / 7) % 7;
            for (size_t k = 0; k < n; k++) ins[k] = (char)('a' + (r + k) % 26);
            gb_insert(&gb, pos, ins, n);
            memmove(ref + pos + n, ref + pos, ref_len - pos);
            memcpy(ref + pos, ins, n);
            ref_len += n;
        } else if (ref_len > 0) {
            size_t pos = (r / 3) % ref_len;
            size_t n = 1 + (r / 11) % 5;
            if (pos + n > ref_len) n = ref_len - pos;
            gb_delete(&gb, pos, n);
            memmove(ref + pos, ref + pos + n, ref_len - pos - n);
            ref_len -= n;
        }
        checks++;
        if (gb_length(&gb) != ref_len) { failures++; continue; }
        for (int q = 0; q < 4; q++) {
            rng = rng * 6364136223846793005UL + 1442695040888963407UL;
            size_t a = ref_len ? (size_t)(rng >> 33) % (ref_len + 1) : 0;
            size_t b = ref_len ? (size_t)(rng >> 13) % (ref_len + 1) : 0;
            if (a > b) { size_t t = a; a = b; b = t; }
            char *c = gb_copy_range(&gb, a, b - a);
            checks++;
            if (memcmp(c, ref + a, b - a) != 0 || c[b - a] != '\0') failures++;
            for (size_t i = a; i < b; i++) {
                if (gb_char_at(&gb, i) != ref[i]) { failures++; break; }
            }
            free(c);
        }
    }
    gb_free(&gb);
    printf("%ld checks, %ld failures\n%s\n", checks, failures, failures ? "TESTS FAILED" : "ALL TESTS PASSED");
    return failures ? 1 : 0;
}
