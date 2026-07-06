/*
 * Reine Editor-Logik (Cursor, Selektion, Undo/Redo) auf Basis des Gap
 * Buffers - keine Abhaengigkeit von Core Text/Core Graphics. Zeilen- und
 * Spaltenrechnung nimmt an, dass mit einer Monospace-Schrift gerendert
 * wird (siehe render.c), wodurch "Spalte" als Zeichenanzahl statt als
 * Pixelposition gefuehrt werden kann.
 */
#include "editor.h"

#include <stdlib.h>
#include <string.h>

#define UNSET_COL ((size_t)-1)

/* ---- kleine Hilfsfunktionen ---- */

static int is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static size_t word_left(Editor *ed, size_t pos) {
    if (pos == 0) {
        return 0;
    }
    size_t i = pos;
    while (i > 0 && !is_word_char(gb_char_at(&ed->buffer, i - 1))) {
        i--;
    }
    while (i > 0 && is_word_char(gb_char_at(&ed->buffer, i - 1))) {
        i--;
    }
    return i;
}

static size_t word_right(Editor *ed, size_t pos) {
    size_t len = editor_length(ed);
    size_t i = pos;
    while (i < len && !is_word_char(gb_char_at(&ed->buffer, i))) {
        i++;
    }
    while (i < len && is_word_char(gb_char_at(&ed->buffer, i))) {
        i++;
    }
    return i;
}

/* Byte-Laenge des UTF-8-Zeichens, das VOR pos endet (fuer Backspace) -
 * laeuft rueckwaerts ueber Continuation-Bytes (10xxxxxx) bis zum
 * Lead-Byte, hoechstens 4 Bytes (laengste gueltige UTF-8-Sequenz). Ohne
 * das wuerde Backspace bei jedem mehrbytigen Zeichen (Umlaute, Akzente,
 * nicht-lateinische Schrift) nur ein Byte davon loeschen und ein
 * ungueltiges UTF-8-Fragment im Puffer zuruecklassen. */
static size_t utf8_backward_len(Editor *ed, size_t pos) {
    size_t n = 1;
    while (n < 4 && n < pos && (((unsigned char)gb_char_at(&ed->buffer, pos - n)) & 0xC0) == 0x80) {
        n++;
    }
    return n;
}

/* Byte-Laenge des UTF-8-Zeichens, das BEI pos beginnt (fuer Forward Delete). */
static size_t utf8_forward_len(Editor *ed, size_t pos, size_t limit) {
    unsigned char b = (unsigned char)gb_char_at(&ed->buffer, pos);
    size_t n;
    if ((b & 0x80) == 0x00) {
        n = 1;
    } else if ((b & 0xE0) == 0xC0) {
        n = 2;
    } else if ((b & 0xF0) == 0xE0) {
        n = 3;
    } else if ((b & 0xF8) == 0xF0) {
        n = 4;
    } else {
        n = 1; /* ungueltiges Lead-Byte - defensiv nur 1 Byte loeschen */
    }
    if (pos + n > limit) {
        n = limit - pos;
    }
    return n;
}

/* ---- Undo-Stack ---- */

static void undo_stack_init(UndoStack *st) {
    st->records = NULL;
    st->count = 0;
    st->capacity = 0;
    st->pos = 0;
}

static void undo_stack_free(UndoStack *st) {
    for (size_t i = 0; i < st->count; i++) {
        free(st->records[i].text);
    }
    free(st->records);
}

static void undo_stack_truncate_redo(UndoStack *st) {
    for (size_t i = st->pos; i < st->count; i++) {
        free(st->records[i].text);
    }
    st->count = st->pos;
}

static UndoRecord *undo_stack_push_new(UndoStack *st) {
    undo_stack_truncate_redo(st);
    if (st->count == st->capacity) {
        st->capacity = st->capacity ? st->capacity * 2 : 64;
        st->records = realloc(st->records, st->capacity * sizeof(UndoRecord));
    }
    UndoRecord *r = &st->records[st->count++];
    st->pos = st->count;
    return r;
}

