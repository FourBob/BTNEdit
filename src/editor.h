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
} Editor;

typedef enum {
    BTN_MOVE_LEFT,
    BTN_MOVE_RIGHT,
    BTN_MOVE_UP,
    BTN_MOVE_DOWN,
    BTN_MOVE_LINE_START,
    BTN_MOVE_LINE_END,
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

/* Tab-bewusste visuelle Spalte eines Offsets innerhalb seiner Zeile, bzw.
 * der Zeichen-Offset einer visuellen Spalte in einer gegebenen Zeile
 * (auf Zeilenende geklemmt). Reine Inhaltslogik, keine Font-Metrik. */
size_t editor_visual_column(Editor *ed, size_t offset);
size_t editor_offset_for_column(Editor *ed, size_t line_index, size_t target_col);

int editor_has_selection(Editor *ed);
size_t editor_selection_start(Editor *ed);
size_t editor_selection_end(Editor *ed);

void editor_insert_text(Editor *ed, const char *text, size_t len);
void editor_delete_backward(Editor *ed);
void editor_delete_forward(Editor *ed);
void editor_delete_selection(Editor *ed);

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
