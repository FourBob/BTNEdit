#ifndef BTN_EDITOR_H
#define BTN_EDITOR_H

#include <stddef.h>
#include "gapbuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Spaltenbreite eines Tabstopps. Tabs bleiben als echtes '\t'-Byte im
 * Puffer erhalten (wichtig fuers spaetere Speichern); Cursor-Mathematik,
 * Selektions-Highlight und das Zeichnen ruecken stattdessen gemeinsam auf
 * Basis dieser einen Konstante zum naechsten Vielfachen vor, damit visuelle
 * Darstellung und Cursor-Position nie auseinanderlaufen. */
#define BTN_TAB_WIDTH 4

typedef struct {
    int is_insert;    /* 1 = record represents an insertion, 0 = a deletion */
    size_t pos;
    size_t len;
    size_t capacity;
    char *text;       /* inserted text (insert record) or deleted text (delete record) */
} UndoRecord;

typedef struct {
    UndoRecord *records;
    size_t count;     /* total records currently valid */
    size_t capacity;
    size_t pos;       /* records[0..pos) are applied; pos==count means nothing to redo */
} UndoStack;

typedef struct {
    GapBuffer buffer;
    size_t cursor;
    size_t anchor;       /* selection is [min(cursor,anchor), max(cursor,anchor)) */
    size_t desired_col;  /* (size_t)-1 = unset; used for Up/Down column memory */
    UndoStack undo;
    /* Steigt bei jeder tatsaechlichen Inhaltsaenderung (auch bei Undo/Redo).
     * main.c vergleicht dies mit dem bei der letzten Sicherung/dem letzten
     * Laden gemerkten Wert, um "ungesichert" zu erkennen - bewusst NICHT
     * ueber undo.pos, weil das beim Zusammenfassen (Coalescing) aufeinander
     * folgender Tastendruecke unveraendert bleiben kann, obwohl sich der
     * Inhalt sehr wohl geaendert hat. */
    size_t edit_seq;
    /* Von jeder Cursor-Neupositionierung (Klick, Pfeiltasten, Undo/Redo,
     * Wort-/Zeilen-/Alles-Auswahl) auf 1 gesetzt und vom naechsten Insert/
     * Delete konsumiert: verhindert, dass Tippen nach einem Klick zurueck
     * an dieselbe Stelle faelschlich mit einem viel frueheren Undo-Schritt
     * zusammengefasst (coalesced) wird. */
    int suppress_coalesce;
} Editor;

/* Auf/Ab, Pos1/Ende und Cmd+Links/Rechts fehlen hier bewusst: die haengen
 * mit Wortumbruch von der Fensterbreite ab (visuelle statt logische Zeile)
 * und leben deshalb zusammen mit dem Zeilenumbruch-Layout in render.c/
 * main.c statt hier in der reinen, praesentationsunabhaengigen Logik. */
typedef enum {
    BTN_MOVE_LEFT,
    BTN_MOVE_RIGHT,
    BTN_MOVE_WORD_LEFT,
    BTN_MOVE_WORD_RIGHT,
    BTN_MOVE_DOC_START,
    BTN_MOVE_DOC_END
} BtnMove;

void editor_init(Editor *ed);
void editor_free(Editor *ed);

/* Ersetzt den gesamten Inhalt (z.B. beim Laden einer Datei), setzt Cursor
 * und Undo-Verlauf zurueck - das Laden selbst ist nicht rueckgaengig machbar. */
void editor_set_text(Editor *ed, const char *text, size_t len);

size_t editor_length(Editor *ed);
size_t editor_line_count(Editor *ed);
void editor_line_bounds(Editor *ed, size_t line_index, size_t *out_start, size_t *out_len);
size_t editor_offset_to_line(Editor *ed, size_t offset);
size_t editor_word_count(Editor *ed);

/* Tab-bewusste visuelle Spalte eines Offsets innerhalb seiner Zeile, bzw.
 * der Zeichen-Offset einer visuellen Spalte in einer gegebenen Zeile
 * (auf Zeilenende geklemmt). Reine Inhaltslogik, keine Font-Metrik. */