static void record_grow(UndoRecord *r, size_t extra) {
    if (r->len + extra <= r->capacity) {
        return;
    }
    r->capacity = r->capacity ? r->capacity * 2 : 16;
    if (r->capacity < r->len + extra) {
        r->capacity = r->len + extra;
    }
    r->text = realloc(r->text, r->capacity);
}

/* Legt einen frischen UndoRecord an und fuellt ihn - der gemeinsame Kern
 * fuer alle drei Stellen, die einen NICHT zusammengefassten Record
 * brauchen (neuer Insert, neuer Delete, Block-Delete einer Selektion). */
static void undo_record_fill(UndoStack *st, int is_insert, size_t pos, const char *src, size_t len) {
    UndoRecord *r = undo_stack_push_new(st);
    r->is_insert = is_insert;
    r->pos = pos;
    r->len = len;
    r->capacity = len;
    r->text = malloc(len ? len : 1);
    memcpy(r->text, src, len);
}

static void undo_push_insert(Editor *ed, size_t pos, const char *text, size_t len) {
    UndoStack *st = &ed->undo;
    int blocked = ed->suppress_coalesce;
    ed->suppress_coalesce = 0;
    if (!blocked && st->pos > 0 && st->pos == st->count) {
        UndoRecord *last = &st->records[st->pos - 1];
        if (last->is_insert && last->pos + last->len == pos && len == 1 &&
            text[0] != '\n' && (last->len == 0 || last->text[last->len - 1] != '\n')) {
            record_grow(last, len);
            memcpy(last->text + last->len, text, len);
            last->len += len;
            return;
        }
    }
    undo_record_fill(st, 1, pos, text, len);
}

static void undo_push_delete(Editor *ed, size_t pos, const char *deleted, size_t len, int backward) {
    UndoStack *st = &ed->undo;
    int blocked = ed->suppress_coalesce;
    ed->suppress_coalesce = 0;
    if (!blocked && st->pos > 0 && st->pos == st->count && len == 1 && deleted[0] != '\n') {
        UndoRecord *last = &st->records[st->pos - 1];
        if (!last->is_insert) {
            if (backward && pos + len == last->pos) {
                record_grow(last, len);
                memmove(last->text + len, last->text, last->len);
                memcpy(last->text, deleted, len);
                last->len += len;
                last->pos = pos;
                return;
            }
            if (!backward && pos == last->pos) {
                record_grow(last, len);
                memcpy(last->text + last->len, deleted, len);
                last->len += len;
                return;
            }
        }
    }
    undo_record_fill(st, 0, pos, deleted, len);
}

static void undo_push_delete_block(Editor *ed, size_t pos, const char *deleted, size_t len) {
    ed->suppress_coalesce = 0;
    undo_record_fill(&ed->undo, 0, pos, deleted, len);
}

/* ---- Lebenszyklus ---- */

void editor_init(Editor *ed) {
    gb_init(&ed->buffer, 4096);
    ed->cursor = 0;
    ed->anchor = 0;
    ed->desired_col = UNSET_COL;
    ed->edit_seq = 0;
    ed->suppress_coalesce = 0;
    undo_stack_init(&ed->undo);
}

void editor_free(Editor *ed) {
    gb_free(&ed->buffer);
    undo_stack_free(&ed->undo);
}

void editor_set_text(Editor *ed, const char *text, size_t len) {
    gb_free(&ed->buffer);
    gb_init(&ed->buffer, len + 64);
    gb_insert(&ed->buffer, 0, text, len);

    ed->cursor = 0;
    ed->anchor = 0;
    ed->desired_col = UNSET_COL;
    ed->edit_seq = 0;
    ed->suppress_coalesce = 0;

    undo_stack_free(&ed->undo);
    undo_stack_init(&ed->undo);
}

/* ---- Abfragen ---- */

size_t editor_length(Editor *ed) {
    return gb_length(&ed->buffer);
}

