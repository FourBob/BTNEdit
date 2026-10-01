/* Minimaler Ersatz fuer CoreGraphics, damit Tests render.h/shim.h einbinden
 * koennen: nur die Typen, keine Zeichenfunktionen (die Tests linken kein
 * CoreGraphics). Wird per -Itests/stubs nur fuer diese Tests vorangestellt. */
#ifndef BTN_TEST_CG_STUB_H
#define BTN_TEST_CG_STUB_H
#include <stddef.h>
typedef double CGFloat;
typedef struct { CGFloat x, y; } CGPoint;
typedef struct { CGFloat width, height; } CGSize;
typedef struct { CGPoint origin; CGSize size; } CGRect;
typedef struct CGContext *CGContextRef;
static inline CGRect CGRectMake(CGFloat x, CGFloat y, CGFloat w, CGFloat h) {
    CGRect r = { { x, y }, { w, h } };
    return r;
}
#endif
