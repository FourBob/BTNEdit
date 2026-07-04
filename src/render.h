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

/* Zeichnet einen Frame: Hintergrund, Selektion, Text, Cursor,
 * Zeilennummern-Gutter und die Statusleiste. scroll_line ist die
 * (0-basierte) oberste sichtbare Pufferzeile. */
void btn_render_frame(CGContextRef ctx, CGRect bounds, Editor *ed, long scroll_line);

/* Bildet einen View-Punkt (Ursprung unten links, wie bei einer
 * nicht geflippten NSView) auf einen logischen Buffer-Offset ab,
 * unter Beruecksichtigung der aktuellen Scroll-Position. */
size_t btn_hit_test(Editor *ed, CGRect bounds, double x, double y, long scroll_line);

#ifdef __cplusplus
}
#endif

#endif /* BTN_RENDER_H */
