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

int editor_is_word_char(char c) {
    return is_word_char(c);
}

/* Zaehler ueber alle Editoren, siehe edit_seq-Kommentar in editor.h. */
static size_t s_last_edit_seq = 0;

/* Nach JEDER Inhaltsaenderung an Offset pos aufzurufen: neuer, global
 * eindeutiger edit_seq, und dirty_floor merkt sich den kleinsten seit dem
 * letzten Bezugspunkt veraenderten Offset. */
static void mark_content_changed(Editor *ed, size_t pos) {
    ed->edit_seq = ++s_last_edit_seq;
    if (pos < ed->dirty_floor) {
        ed->dirty_floor = pos;
    }
}

/* Komplett neuer Inhalt (init/set_text): neuer edit_seq, der zugleich der
 * neue Bezugspunkt ist - kein Cache kann diesen Wert schon kennen. */
static void reset_content_tracking(Editor *ed) {
    ed->edit_seq = ++s_last_edit_seq;
    ed->dirty_base_seq = ed->edit_seq;
    ed->dirty_floor = (size_t)-1;
}

size_t editor_changed_from(const Editor *ed, size_t base_seq) {
    if (base_seq == ed->edit_seq) {
        return (size_t)-1;
    }
    if (base_seq == ed->dirty_base_seq) {
        return ed->dirty_floor;
    }
    return 0;
}

void editor_rebase_changes(Editor *ed) {
    ed->dirty_base_seq = ed->edit_seq;
    ed->dirty_floor = (size_t)-1;
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

/* Die EINE Regel fuer "ein Zeichen" im ganzen Programm (siehe editor.h):
 * eine gueltige UTF-8-Sequenz nach RFC 3629 - keine Ueberlaengen, keine
 * Surrogate, nichts ueber U+10FFFF - ist ein Zeichen; jedes andere Byte ist
 * fuer sich ein Zeichen und wird von render.c als ISO-8859-1 gezeichnet.
 * Vorher prueften Cursorbewegung, Loeschen, Spaltenrechnung und Anzeige das
 * jeweils anders: Entf auf einem Latin-1-"ae" (0xE4 = "3-Byte-Lead") loeschte
 * die folgenden zwei Bytes mit, auch einen Zeilenumbruch. */
size_t btn_utf8_char_len(const unsigned char *s, size_t avail) {
    unsigned char b0 = s[0];
    if (b0 < 0x80) {
        return 1;
    }
    size_t n;
    unsigned char lo = 0x80, hi = 0xBF;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        n = 2;
    } else if (b0 == 0xE0) {
        n = 3;
        lo = 0xA0;
    } else if ((b0 >= 0xE1 && b0 <= 0xEC) || b0 == 0xEE || b0 == 0xEF) {
        n = 3;
    } else if (b0 == 0xED) {
        n = 3;
        hi = 0x9F;
    } else if (b0 == 0xF0) {
        n = 4;
        lo = 0x90;
    } else if (b0 >= 0xF1 && b0 <= 0xF3) {
        n = 4;
    } else if (b0 == 0xF4) {
        n = 4;
        hi = 0x8F;
    } else {
        return 1;
    }
    if (avail < n || s[1] < lo || s[1] > hi) {
        return 1;
    }
    for (size_t k = 2; k < n; k++) {
        if ((s[k] & 0xC0) != 0x80) {
            return 1;
        }
    }
    return n;
}

size_t btn_utf8_seq_start(const unsigned char *s, size_t len, size_t pos) {
    if (pos >= len) {
        return pos;
    }
    size_t p = pos;
    size_t steps = 0;
    while (steps < 3 && p > 0 && (s[p] & 0xC0) == 0x80) {
        p--;
        steps++;
    }
    if (p < pos && btn_utf8_char_len(s + p, len - p) > pos - p) {
        return p;
    }
    return pos;
}

