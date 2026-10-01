/* Tab-Beschriftung: Kuerzen nach Zeichen statt Bytes, nie mitten in einem
 * UTF-8-Zeichen. */
#include <stdio.h>
#include <string.h>

/* utf8_safe_cut + utf8_prefix_bytes werden verbatim aus render.c extrahiert
 * (siehe Kompilierbefehl), damit exakt der echte Code getestet wird. */
#include "render_helpers_extracted.h"

static int failures = 0;

static void check(size_t got, size_t expected, const char *label) {
    if (got != expected) {
        printf("FAIL [%s] got %zu, expected %zu\n", label, got, expected);
        failures++;
    } else {
        printf("ok   [%s] = %zu\n", label, got);
    }
}

/* Nachgebaute Kuerzungslogik aus btn_render_tab_bar() (ohne CoreText). */
static void truncate_like_tab_bar(const char *label, size_t max_chars, char *buf, size_t bufsize) {
    size_t label_len = utf8_prefix_bytes(label, max_chars);
    int truncated = label[label_len] != '\0';
    if (truncated) {
        label_len = utf8_prefix_bytes(label, max_chars > 3 ? max_chars - 3 : max_chars);
    }
    if (label_len >= bufsize - 4) {
        label_len = utf8_safe_cut(label, bufsize - 4);
    }
    memcpy(buf, label, label_len);
    buf[label_len] = '\0';
    if (truncated) {
        strcat(buf, "...");
    }
}

int main(void) {
    /* "Müller.txt": M(1) ü(2) l l e r . t x t = 10 Zeichen, 11 Bytes */
    const char *name = "M\xC3\xBCller.txt";
    check(strlen(name), 11, "Müller.txt byte length");
    check(utf8_prefix_bytes(name, 0), 0, "prefix 0 chars");
    check(utf8_prefix_bytes(name, 1), 1, "prefix 1 char (M)");
    check(utf8_prefix_bytes(name, 2), 3, "prefix 2 chars (Mü) -> 3 bytes");
    check(utf8_prefix_bytes(name, 3), 4, "prefix 3 chars (Mül)");
    check(utf8_prefix_bytes(name, 10), 11, "prefix all 10 chars = full length");
    check(utf8_prefix_bytes(name, 99), 11, "prefix beyond end = full length");

    /* Kernfall: 10 Zeichen passen in 10 Zellen -> darf NICHT gekuerzt werden
     * (die alte strlen-Logik haette bei 11 Bytes > 10 gekuerzt). */
    char buf[256];
    truncate_like_tab_bar(name, 10, buf, sizeof(buf));
    if (strcmp(buf, name) != 0) {
        printf("FAIL [fits in 10 cells] got '%s', expected unchanged '%s'\n", buf, name);
        failures++;
    } else {
        printf("ok   [fits in 10 cells] unchanged\n");
    }

    /* 9 Zellen: 10 Zeichen passen nicht -> 6 Zeichen + "..." = "Müller..." */
    truncate_like_tab_bar(name, 9, buf, sizeof(buf));
    if (strcmp(buf, "M\xC3\xBCller...") != 0) {
        printf("FAIL [9 cells] got '%s'\n", buf);
        failures++;
    } else {
        printf("ok   [9 cells] = 'Müller...'\n");
    }

    /* Schnitt darf nie mitten im Umlaut landen: 2 Zellen -> max_chars<=3
     * -> label_len = prefix(2) = "Mü" (3 Bytes) + "..." */
    truncate_like_tab_bar(name, 2, buf, sizeof(buf));
    if (strcmp(buf, "M\xC3\xBC...") != 0) {
        printf("FAIL [2 cells] got '%s'\n", buf);
        failures++;
    } else {
        printf("ok   [2 cells] = 'Mü...' (Umlaut intakt)\n");
    }

    /* Reines ASCII: verhaelt sich exakt wie vorher. */
    truncate_like_tab_bar("document.txt", 8, buf, sizeof(buf));
    if (strcmp(buf, "docum...") != 0) {
        printf("FAIL [ascii 8 cells] got '%s'\n", buf);
        failures++;
    } else {
        printf("ok   [ascii 8 cells] = 'docum...'\n");
    }

    /* Leerer String */
    check(utf8_prefix_bytes("", 5), 0, "empty string");

    if (failures == 0) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    }
    printf("\n%d TEST(S) FAILED\n", failures);
    return 1;
}
