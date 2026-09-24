/* Cursor-Spalten zaehlen Zeichen, nicht Bytes. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "editor.h"

static int failures = 0;

static void check_col(Editor *ed, size_t range_start, size_t offset, size_t expected, const char *label) {
    size_t got = editor_visual_column_in_range(ed, range_start, offset);
    if (got != expected) {
        printf("FAIL [%s] editor_visual_column_in_range(start=%zu, offset=%zu) = %zu, expected %zu\n",
               label, range_start, offset, got, expected);
        failures++;
    } else {
        printf("ok   [%s] col(%zu..%zu) = %zu\n", label, range_start, offset, got);
    }
}

static void check_reverse(Editor *ed, size_t range_start, size_t range_len, size_t target_col,
                           size_t expected_offset, const char *label) {
    size_t got = editor_offset_for_column_in_range(ed, range_start, range_len, target_col);
    if (got != expected_offset) {
        printf("FAIL [%s] offset_for_column(col=%zu) = %zu, expected %zu\n",
               label, target_col, got, expected_offset);
        failures++;
    } else {
        printf("ok   [%s] offset_for_column(col=%zu) = %zu\n", label, target_col, got);
    }
}

int main(void) {
    Editor ed;
    editor_init(&ed);

    /* "a" (1) + "ä" (2 bytes: 0xC3 0xA4) + "b" (1) + "€" (3 bytes: 0xE2 0x82 0xAC) + "c" (1) */
    const char *text = "a\xC3\xA4""b\xE2\x82\xAC""c";
    size_t len = strlen(text);
    editor_insert_text(&ed, text, len);
    printf("text byte length = %zu (expected 8: 1+2+1+3+1)\n", len);
    assert(len == 8);

    /* Byte offsets: a=0, ä=1..2, b=3, €=4..6, c=7, end=8
     * Visual columns (codepoints): a=0, ä=1, b=2, €=3, c=4, end=5 */
    check_col(&ed, 0, 0, 0, "before a");
    check_col(&ed, 0, 1, 1, "after a, before ä");
    check_col(&ed, 0, 3, 2, "after ä, before b");
    check_col(&ed, 0, 4, 3, "after b, before €");
    check_col(&ed, 0, 7, 4, "after €, before c");
    check_col(&ed, 0, 8, 5, "after c (end)");
    /* offset=2 is right after ä's LEAD byte (index 1) and before its
     * continuation byte (index 2) - the lead byte is already fully within
     * [0,2), so it correctly counts as 1 column; col=2 total (a + ä). */
    check_col(&ed, 0, 2, 2, "right after ä's lead byte");

    check_reverse(&ed, 0, len, 0, 0, "col 0 -> a");
    check_reverse(&ed, 0, len, 1, 1, "col 1 -> ä start");
    check_reverse(&ed, 0, len, 2, 3, "col 2 -> b");
    check_reverse(&ed, 0, len, 3, 4, "col 3 -> € start");
    check_reverse(&ed, 0, len, 4, 7, "col 4 -> c");
    check_reverse(&ed, 0, len, 5, 8, "col 5 -> end");
    check_reverse(&ed, 0, len, 99, 8, "col beyond end -> clamped to end");

    /* Regression: pure ASCII must still behave exactly as before (1 byte = 1 col). */
    editor_free(&ed);
    editor_init(&ed);
    editor_insert_text(&ed, "hello", 5);
    check_col(&ed, 0, 5, 5, "ascii: hello");
    check_reverse(&ed, 0, 5, 3, 3, "ascii reverse: col 3 -> byte 3");

    /* Tabs still work (tab-stop advance, not affected by UTF-8 handling). */
    editor_free(&ed);
    editor_init(&ed);
    editor_insert_text(&ed, "a\tb", 3);
    size_t after_tab_col = editor_visual_column_in_range(&ed, 0, 2);
    printf("tab: col after 'a\\t' = %zu (tab stop width dependent, just checking > 1)\n", after_tab_col);
    if (after_tab_col <= 1) {
        printf("FAIL tab did not advance past col 1\n");
        failures++;
    }

    editor_free(&ed);

    if (failures == 0) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    } else {
        printf("\n%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