size_t editor_line_count(Editor *ed) {
    size_t len = editor_length(ed);
    size_t count = 1;
    for (size_t i = 0; i < len; i++) {
        if (gb_char_at(&ed->buffer, i) == '\n') {
            count++;
        }
    }
    return count;
}

void editor_line_bounds(Editor *ed, size_t line_index, size_t *out_start, size_t *out_len) {
    size_t len = editor_length(ed);
    size_t line = 0, start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || gb_char_at(&ed->buffer, i) == '\n') {
            if (line == line_index) {
                *out_start = start;
                *out_len = i - start;
                return;
            }
            line++;
            start = i + 1;
        }
    }
    *out_start = start;
    *out_len = 0;
}

size_t editor_offset_to_line(Editor *ed, size_t offset) {
    size_t len = editor_length(ed);
    if (offset > len) {
        offset = len;
    }
    size_t line = 0;
    for (size_t i = 0; i < offset; i++) {
        if (gb_char_at(&ed->buffer, i) == '\n') {
            line++;
        }
    }
    return line;
}

size_t editor_word_count(Editor *ed) {
    size_t len = editor_length(ed);
    size_t count = 0;
    int in_word = 0;
    for (size_t i = 0; i < len; i++) {
        int w = is_word_char(gb_char_at(&ed->buffer, i));
        if (w && !in_word) {
            count++;
        }
        in_word = w;
    }
    return count;
}

size_t editor_utf8_seq_start(Editor *ed, size_t pos) {
    size_t len = editor_length(ed);
    if (pos >= len) {
        return pos;
    }
    size_t start = pos;
    size_t steps = 0;
    while (steps < 3 && start > 0 && (((unsigned char)gb_char_at(&ed->buffer, start)) & 0xC0) == 0x80) {
        start--;
        steps++;
    }
    return start;
}

void editor_mark_cursor_moved(Editor *ed) {
    ed->suppress_coalesce = 1;
}

static size_t advance_tab_stop(size_t col) {
    return ((col / BTN_TAB_WIDTH) + 1) * BTN_TAB_WIDTH;
}

size_t editor_tab_advance(size_t col) {
    return advance_tab_stop(col);
}

size_t editor_visual_column_in_range(Editor *ed, size_t range_start, size_t offset) {
    size_t col = 0;
    for (size_t i = range_start; i < offset; i++) {
        col = (gb_char_at(&ed->buffer, i) == '\t') ? advance_tab_stop(col) : col + 1;
    }
    return col;
}

size_t editor_offset_for_column_in_range(Editor *ed, size_t range_start, size_t range_len, size_t target_col) {
    size_t range_end = range_start + range_len;
    size_t col = 0;
    size_t i = range_start;
    while (i < range_end && col < target_col) {
        col = (gb_char_at(&ed->buffer, i) == '\t') ? advance_tab_stop(col) : col + 1;
        i++;
    }
    return i;
}

size_t editor_visual_column(Editor *ed, size_t offset) {
    size_t line_start, line_len;
    editor_line_bounds(ed, editor_offset_to_line(ed, offset), &line_start, &line_len);
    (void)line_len;
    return editor_visual_column_in_range(ed, line_start, offset);
}

size_t editor_offset_for_column(Editor *ed, size_t line_index, size_t target_col) {
    size_t line_start, line_len;
    editor_line_bounds(ed, line_index, &line_start, &line_len);
    return editor_offset_for_column_in_range(ed, line_start, line_len, target_col);
}

int editor_has_selection(Editor *ed) {
    return ed->cursor != ed->anchor;
}

size_t editor_selection_start(Editor *ed) {
    return ed->cursor < ed->anchor ? ed->cursor : ed->anchor;
}

size_t editor_selection_end(Editor *ed) {
    return ed->cursor > ed->anchor ? ed->cursor : ed->anchor;
}

/* ---- Bearbeiten ---- */

