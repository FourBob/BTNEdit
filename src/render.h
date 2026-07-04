#ifndef BTN_RENDER_H
#define BTN_RENDER_H

#include <CoreGraphics/CoreGraphics.h>
#include "editor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Layout-Konstanten - oeffentlich, weil main.c sie fuers Scroll-Mass
 * (wie viele Zeilen passen ins Fenster) und fuer Klick-Ausschluss braucht. */
#define BTN_LINE_HEIGHT 18.0
#define BTN_FOOTER_HEIGHT 22.0

/* Eine visuelle Zeile (Row) nach Wortumbruch: [start, start+len) im
 * Puffer. logical_line ist die zugehoerige "echte" Zeile (fuer die
 * Gutter-Nummerierung); is_continuation markiert Folge-Rows einer per
 * Wortumbruch aufgeteilten logischen Zeile (die keine eigene Nummer
 * bekommen). */
typedef struct {
    size_t start;
    size_t len;
    size_t logical_line;
    int is_continuation;
} BtnRow;

/* Verfuegbare Breite fuer Text (Fensterbreite minus Gutter/Polsterung). */
double btn_layout_text_width(CGRect bounds);

/* Berechnet das komplette Zeilenumbruch-Layout fuer den aktuellen
 * Pufferinhalt bei gegebener Textbreite. Caller muss btn_layout_free()
 * aufrufen. Wird sowohl vom Zeichnen als auch von main.c fuer
 * wortumbruch-bewusste Cursor-Bewegung/Scrolling/Hit-Testing genutzt. */
BtnRow *btn_layout_build(Editor *ed, double text_width, size_t *out_row_count);
void btn_layout_free(BtnRow *rows);

/* Row-Index, in dem der gegebene Puffer-Offset liegt (Grenzfaelle an
 * einem Zeilenumbruch werden dem Zeilenanfang der naechsten Row
 * zugeschlagen, nicht dem Ende der vorherigen). */
size_t btn_layout_row_for_offset(const BtnRow *rows, size_t row_count, size_t offset);

/* Zeichnet einen Frame: Hintergrund, Selektion, Text, Cursor,
 * Zeilennummern-Gutter und die Statusleiste. scroll_row ist der
 * (0-basierte) oberste sichtbare Row-Index. */
void btn_render_frame(CGContextRef ctx, CGRect bounds, Editor *ed, long scroll_row);

/* Bildet einen View-Punkt (Ursprung unten links, wie bei einer
 * nicht geflippten NSView) auf einen logischen Buffer-Offset ab,
 * unter Beruecksichtigung der aktuellen Scroll-Position. */
size_t btn_hit_test(Editor *ed, CGRect bounds, double x, double y, long scroll_row);

#ifdef __cplusplus
}
#endif

#endif /* BTN_RENDER_H */