size_t editor_char_len(Editor *ed, size_t pos, size_t limit) {
    unsigned char b[4];
    b[0] = (unsigned char)gb_char_at(&ed->buffer, pos);
    if (b[0] < 0x80) {
        return 1;
    }
    size_t avail = limit - pos;
    if (avail > 4) {
        avail = 4;
    }
    for (size_t k = 1; k < avail; k++) {
        b[k] = (unsigned char)gb_char_at(&ed->buffer, pos + k);
    }
    return btn_utf8_char_len(b, avail);
}

static int is_continuation_byte(Editor *ed, size_t pos) {
    return (((unsigned char)gb_char_at(&ed->buffer, pos)) & 0xC0) == 0x80;
}

/* Byte-Laenge des Zeichens, das VOR pos endet (Backspace, Pfeil links).
 * Kandidat ist das naechste Nicht-Fortsetzungsbyte hoechstens 3 Bytes davor;
 * nur wenn von dort genau ein Zeichen bis pos reicht, ist es dieses Zeichen,
 * sonst steht das Byte vor pos fuer sich (verirrtes Fortsetzungsbyte,
 * abgeschnittene Sequenz). Jedes Nicht-Fortsetzungsbyte beginnt ein Zeichen,
 * daher stimmt das exakt mit der Vorwaerts-Zerlegung ueberein. */
static size_t utf8_backward_len(Editor *ed, size_t pos) {
    size_t p = pos - 1;
    size_t steps = 0;
    while (steps < 3 && p > 0 && is_continuation_byte(ed, p)) {
        p--;
        steps++;
    }
    if (editor_char_len(ed, p, pos) == pos - p) {
        return pos - p;
    }
    return 1;
}

/* Byte-Laenge des Zeichens, das BEI pos beginnt (Entf, Pfeil rechts). */
static size_t utf8_forward_len(Editor *ed, size_t pos, size_t limit) {
    return editor_char_len(ed, pos, limit);
}

/* ---- Undo-Stack ---- */