void editor_delete_selection(Editor *ed) {
    if (!editor_has_selection(ed)) {
        return;
    }
    size_t start = editor_selection_start(ed);
    size_t end = editor_selection_end(ed);
    size_t len = end - start;

    char *deleted = gb_copy_range(&ed->buffer, start, len);
    gb_delete(&ed->buffer, start, len);
    undo_push_delete_block(ed, start, deleted, len);
    free(deleted);

    ed->cursor = start;
    ed->anchor = start;
    ed->desired_col = UNSET_COL;
    ed->edit_seq++;
}

void editor_insert_text(Editor *ed, const char *text, size_t len) {
    if (len == 0) {
        return;
    }
    if (editor_has_selection(ed)) {
        editor_delete_selection(ed);
    }

    size_t pos = ed->cursor;
    gb_insert(&ed->buffer, pos, text, len);
    undo_push_insert(ed, pos, text, len);

    ed->cursor = pos + len;
    ed->anchor = ed->cursor;
    ed->desired_col = UNSET_COL;
    ed->edit_seq++;
}

void editor_delete_backward(Editor *ed) {
    if (editor_has_selection(ed)) {
        editor_delete_selection(ed);
        return;
    }
    if (ed->cursor == 0) {
        return;
    }
    size_t n = utf8_backward_len(ed, ed->cursor);
    size_t pos = ed->cursor - n;
    char *deleted = gb_copy_range(&ed->buffer, pos, n);
    gb_delete(&ed->buffer, pos, n);
    undo_push_delete(ed, pos, deleted, n, 1);
    free(deleted);

    ed->cursor = pos;
    ed->anchor = pos;
    ed->desired_col = UNSET_COL;
    ed->edit_seq++;
}

void editor_delete_forward(Editor *ed) {
    if (editor_has_selection(ed)) {
        editor_delete_selection(ed);
        return;
    }
    size_t len = editor_length(ed);
    if (ed->cursor >= len) {
        return;
    }
    size_t n = utf8_forward_len(ed, ed->cursor, len);
    char *deleted = gb_copy_range(&ed->buffer, ed->cursor, n);
    gb_delete(&ed->buffer, ed->cursor, n);
    undo_push_delete(ed, ed->cursor, deleted, n, 0);
    free(deleted);
    ed->desired_col = UNSET_COL;
    ed->edit_seq++;
}

/* ---- Bewegung & Selektion ---- */

void editor_move(Editor *ed, BtnMove move, int extend) {
    size_t len = editor_length(ed);
    size_t new_pos = ed->cursor;

    switch (move) {
        case BTN_MOVE_LEFT:
            if (!extend && editor_has_selection(ed)) {
                new_pos = editor_selection_start(ed);
            } else if (new_pos > 0) {
                new_pos--;
            }
            break;
        case BTN_MOVE_RIGHT:
            if (!extend && editor_has_selection(ed)) {
                new_pos = editor_selection_end(ed);
            } else if (new_pos < len) {
                new_pos++;
            }
            break;
        case BTN_MOVE_WORD_LEFT:
            new_pos = word_left(ed, ed->cursor);
            break;
        case BTN_MOVE_WORD_RIGHT:
            new_pos = word_right(ed, ed->cursor);
            break;
        case BTN_MOVE_DOC_START:
            new_pos = 0;
            break;
        case BTN_MOVE_DOC_END:
            new_pos = len;
            break;
    }

    ed->desired_col = UNSET_COL;
    editor_mark_cursor_moved(ed);
    ed->cursor = new_pos;
    if (!extend) {
        ed->anchor = new_pos;
    }
}

void editor_set_cursor(Editor *ed, size_t offset, int extend) {
    size_t len = editor_length(ed);
    if (offset > len) {
        offset = len;
    }
    ed->cursor = offset;
    if (!extend) {
        ed->anchor = offset;
    }
    ed->desired_col = UNSET_COL;
    editor_mark_cursor_moved(ed);
}

