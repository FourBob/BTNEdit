#ifndef BTN_RENDER_H
#define BTN_RENDER_H

#include <CoreGraphics/CoreGraphics.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zeichnet einen Frame: Hintergrund, Zeilennummern-Gutter und den Text.
 * text ist ein NUL-terminierter UTF-8-String mit '\n' als Zeilentrenner. */
void btn_render_frame(CGContextRef ctx, CGRect bounds, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* BTN_RENDER_H */