static void undo_stack_init(UndoStack *st) {
    st->records = NULL;
    st->count = 0;
    st->capacity = 0;
    st->pos = 0;
    st->open_group = 0;
    st->last_group = 0;
    st->group_depth = 0;
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

/* Antwort auf eine fehlgeschlagene Allokation im Undo-Stack: den ganzen
 * Verlauf verwerfen. Die Bearbeitung selbst bleibt gueltig, sie ist nur
 * nicht mehr rueckgaengig machbar. Ein fehlender oder halb geschriebener
 * Record wuerde dagegen spaetere Undos an falscher Stelle anwenden - und
 * realloc() direkt auf den Besitzer-Zeiger verlor vorher den alten Block
 * und schrieb dann ueber NULL. Eine offene Gruppe bleibt offen. */
static void undo_drop_history(UndoStack *st) {
    unsigned long open_group = st->open_group, last_group = st->last_group;
    int depth = st->group_depth;
    undo_stack_free(st);
    undo_stack_init(st);
    st->open_group = open_group;
    st->last_group = last_group;
    st->group_depth = depth;
}

static UndoRecord *undo_stack_push_new(UndoStack *st) {
    undo_stack_truncate_redo(st);
    if (st->count == st->capacity) {
        size_t new_cap = st->capacity ? st->capacity * 2 : 64;
        UndoRecord *grown = NULL;
        if (new_cap <= (size_t)-1 / sizeof(UndoRecord)) {
            grown = realloc(st->records, new_cap * sizeof(UndoRecord));
        }
        if (!grown) {
            undo_drop_history(st);
            return NULL;
        }
        st->records = grown;
        st->capacity = new_cap;
    }
    UndoRecord *r = &st->records[st->count++];
    st->pos = st->count;
    return r;
}

/* 1 = Platz fuer extra weitere Bytes, 0 = Allokation fehlgeschlagen (r
 * unveraendert). */
static int record_grow(UndoRecord *r, size_t extra) {
    if (r->len + extra <= r->capacity) {
        return 1;
    }
    size_t new_cap = r->capacity ? r->capacity * 2 : 16;
    if (new_cap < r->len + extra) {
        new_cap = r->len + extra;
    }
    char *grown = realloc(r->text, new_cap);
    if (!grown) {
        return 0;
    }
    r->text = grown;
    r->capacity = new_cap;
    return 1;
}

/* Legt einen frischen UndoRecord an und fuellt ihn - der gemeinsame Kern
 * fuer alle drei Stellen, die einen NICHT zusammengefassten Record
 * brauchen (neuer Insert, neuer Delete, Block-Delete einer Selektion). */
static void undo_record_fill(UndoStack *st, int is_insert, size_t pos, const char *src, size_t len) {
    UndoRecord *r = undo_stack_push_new(st);
    if (!r) {
        return;
    }
    char *text = malloc(len ? len : 1);
    if (!text) {
        st->count--;
        st->pos = st->count;
        undo_drop_history(st);
        return;
    }
    memcpy(text, src, len);
    r->is_insert = is_insert;
    r->pos = pos;
    r->len = len;
    r->capacity = len;
    r->text = text;
    r->group = st->open_group;
}

static void undo_push_insert(Editor *ed, size_t pos, const char *text, size_t len) {
    UndoStack *st = &ed->undo;
    int blocked = ed->suppress_coalesce;
    ed->suppress_coalesce = 0;
    if (!blocked && st->pos > 0 && st->pos == st->count) {
        UndoRecord *last = &st->records[st->pos - 1];
        /* Genau EIN Zeichen (auch mehrbytig - "ae" sind 2 Bytes), nicht nur
         * len == 1: sonst begann jeder Umlaut einen neuen Undo-Schritt, und
         * Cmd+Z nahm deutsche Saetze in Bruchstuecken zurueck. */
        if (last->is_insert && last->group == st->open_group && last->pos + last->len == pos &&
            btn_utf8_char_len((const unsigned char *)text, len) == len &&
            text[0] != '\n' && (last->len == 0 || last->text[last->len - 1] != '\n')) {
            if (!record_grow(last, len)) {
                undo_drop_history(st);
                return;
            }
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
    if (!blocked && st->pos > 0 && st->pos == st->count &&
        btn_utf8_char_len((const unsigned char *)deleted, len) == len && deleted[0] != '\n') {
        UndoRecord *last = &st->records[st->pos - 1];
        if (!last->is_insert && last->group == st->open_group) {
            if (backward && pos + len == last->pos) {
                if (!record_grow(last, len)) {
                    undo_drop_history(st);
                    return;
                }
                memmove(last->text + len, last->text, last->len);
                memcpy(last->text, deleted, len);
                last->len += len;
                last->pos = pos;
                return;
            }
            if (!backward && pos == last->pos) {
                if (!record_grow(last, len)) {
                    undo_drop_history(st);
                    return;
                }
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
    reset_content_tracking(ed);
    ed->suppress_coalesce = 0;
    ed->single_line = 0;
    undo_stack_init(&ed->undo);
}

void editor_free(Editor *ed) {
    gb_free(&ed->buffer);
    undo_stack_free(&ed->undo);
}

void editor_set_single_line(Editor *ed, int single_line) {
    ed->single_line = single_line;
}

/* Ersetzt '\n'/'\r' durch ' ' in einer Kopie von text, falls ed einzeilig
 * ist - genutzt von editor_insert_text()/editor_set_text(), damit KEIN
 * Einfuegeweg (Tippen, Einfuegen aus der Zwischenablage, künftige Wege wie
 * Drag&Drop/IME) das einzeilige Feld je mit einem echten Zeilenumbruch
 * durcheinanderbringen kann. Tabs bleiben: das Feld zeichnet und rechnet
 * Spalten mit denselben Tabstopps (decode_row_for_display()/
 * editor_visual_column_in_range() ab Spalte 0), und nur so findet die
 * Suche nach einer vorbefuellten Selektion wie "a<Tab>b" den Tab im
 * Dokument (vorher wurde daraus "a b" - "Nicht gefunden"). Gibt NULL zurueck, wenn keine Ersetzung noetig war
 * (Aufrufer nutzt dann weiter das Original); sonst einen neu allokierten,
 * gleich langen Puffer (Ersetzung ist immer 1:1, keine Laengenaenderung).
 */
static char *sanitize_single_line(const char *text, size_t len) {
    int needs_sanitizing = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n' || text[i] == '\r') {
            needs_sanitizing = 1;
            break;
        }
    }
    if (!needs_sanitizing) {
        return NULL;
    }
    char *out = btn_xmalloc(len);
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        out[i] = (c == '\n' || c == '\r') ? ' ' : c;
    }
    return out;
}

void editor_set_text(Editor *ed, const char *text, size_t len) {
    char *sanitized = ed->single_line ? sanitize_single_line(text, len) : NULL;
    if (sanitized) {
        text = sanitized;
    }

    gb_free(&ed->buffer);
    gb_init(&ed->buffer, len + 64);
    gb_insert(&ed->buffer, 0, text, len);

    ed->cursor = 0;
    ed->anchor = 0;
    ed->desired_col = UNSET_COL;
    reset_content_tracking(ed);
    ed->suppress_coalesce = 0;

    undo_stack_free(&ed->undo);
    undo_stack_init(&ed->undo);
    free(sanitized);
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
    size_t p = pos;
    size_t steps = 0;
    while (steps < 3 && p > 0 && is_continuation_byte(ed, p)) {
        p--;
        steps++;
    }
    if (p < pos && editor_char_len(ed, p, len) > pos - p) {
        return p;
    }
    return pos;
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

/* Zaehlt Zeichen nach btn_utf8_char_len(), nicht Bytes - jedes Zeichen ist
 * genau EINE visuelle Spalte (Tab: bis zum naechsten Tabstopp), genau so,
 * wie render.c's draw_row_line() es zeichnet. Sonst wuerde z.B. "ä"
 * (2 Bytes) als 2 Spalten zaehlen und der Cursor bei jedem mehrbytigen
 * Zeichen in derselben Zeile weiter vom gezeichneten Text weglaufen. */
size_t editor_visual_column_in_range(Editor *ed, size_t range_start, size_t offset) {
    size_t col = 0;
    size_t i = range_start;
    while (i < offset) {
        unsigned char c = (unsigned char)gb_char_at(&ed->buffer, i);
        col = (c == '\t') ? advance_tab_stop(col) : col + 1;
        i += (c < 0x80) ? 1 : editor_char_len(ed, i, offset);
    }
    return col;
}

size_t editor_offset_for_column_in_range(Editor *ed, size_t range_start, size_t range_len, size_t target_col) {
    size_t range_end = range_start + range_len;
    size_t col = 0;
    size_t i = range_start;
    while (i < range_end && col < target_col) {
        char c = gb_char_at(&ed->buffer, i);
        col = (c == '\t') ? advance_tab_stop(col) : col + 1;
        /* Ganze Byte-Laenge des Zeichens ueberspringen (nicht nur 1 Byte),
         * sonst wuerde i bei einem mehrbytigen Zeichen mitten in dessen
         * Fortsetzungsbytes stehen bleiben, statt am Anfang des naechsten
         * echten Zeichens - siehe editor_visual_column_in_range() oben fuer
         * dasselbe Grundproblem in der jeweils anderen Richtung. */
        i += utf8_forward_len(ed, i, range_end);
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
/* Obergrenze fuer die Verschachtelungstiefen-Suche unten - ohne die wuerde
 * eine unpaarige/sehr weit entfernte Klammer bei JEDEM Redraw (die
 * Hervorhebung in render.c laeuft bei jedem Tastendruck) einen Scan bis
 * zum Puffer-Anfang/-Ende ausloesen. Anfuehrungszeichen brauchen das nicht
 * (find_matching_quote() bricht ohnehin an Zeilenumbruechen ab). Grosszuegig
 * genug, um in praktisch jeder echten Datei das tatsaechliche Gegenstueck
 * noch zu finden, aber klein genug, um den Redraw-Pfad nie spuerbar zu
 * verlangsamen. */
#define BTN_BRACKET_MATCH_SCAN_LIMIT 20000

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
    size_t scanned = 0;
    if (forward) {
        for (size_t i = offset; i < len && scanned < BTN_BRACKET_MATCH_SCAN_LIMIT; i++, scanned++) {
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
        for (size_t i = offset; scanned < BTN_BRACKET_MATCH_SCAN_LIMIT; scanned++) {
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
    mark_content_changed(ed, start);
}

void editor_insert_text(Editor *ed, const char *text, size_t len) {
    if (len == 0) {
        return;
    }
    if (editor_has_selection(ed)) {
        editor_delete_selection(ed);
    }

    char *sanitized = ed->single_line ? sanitize_single_line(text, len) : NULL;
    if (sanitized) {
        text = sanitized;
    }

    size_t pos = ed->cursor;
    gb_insert(&ed->buffer, pos, text, len);
    undo_push_insert(ed, pos, text, len);

    ed->cursor = pos + len;
    ed->anchor = ed->cursor;
    ed->desired_col = UNSET_COL;
    mark_content_changed(ed, pos);
    free(sanitized);
}

/* Ersetzt die aktuelle Selektion durch open_c...close_c mit dem bisherigen
 * Selektionsinhalt dazwischen (z.B. Selektion "abc" + Klammer "(" wird zu
 * "(abc)") und laesst den neuen Inneninhalt weiterhin selektiert. Gemeinsame
 * Logik fuer Anfuehrungszeichen (open_c == close_c) und echte Klammern
 * (open_c != close_c) - beide Faelle unterschieden sich vorher nur in dieser
 * einen Konstante, waren aber als zwei fast identische ~14-Zeilen-Kopien
 * ausgeschrieben. */
static void wrap_selection_with(Editor *ed, char open_c, char close_c) {
    size_t start = editor_selection_start(ed);
    /* Laenge aus den Selektionsgrenzen, nicht strlen(): eine Selektion in
     * einer per "Trotzdem oeffnen" geladenen Binaerdatei kann NUL-Bytes
     * enthalten - strlen() haette alles dahinter beim Wiedereinfuegen
     * verschluckt. */
    size_t sel_len = editor_selection_end(ed) - start;
    char *sel = editor_get_selection_text(ed);
    /* Ein Undo-Schritt statt drei (Loeschen, Klammer, Text, Klammer). */
    editor_begin_undo_group(ed);
    editor_delete_selection(ed);
    editor_insert_text(ed, &open_c, 1);
    editor_insert_text(ed, sel, sel_len);
    editor_insert_text(ed, &close_c, 1);
    editor_end_undo_group(ed);
    free(sel);
    ed->anchor = start + 1;
    ed->cursor = start + 1 + sel_len;
}

/* Fuegt das leere Paar open_c/close_c ein (keine Selektion) und platziert
 * den Cursor dazwischen. */
static void insert_empty_pair(Editor *ed, char open_c, char close_c) {
    char pair[2] = { open_c, close_c };
    editor_insert_text(ed, pair, 2);
    ed->cursor--;
    ed->anchor = ed->cursor;
}

/* Fuegt eine Klammer ODER ein Anfuehrungszeichen ein: bei einer oeffnenden
 * Klammer wird automatisch die Gegenklammer mit eingefuegt und der Cursor
 * dazwischen platziert - oder, falls eine Selektion besteht, die Selektion
 * damit umschlossen und weiterhin selektiert (so laesst sich z.B. ein
 * bestehender Ausdruck nachtraeglich in Klammern/Anfuehrungszeichen setzen).
 * Bei einer schliessenden Klammer wird nur darueber weggerueckt (Typdurch-
 * lauf), wenn genau diese schon direkt am Cursor steht (typischerweise weil
 * sie gerade automatisch eingefuegt wurde) UND keine Selektion besteht -
 * mit aktiver Selektion soll ein getipptes schliessendes Zeichen sie ganz
 * normal ersetzen (der Aufrufer faellt dafuer auf editor_insert_text()
 * zurueck, siehe Rueckgabewert 0). Anfuehrungszeichen haben kein eigenes
 * schliessendes Zeichen (oeffnend == schliessend), brauchen den
 * Typdurchlauf-Check deshalb VOR statt nach der "oeffnend einfuegen"-Logik.
 * Rueckgabe: 1 = behandelt (Aufrufer braucht selbst kein editor_insert_text()
 * mehr), 0 = c war keine unterstuetzte Art (oder eine Selektion stand einem
 * Typdurchlauf im Weg - Aufrufer soll normal einfuegen). */
int editor_handle_bracket_key(Editor *ed, char c) {
    if (is_quote_char(c)) {
        if (!editor_has_selection(ed) && ed->cursor < editor_length(ed) &&
            gb_char_at(&ed->buffer, ed->cursor) == c) {
            ed->cursor++;
            ed->anchor = ed->cursor;
            return 1;
        }
        if (editor_has_selection(ed)) {
            wrap_selection_with(ed, c, c);
        } else {
            insert_empty_pair(ed, c, c);
        }
        return 1;
    }

    char close_c = matching_close_for(c);
    if (close_c) {
        if (editor_has_selection(ed)) {
            wrap_selection_with(ed, c, close_c);
        } else {
            insert_empty_pair(ed, c, close_c);
        }
        return 1;
    }
    if (!editor_has_selection(ed) && is_closing_bracket(c) && ed->cursor < editor_length(ed) &&
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
     * stueck wie bei echten Klammern). Nur fuers Dokument (!single_line):
     * ein einzeiliges Feld (Suchen/Ersetzen) bekommt automatisch geschlossene
     * Paare nie ueber editor_handle_bracket_key() (das ruft nur main.c's
     * Dokument-Tastatur-Pfad auf), also ist dort JEDES benachbarte Paar von
     * Hand Zeichen-fuer-Zeichen getippt worden - die Heuristik kann das
     * strukturell nicht von einem echten Auto-Paar unterscheiden und wuerde
     * sonst faelschlich beide Zeichen auf einmal loeschen. */
    if (!ed->single_line) {
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
            mark_content_changed(ed, pos);
            return;
        }
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
    mark_content_changed(ed, pos);
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
    mark_content_changed(ed, ed->cursor);
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
                /* Ein ZEICHEN, nicht ein Byte - sonst landet der Cursor
                 * mitten in einer UTF-8-Sequenz (bei "ä" zwischen C3 und
                 * A4, gezeichnet an derselben Spalte, also unsichtbar), und
                 * das naechste Tippen/Backspace zerreisst die Sequenz zu
                 * ungueltigem UTF-8 in der Datei. Dieselben Helfer wie
                 * Backspace/Entf. */
                new_pos -= utf8_backward_len(ed, new_pos);
            }
            break;
        case BTN_MOVE_RIGHT:
            if (!extend && editor_has_selection(ed)) {
                new_pos = editor_selection_end(ed);
            } else if (new_pos < len) {
                new_pos += utf8_forward_len(ed, new_pos, len);
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
        char *empty = btn_xmalloc(1);
        empty[0] = '\0';
        return empty;
    }
    size_t start = editor_selection_start(ed);
    size_t end = editor_selection_end(ed);
    return gb_copy_range(&ed->buffer, start, end - start);
}

/* ---- Undo/Redo ---- */

/* Wendet records[pos-1] rueckwaerts an (Undo) bzw. records[pos] vorwaerts
 * (Redo) und setzt den Cursor ans Ende der wiederhergestellten Stelle. */
static void undo_apply_one(Editor *ed, int undo) {
    UndoStack *st = &ed->undo;
    UndoRecord *r = undo ? &st->records[--st->pos] : &st->records[st->pos++];
    if (r->is_insert == !undo) {
        gb_insert(&ed->buffer, r->pos, r->text, r->len);
        ed->cursor = r->pos + r->len;
    } else {
        gb_delete(&ed->buffer, r->pos, r->len);
        ed->cursor = r->pos;
    }
    mark_content_changed(ed, r->pos);
}

void editor_undo(Editor *ed) {
    UndoStack *st = &ed->undo;
    if (st->pos == 0) {
        return;
    }
    /* Eine ganze Gruppe (siehe editor_begin_undo_group()) auf einmal. */
    unsigned long group = st->records[st->pos - 1].group;
    do {
        undo_apply_one(ed, 1);
    } while (group != 0 && st->pos > 0 && st->records[st->pos - 1].group == group);
    ed->anchor = ed->cursor;
    ed->desired_col = UNSET_COL;
    editor_mark_cursor_moved(ed);
}

void editor_redo(Editor *ed) {
    UndoStack *st = &ed->undo;
    if (st->pos == st->count) {
        return;
    }
    unsigned long group = st->records[st->pos].group;
    do {
        undo_apply_one(ed, 0);
    } while (group != 0 && st->pos < st->count && st->records[st->pos].group == group);
    ed->anchor = ed->cursor;
    ed->desired_col = UNSET_COL;
    editor_mark_cursor_moved(ed);
}

void editor_begin_undo_group(Editor *ed) {
    UndoStack *st = &ed->undo;
    if (st->group_depth++ == 0) {
        st->open_group = ++st->last_group;
    }
}

void editor_end_undo_group(Editor *ed) {
    UndoStack *st = &ed->undo;
    if (st->group_depth > 0 && --st->group_depth == 0) {
        st->open_group = 0;
        /* Naechster Tastendruck nicht in den letzten Record der Gruppe
         * hineinfassen. */
        ed->suppress_coalesce = 1;
    }
}

/* ---- Einruecken ---- */

/* Wie viel vom Dokumentanfang fuer die Stil-Erkennung gelesen wird - Tab
 * soll auch bei einer 1-GB-Datei sofort reagieren. */
#define BTN_INDENT_SAMPLE_LEN (1024 * 1024)

static size_t line_start_at(Editor *ed, size_t pos) {
    while (pos > 0 && gb_char_at(&ed->buffer, pos - 1) != '\n') {
        pos--;
    }
    return pos;
}

int editor_indent_uses_spaces(Editor *ed) {
    size_t len = editor_length(ed);
    size_t limit = len < BTN_INDENT_SAMPLE_LEN ? len : BTN_INDENT_SAMPLE_LEN;
    size_t tab_lines = 0, space_lines = 0;
    int at_line_start = 1;
    for (size_t i = 0; i < limit; i++) {
        char c = gb_char_at(&ed->buffer, i);
        if (at_line_start) {
            if (c == '\t') {
                tab_lines++;
            } else if (c == ' ' && i + 1 < len && gb_char_at(&ed->buffer, i + 1) == ' ') {
                /* mindestens zwei: " * " in Blockkommentaren ist keine Einrueckung */
                space_lines++;
            }
        }
        at_line_start = (c == '\n');
    }
    return space_lines > tab_lines;
}

/* Eine Einrueckstufe: '\t' oder BTN_TAB_WIDTH Leerzeichen. */
static size_t indent_unit(Editor *ed, char *buf) {
    if (editor_indent_uses_spaces(ed)) {
        memset(buf, ' ', BTN_TAB_WIDTH);
        return BTN_TAB_WIDTH;
    }
    buf[0] = '\t';
    return 1;
}

void editor_insert_newline(Editor *ed) {
    size_t start = editor_selection_start(ed);
    size_t line = line_start_at(ed, start);
    size_t n = 0;
    while (line + n < start) {
        char c = gb_char_at(&ed->buffer, line + n);
        if (c != ' ' && c != '\t') {
            break;
        }
        n++;
    }
    char *text = btn_xmalloc(n + 1);
    text[0] = '\n';
    for (size_t i = 0; i < n; i++) {
        text[1 + i] = gb_char_at(&ed->buffer, line + i);
    }
    /* Selektion ersetzen + Umbruch + Einrueckung = ein Undo-Schritt */
    editor_begin_undo_group(ed);
    editor_insert_text(ed, text, ed->single_line ? 1 : n + 1);
    editor_end_undo_group(ed);
    free(text);
}

/* Rueckt alle Zeilen ein bzw. aus, die die Selektion beruehrt (ohne
 * Selektion: die Zeile des Cursors). Eine Zeile, auf deren Spalte 0 die
 * Selektion nur endet, zaehlt nicht mit; leere Zeilen werden nicht
 * eingerueckt. Ein Undo-Schritt, Anker und Cursor bleiben auf ihrem Text. */
static void indent_lines(Editor *ed, int outdent) {
    size_t s = editor_selection_start(ed), e = editor_selection_end(ed);
    size_t last = e;
    if (e > s && gb_char_at(&ed->buffer, e - 1) == '\n') {
        last = e - 1;
    }
    /* Zeilenanfaenge sammeln (aufsteigend) */
    size_t cap = 16, count = 0;
    size_t *starts = btn_xmalloc(cap * sizeof(size_t));
    starts[count++] = line_start_at(ed, s);
    for (size_t i = starts[0]; i < last; i++) {
        if (gb_char_at(&ed->buffer, i) == '\n') {
            if (count == cap) {
                cap *= 2;
                starts = btn_xrealloc(starts, btn_xmul(cap, sizeof(size_t)));
            }
            starts[count++] = i + 1;
        }
    }

    char unit[BTN_TAB_WIDTH];
    size_t unit_len = outdent ? 0 : indent_unit(ed, unit);
    size_t *removed = btn_xmalloc(count * sizeof(size_t)); /* je Zeile: entfernte Bytes */
    size_t anchor = ed->anchor, cursor = ed->cursor;
    long anchor_shift = 0, cursor_shift = 0;

    editor_begin_undo_group(ed);
    /* von hinten nach vorn - fruehere Offsets bleiben so gueltig */
    for (size_t k = count; k-- > 0;) {
        size_t line = starts[k];
        size_t len = editor_length(ed);
        removed[k] = 0;
        if (!outdent) {
            if (line >= len || gb_char_at(&ed->buffer, line) == '\n') {
                continue; /* leere Zeile */
            }
            editor_set_cursor(ed, line, 0);
            editor_insert_text(ed, unit, unit_len);
        } else {
            size_t n = 0;
            if (line < len && gb_char_at(&ed->buffer, line) == '\t') {
                n = 1;
            } else {
                while (n < BTN_TAB_WIDTH && line + n < len && gb_char_at(&ed->buffer, line + n) == ' ') {
                    n++;
                }
            }
            if (n == 0) {
                continue;
            }
            editor_set_cursor(ed, line, 0);
            editor_set_cursor(ed, line + n, 1);
            editor_delete_selection(ed);
            removed[k] = n;
        }
        /* Verschiebung fuer Anker/Cursor in Originalkoordinaten */
        size_t pos[2] = { anchor, cursor };
        long *shift[2] = { &anchor_shift, &cursor_shift };
        for (int j = 0; j < 2; j++) {
            if (!outdent) {
                if (pos[j] > line) {
                    *shift[j] += (long)unit_len;
                }
            } else if (pos[j] > line) {
                size_t end = line + removed[k];
                *shift[j] -= (long)((pos[j] < end ? pos[j] : end) - line);
            }
        }
    }
    editor_end_undo_group(ed);

    editor_set_cursor(ed, (size_t)((long)anchor + anchor_shift), 0);
    editor_set_cursor(ed, (size_t)((long)cursor + cursor_shift), 1);
    free(removed);
    free(starts);
}

void editor_tab_key(Editor *ed, int outdent) {
    if (outdent) {
        indent_lines(ed, 1);
        return;
    }
    size_t s = editor_selection_start(ed), e = editor_selection_end(ed);
    for (size_t i = s; i < e; i++) {
        if (gb_char_at(&ed->buffer, i) == '\n') {
            indent_lines(ed, 0); /* Selektion ueber mehrere Zeilen */
            return;
        }
    }
    /* Selektion ersetzen + einfuegen = ein Undo-Schritt */
    editor_begin_undo_group(ed);
    if (editor_indent_uses_spaces(ed)) {
        /* bis zum naechsten Tabstopp auffuellen, wie ein Tab aussaehe */
        size_t col = editor_visual_column_in_range(ed, line_start_at(ed, s), s);
        size_t n = editor_tab_advance(col) - col;
        char spaces[BTN_TAB_WIDTH];
        memset(spaces, ' ', sizeof spaces);
        editor_insert_text(ed, spaces, n);
    } else {
        editor_insert_text(ed, "\t", 1);
    }
    editor_end_undo_group(ed);
}
