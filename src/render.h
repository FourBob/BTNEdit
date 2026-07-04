#ifndef BTN_RENDER_H
#define BTN_RENDER_H

#include <CoreGraphics/CoreGraphics.h>
#include "editor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hoehe der Statusleiste am unteren Fensterrand (Cursor-Position, Zeilen/
 * Woerter/Zeichen, Encoding). Oeffentlich, damit main.c Klicks innerhalb
 * dieses Bereichs ignorieren kann statt sie als Text-Klick zu werten. */
#define BTN_FOOTER_HEIGHT 22.0

/* Zeichnet einen Frame: Hintergrund, Selektion, Text, Cursor,
 * Zeilennummern-Gutter und die Statusleiste. */
void btn_render_frame(CGContextRef ctx, CGRect bounds, Editor *ed);

/* Bildet einen View-Punkt (Ursprung unten links, wie bei einer
 * nicht geflippten NSView) auf einen logischen Buffer-Offset ab. */
size_t btn_hit_test(Editor *ed, CGRect bounds, double x, double y);

#ifdef __cplusplus
}
#endif

#endif /* BTN_RENDER_H */
