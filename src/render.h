#ifndef BTN_RENDER_H
#define BTN_RENDER_H

#include <CoreGraphics/CoreGraphics.h>
#include "editor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Zeichnet einen Frame: Hintergrund, Selektion, Text, Cursor und
 * Zeilennummern-Gutter. */
void btn_render_frame(CGContextRef ctx, CGRect bounds, Editor *ed);

/* Bildet einen View-Punkt (Ursprung unten links, wie bei einer
 * nicht geflippten NSView) auf einen logischen Buffer-Offset ab. */
size_t btn_hit_test(Editor *ed, CGRect bounds, double x, double y);

#ifdef __cplusplus
}
#endif

#endif /* BTN_RENDER_H */
