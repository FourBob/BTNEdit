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

/* "Klammer" umfasst hier auf Wunsch auch Anfuehrungszeichen (einfach und
 * doppelt) - fuers Auto-Vervollstaendigen/die Hervorhebung verhalten sie
 * sich fast wie eine Klammer, nur dass oeffnendes und schliessendes Zeichen
 * identisch sind (siehe is_quote_char()/find_matching_quote() unten, wo das
 * eine andere Suchstrategie als bei echten Klammern braucht). */
static int is_bracket_char(char c) {
    return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' ||
           c == '"' || c == '\'';
}

static int is_closing_bracket(char c) {
    return c == ')' || c == ']' || c == '}';
}

static int is_quote_char(char c) {
    return c == '"' || c == '\'';
}

/* Gegenstueck einer OEFFNENDEN Klammer, oder 0 wenn c keine ist (Anfuehrungs-
 * zeichen bewusst NICHT hier drin, siehe is_quote_char() - deren oeffnendes
 * und schliessendes Zeichen sind ja identisch, das braucht eigene Logik).
 * Genutzt sowohl fuers automatische Schliessen (editor_handle_bracket_key)
 * als auch fuers Loeschen eines leeren Klammerpaars auf einen Schlag
 * (editor_delete_backward). */
static char matching_close_for(char c) {
    switch (c) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        default: return 0;
    }
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

/* Anfuehrungszeichen haben kein eigenes oeffnendes/schliessendes Zeichen -
 * anders als bei echten Klammern hilft Verschachtelungstiefe hier nichts.
 * Stattdessen: erst vorwaerts nach dem naechsten gleichen Zeichen suchen
 * (offset ist dann das oeffnende), sonst rueckwaerts (offset war dann das
 * schliessende). Bricht an Zeilenumbruechen ab, da Strings ueblicherweise
 * nicht ueber mehrere Zeilen gehen - reicht fuer den ueblichen Fall, kennt
 * aber wie die Klammersuche kein Escaping (ein \" wird als eigenstaendiges
 * Anfuehrungszeichen gezaehlt). */
static int find_matching_quote(Editor *ed, size_t offset, char q, size_t *out_match) {
    size_t len = editor_length(ed);
    for (size_t i = offset + 1; i < len; i++) {
        char ch = gb_char_at(&ed->buffer, i);
        if (ch == '\n') {
            break;
        }
        if (ch == q) {
            *out_match = i;
            return 1;
        }
    }
    for (size_t i = offset; i > 0; ) {
        i--;
        char ch = gb_char_at(&ed->buffer, i);
        if (ch == '\n') {
            break;
        }
        if (ch == q) {
            *out_match = i;
            return 1;
        }
    }
    return 0;
}

/* Steht bei offset eine Klammer ODER ein Anfuehrungszeichen, wird ihr
 * Gegenstueck gesucht (fuer render.c's Hervorhebung) - bei Klammern per
 * Verschachtelungstiefen-Zaehlung derselben Klammerart, bei Anfuehrungs-
 * zeichen per find_matching_quote() (siehe dort). Reine Byte-Suche ohne
 * Kenntnis von Kommentaren/Strings - die lebt in highlight.c, nicht hier;
 * eine Klammer/ein Anfuehrungszeichen innerhalb eines String-Literals oder
 * Kommentars kann dadurch in seltenen Faellen einen inhaltlich "falschen",
 * aber stets wohldefinierten Treffer liefern. */
int editor_find_matching_bracket(Editor *ed, size_t offset, size_t *out_match) {
    size_t len = editor_length(ed);
    if (offset >= len) {
        return 0;
    }
    char c = gb_char_at(&ed->buffer, offset);
    if (is_quote_char(c)) {
        return find_matching_quote(ed, offset, c, out_match);
    }
    char open_c, close_c;
    int forward;
    if (c == '(' || c == '[' || c == '{') {
        open_c = c;
        close_c = matching_close_for(c);
        forward = 1;
    } else if (is_closing_bracket(c)) {
        close_c = c;
        open_c = (c == ')') ? '(' : (c == ']') ? '[' : '{';
        forward = 0;
    } else {
        return 0;
    }

    int depth = 0;
    if (forward) {
        for (size_t i = offset; i < len; i++) {
            char ch = gb_char_at(&ed->buffer, i);
            if (ch == open_c) {
                depth++;
            } else if (ch == close_c) {
                depth--;
                if (depth == 0) {
                    *out_match = i;
                    return 1;
                }
            }
        }
    } else {
        for (size_t i = offset; ; ) {
            char ch = gb_char_at(&ed->buffer, i);
            if (ch == close_c) {
                depth++;
            } else if (ch == open_c) {
                depth--;
                if (depth == 0) {
                    *out_match = i;
                    return 1;
                }
            }
            if (i == 0) {
                break;
            }
            i--;
        }
    }
    return 0;
}

/* Bevorzugt die Klammer direkt VOR dem Cursor, sonst die direkt DAHINTER -
 * deckt beide ueblichen Faelle ab (gerade eine schliessende Klammer
 * getippt / Cursor steht direkt vor einer oeffnenden). Fuer render.c's
 * Klammer-Hervorhebung, die nur aktiv wird, wenn der Cursor unmittelbar an
 * einer Klammer klebt. */