size_t editor_visual_column(Editor *ed, size_t offset);
size_t editor_offset_for_column(Editor *ed, size_t line_index, size_t target_col);

/* Wie oben, aber auf einen beliebigen Bereich [range_start, range_start+
 * range_len) bezogen statt auf eine logische Zeile - das ist, was render.c
 * fuer umgebrochene visuelle Zeilen (Rows) braucht, ohne dass editor.c
 * selbst etwas vom Wortumbruch wissen muss. */
size_t editor_visual_column_in_range(Editor *ed, size_t range_start, size_t offset);
size_t editor_offset_for_column_in_range(Editor *ed, size_t range_start, size_t range_len, size_t target_col);

/* Naechster Tabstopp ab der gegebenen Spalte. */
size_t editor_tab_advance(size_t col);

/* Anfang der UTF-8-Sequenz, die das Byte bei pos enthaelt (pos selbst,
 * falls es schon ein Lead-/ASCII-Byte ist). Fuer render.c's Wortumbruch,
 * damit ein erzwungener Umbruch (kein Leerzeichen gefunden) nie mitten in
 * einem mehrbytigen Zeichen landet. */
size_t editor_utf8_seq_start(Editor *ed, size_t pos);

/* Setzt suppress_coalesce - von jeder Cursor-Neupositionierung ausserhalb
 * von editor.c aufzurufen (z.B. main.c's wortumbruch-bewusste Zeilen-
 * bewegung), damit Tippen danach nicht faelschlich mit einem alten
 * Undo-Schritt zusammengefasst wird. editor.c's eigene Cursor-Funktionen
 * rufen das intern bereits selbst auf. */
void editor_mark_cursor_moved(Editor *ed);

int editor_has_selection(Editor *ed);
size_t editor_selection_start(Editor *ed);
size_t editor_selection_end(Editor *ed);

/* Klammer-Matching fuer render.c's Hervorhebung passender Klammernpaare -
 * reine Byte-Suche per Verschachtelungstiefe, ohne Kenntnis von Strings/
 * Kommentaren (das lebt in highlight.c). editor_cursor_adjacent_bracket()
 * liefert die Klammer direkt vor oder hinter dem Cursor (falls vorhanden);
 * editor_find_matching_bracket() dazu deren Gegenstueck. */
int editor_cursor_adjacent_bracket(Editor *ed, size_t *out_pos);
int editor_find_matching_bracket(Editor *ed, size_t offset, size_t *out_match);

void editor_insert_text(Editor *ed, const char *text, size_t len);
void editor_delete_backward(Editor *ed);
void editor_delete_forward(Editor *ed);
void editor_delete_selection(Editor *ed);

/* Klammer-Eingabe mit Auto-Vervollstaendigen/Typdurchlauf (main.c ruft das
 * fuer '(' ')' '[' ']' '{' '}' anstelle von editor_insert_text() auf) -
 * siehe editor.c fuer die genaue Semantik. Rueckgabe: 1 = behandelt,
 * 0 = c war keine der drei Klammerarten (Aufrufer soll normal einfuegen). */
int editor_handle_bracket_key(Editor *ed, char c);

void editor_move(Editor *ed, BtnMove move, int extend_selection);
void editor_set_cursor(Editor *ed, size_t offset, int extend_selection);
void editor_select_word_at(Editor *ed, size_t offset);
void editor_select_line_at(Editor *ed, size_t offset);
void editor_select_all(Editor *ed);

/* Beide geben neu allokierte, NUL-terminierte Strings zurueck (caller muss free() aufrufen). */
char *editor_copy_all(Editor *ed, size_t *out_len);
char *editor_get_selection_text(Editor *ed);

void editor_undo(Editor *ed);
void editor_redo(Editor *ed);

#ifdef __cplusplus
}
#endif

#endif /* BTN_EDITOR_H */
