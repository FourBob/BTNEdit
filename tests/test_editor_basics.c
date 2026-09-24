/* Editor-Grundlagen: Pfeiltasten zeichenweise, Klammer-Umschliessen mit NUL-
 * Bytes, Backspace/Entf auf Mehrbyte-Zeichen. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "editor.h"

static int failures = 0;

static void expect_cursor(Editor *ed, size_t expected, const char *label) {
    if (ed->cursor != expected) {
        printf("FAIL [%s] cursor=%zu expected %zu\n", label, ed->cursor, expected);
        failures++;
    } else {
        printf("ok   [%s] cursor=%zu\n", label, ed->cursor);
    }
}

static void expect_bytes(Editor *ed, const char *expected, size_t expected_len, const char *label) {
    size_t len;
    char *text = editor_copy_all(ed, &len);
    if (len != expected_len || memcmp(text, expected, len) != 0) {
        printf("FAIL [%s] got %zu bytes:", label, len);
        for (size_t i = 0; i < len; i++) printf(" %02X", (unsigned char)text[i]);
        printf("  expected:");
        for (size_t i = 0; i < expected_len; i++) printf(" %02X", (unsigned char)expected[i]);
        printf("\n");
        failures++;
    } else {
        printf("ok   [%s] bytes match\n", label);
    }
    free(text);
}

int main(void) {
    Editor ed;

    /* ---- Fix 2: Pfeil links/rechts zeichenweise ---- */
    editor_init(&ed);
    editor_insert_text(&ed, "a\xC3\xA4" "b", 4);         /* a ä b = 4 Bytes, 3 Zeichen */
    editor_move(&ed, BTN_MOVE_DOC_END, 0);
    expect_cursor(&ed, 4, "END");
    editor_move(&ed, BTN_MOVE_LEFT, 0);
    expect_cursor(&ed, 3, "LEFT over b");
    editor_move(&ed, BTN_MOVE_LEFT, 0);
    expect_cursor(&ed, 1, "LEFT over ä (skips 2 bytes)");
    editor_move(&ed, BTN_MOVE_LEFT, 0);
    expect_cursor(&ed, 0, "LEFT over a");
    editor_move(&ed, BTN_MOVE_LEFT, 0);
    expect_cursor(&ed, 0, "LEFT at start stays");
    editor_move(&ed, BTN_MOVE_RIGHT, 0);
    expect_cursor(&ed, 1, "RIGHT over a");
    editor_move(&ed, BTN_MOVE_RIGHT, 0);
    expect_cursor(&ed, 3, "RIGHT over ä (skips 2 bytes)");
    editor_move(&ed, BTN_MOVE_RIGHT, 0);
    expect_cursor(&ed, 4, "RIGHT over b");
    editor_move(&ed, BTN_MOVE_RIGHT, 0);
    expect_cursor(&ed, 4, "RIGHT at end stays");
    /* Shift-Selektion springt ebenfalls zeichenweise */
    editor_move(&ed, BTN_MOVE_LEFT, 1);
    editor_move(&ed, BTN_MOVE_LEFT, 1);
    if (editor_selection_start(&ed) != 1 || editor_selection_end(&ed) != 4) {
        printf("FAIL [shift-left x2] selection [%zu,%zu) expected [1,4)\n",
               editor_selection_start(&ed), editor_selection_end(&ed));
        failures++;
    } else {
        printf("ok   [shift-left x2] selection [1,4)\n");
    }
    editor_free(&ed);

    /* Der urspruengliche Repro: "a€b", End, Left, Left, dann X tippen -> gueltiges UTF-8 */
    editor_init(&ed);
    editor_insert_text(&ed, "a\xE2\x82\xAC" "b", 5);        /* a € b */
    editor_move(&ed, BTN_MOVE_DOC_END, 0);
    editor_move(&ed, BTN_MOVE_LEFT, 0);
    editor_move(&ed, BTN_MOVE_LEFT, 0);
    expect_cursor(&ed, 1, "a€b End Left Left -> before €");
    editor_insert_text(&ed, "X", 1);
    expect_bytes(&ed, "aX\xE2\x82\xAC" "b", 6, "type X keeps € intact");
    editor_free(&ed);

    /* Backspace nach Left Left ueber € loescht 'a', nicht ein Byte von € */
    editor_init(&ed);
    editor_insert_text(&ed, "a\xE2\x82\xAC" "b", 5);
    editor_move(&ed, BTN_MOVE_DOC_END, 0);
    editor_move(&ed, BTN_MOVE_LEFT, 0);
    editor_move(&ed, BTN_MOVE_LEFT, 0);
    editor_delete_backward(&ed);
    expect_bytes(&ed, "\xE2\x82\xAC" "b", 4, "backspace deletes 'a', € intact");
    editor_free(&ed);

    /* ASCII unveraendert */
    editor_init(&ed);
    editor_insert_text(&ed, "hello", 5);
    editor_move(&ed, BTN_MOVE_DOC_END, 0);
    editor_move(&ed, BTN_MOVE_LEFT, 0);
    expect_cursor(&ed, 4, "ascii LEFT");
    editor_free(&ed);

    /* ---- Fix 7: Klammer-Wrap mit NUL in der Selektion ---- */
    editor_init(&ed);
    editor_insert_text(&ed, "a\0" "b", 3);
    editor_set_cursor(&ed, 0, 0);
    editor_set_cursor(&ed, 3, 1);
    if (!editor_handle_bracket_key(&ed, '(')) {
        printf("FAIL [wrap] bracket key not handled\n");
        failures++;
    }
    expect_bytes(&ed, "(a\0" "b)", 5, "wrap keeps bytes after NUL");
    if (editor_selection_start(&ed) != 1 || editor_selection_end(&ed) != 4) {
        printf("FAIL [wrap] selection [%zu,%zu) expected [1,4)\n",
               editor_selection_start(&ed), editor_selection_end(&ed));
        failures++;
    } else {
        printf("ok   [wrap] selection re-established [1,4)\n");
    }
    editor_free(&ed);

    printf(failures ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