int editor_cursor_adjacent_bracket(Editor *ed, size_t *out_pos) {
    size_t len = editor_length(ed);
    if (ed->cursor > 0 && is_bracket_char(gb_char_at(&ed->buffer, ed->cursor - 1))) {
        *out_pos = ed->cursor - 1;
        return 1;
    }
    if (ed->cursor < len && is_bracket_char(gb_char_at(&ed->buffer, ed->cursor))) {
        *out_pos = ed->cursor;
        return 1;
    }
    return 0;
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

/* Fuegt eine Klammer ODER ein Anfuehrungszeichen ein: bei einer oeffnenden
 * Klammer wird automatisch die Gegenklammer mit eingefuegt und der Cursor
 * dazwischen platziert - oder, falls eine Selektion besteht, die Selektion
 * damit umschlossen und weiterhin selektiert (so laesst sich z.B. ein
 * bestehender Ausdruck nachtraeglich in Klammern/Anfuehrungszeichen setzen).
 * Bei einer schliessenden Klammer wird nur darueber weggerueckt (Typdurch-
 * lauf), wenn genau diese schon direkt am Cursor steht (typischerweise weil
 * sie gerade automatisch eingefuegt wurde) - sonst normal eingefuegt.
 * Anfuehrungszeichen haben kein eigenes schliessendes Zeichen (oeffnend ==
 * schliessend), brauchen den Typdurchlauf-Check deshalb VOR statt nach der
 * "oeffnend einfuegen"-Logik. Rueckgabe: 1 = behandelt (Aufrufer braucht
 * selbst kein editor_insert_text() mehr), 0 = c war keine unterstuetzte Art. */
int editor_handle_bracket_key(Editor *ed, char c) {
    if (is_quote_char(c)) {
        if (!editor_has_selection(ed) && ed->cursor < editor_length(ed) &&
            gb_char_at(&ed->buffer, ed->cursor) == c) {
            ed->cursor++;
            ed->anchor = ed->cursor;
            return 1;
        }
        if (editor_has_selection(ed)) {
            size_t start = editor_selection_start(ed);
            char *sel = editor_get_selection_text(ed);
            size_t sel_len = strlen(sel);
            editor_delete_selection(ed);
            editor_insert_text(ed, &c, 1);
            editor_insert_text(ed, sel, sel_len);
            editor_insert_text(ed, &c, 1);
            free(sel);
            ed->anchor = start + 1;
            ed->cursor = start + 1 + sel_len;
        } else {
            char pair[2] = { c, c };
            editor_insert_text(ed, pair, 2);
            ed->cursor--;
            ed->anchor = ed->cursor;
        }
        return 1;
    }

    char close_c = matching_close_for(c);
    if (close_c) {
        if (editor_has_selection(ed)) {
            size_t start = editor_selection_start(ed);
            char *sel = editor_get_selection_text(ed);
            size_t sel_len = strlen(sel);
            editor_delete_selection(ed);
            editor_insert_text(ed, &c, 1);
            editor_insert_text(ed, sel, sel_len);
            editor_insert_text(ed, &close_c, 1);
            free(sel);
            ed->anchor = start + 1;
            ed->cursor = start + 1 + sel_len;
        } else {
            char pair[2] = { c, close_c };
            editor_insert_text(ed, pair, 2);
            ed->cursor--;
            ed->anchor = ed->cursor;
        }
        return 1;
    }
    if (is_closing_bracket(c) && ed->cursor < editor_length(ed) &&
        gb_char_at(&ed->buffer, ed->cursor) == c) {
        ed->cursor++;
        ed->anchor = ed->cursor;
        return 1;
    }
    return 0;
}

void editor_delete_backward(Editor *ed) {
    if (editor_has_selection(ed)) {
        editor_delete_selection(ed);
        return;
    }
    if (ed->cursor == 0) {
        return;
    }
    /* Direkt zwischen einem gerade erst automatisch eingefuegten, noch
     * leeren Klammer- oder Anfuehrungszeichen-Paar (z.B. "()" oder ""'"'"',
     * nichts dazwischen getippt) loescht Backspace beide Zeichen auf einmal
     * - sonst bliebe ein verwaistes schliessendes Zeichen stehen, das man
     * sonst separat loeschen muesste. Bei Anfuehrungszeichen ist "erwartetes
     * schliessendes Zeichen" einfach dasselbe Zeichen (kein eigenes Gegen-
     * stueck wie bei echten Klammern). */
    char before = gb_char_at(&ed->buffer, ed->cursor - 1);
    char expected_close = is_quote_char(before) ? before : matching_close_for(before);
    if (expected_close && ed->cursor < editor_length(ed) &&
        gb_char_at(&ed->buffer, ed->cursor) == expected_close) {
        size_t pos = ed->cursor - 1;
        char *deleted = gb_copy_range(&ed->buffer, pos, 2);
        gb_delete(&ed->buffer, pos, 2);
        undo_push_delete_block(ed, pos, deleted, 2);
        free(deleted);
        ed->cursor = pos;
        ed->anchor = pos;
        ed->desired_col = UNSET_COL;
        ed->edit_seq++;
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