void editor_select_word_at(Editor *ed, size_t offset) {
    size_t len = editor_length(ed);
    if (offset > len) {
        offset = len;
    }
    if (len == 0) {
        ed->cursor = ed->anchor = 0;
        return;
    }

    size_t start = offset, end = offset;
    int at_word = 0;
    if (offset < len && is_word_char(gb_char_at(&ed->buffer, offset))) {
        at_word = 1;
    } else if (offset > 0 && is_word_char(gb_char_at(&ed->buffer, offset - 1))) {
        at_word = 1;
        start = end = offset - 1;
    }

    if (at_word) {
        while (start > 0 && is_word_char(gb_char_at(&ed->buffer, start - 1))) {
            start--;
        }
        while (end < len && is_word_char(gb_char_at(&ed->buffer, end))) {
            end++;
        }
    } else {
        while (start > 0 && !is_word_char(gb_char_at(&ed->buffer, start - 1)) &&
               gb_char_at(&ed->buffer, start - 1) != '\n') {
            start--;
        }
        while (end < len && !is_word_char(gb_char_at(&ed->buffer, end)) &&
               gb_char_at(&ed->buffer, end) != '\n') {
            end++;
        }
    }

    ed->anchor = start;
    ed->cursor = end;
    ed->desired_col = UNSET_COL;
    editor_mark_cursor_moved(ed);
}

void editor_select_line_at(Editor *ed, size_t offset) {
    size_t line = editor_offset_to_line(ed, offset);
    size_t start, len;
    editor_line_bounds(ed, line, &start, &len);
    size_t end = start + len;
    size_t total = editor_length(ed);
    if (end < total) {
        end++; /* schliesst das abschliessende '\n' mit ein */
    }
    ed->anchor = start;
    ed->cursor = end;
    ed->desired_col = UNSET_COL;
    editor_mark_cursor_moved(ed);
}

void editor_select_all(Editor *ed) {
    ed->anchor = 0;
    ed->cursor = editor_length(ed);
    ed->desired_col = UNSET_COL;
    editor_mark_cursor_moved(ed);
}

/* ---- Kopieren ---- */

char *editor_copy_all(Editor *ed, size_t *out_len) {
    size_t len = editor_length(ed);
    *out_len = len;
    return gb_copy_range(&ed->buffer, 0, len);
}

char *editor_get_selection_text(Editor *ed) {
    if (!editor_has_selection(ed)) {
        char *empty = malloc(1);
        empty[0] = '\0';
        return empty;
    }
    size_t start = editor_selection_start(ed);
    size_t end = editor_selection_end(ed);
    return gb_copy_range(&ed->buffer, start, end - start);
}

/* ---- Undo/Redo ---- */

void editor_undo(Editor *ed) {
    UndoStack *st = &ed->undo;
    if (st->pos == 0) {
        return;
    }
    st->pos--;
    UndoRecord *r = &st->records[st->pos];
    if (r->is_insert) {
        gb_delete(&ed->buffer, r->pos, r->len);
        ed->cursor = r->pos;
    } else {
        gb_insert(&ed->buffer, r->pos, r->text, r->len);
        ed->cursor = r->pos + r->len;
    }
    ed->anchor = ed->cursor;
    ed->desired_col = UNSET_COL;
    editor_mark_cursor_moved(ed);
    ed->edit_seq++;
}

void editor_redo(Editor *ed) {
    UndoStack *st = &ed->undo;
    if (st->pos == st->count) {
        return;
    }
    UndoRecord *r = &st->records[st->pos];
    if (r->is_insert) {
        gb_insert(&ed->buffer, r->pos, r->text, r->len);
        ed->cursor = r->pos + r->len;
    } else {
        gb_delete(&ed->buffer, r->pos, r->len);
        ed->cursor = r->pos;
    }
    st->pos++;
    ed->anchor = ed->cursor;
    ed->desired_col = UNSET_COL;
    editor_mark_cursor_moved(ed);
    ed->edit_seq++;
}
