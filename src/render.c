/*
 * Reines Rendering ueber Core Graphics/Core Text - kein AppKit-Widget
 * beteiligt. Fester Zeilenabstand, Monospace-Schrift (Menlo); kein
 * Word-Wrap/Scrolling (folgt in einem spaeteren Meilenstein).
 *
 * Da die Schrift monospaced ist, kann "Spalte" ueberall als Zeichenanzahl
 * statt als Pixelposition behandelt werden - das vereinfacht sowohl das
 * Zeichnen des Cursors/der Selektion als auch das Hit-Testing bei
 * Mausklicks erheblich (siehe btn_hit_test).
 */
#include "render.h"
#include <CoreText/CoreText.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define GUTTER_WIDTH 44.0
#define LINE_HEIGHT 18.0
#define FONT_SIZE 13.0
#define LEFT_PADDING 8.0
#define TOP_PADDING 8.0

static CTFontRef g_font = NULL;
static double g_char_width = 0.0;

static CTFontRef get_font(void) {
    if (!g_font) {
        g_font = CTFontCreateWithName(CFSTR("Menlo"), FONT_SIZE, NULL);
    }
    return g_font;
}

static CFDictionaryRef make_attrs(CTFontRef font, CGColorRef color) {
    CFStringRef keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    CFTypeRef values[] = { font, color };
    return CFDictionaryCreate(NULL, (const void **)keys, (const void **)values, 2,
                               &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

static double get_char_width(void) {
    if (g_char_width <= 0.0) {
        CGColorRef black = CGColorCreateGenericRGB(0.0, 0.0, 0.0, 1.0);
        CFDictionaryRef attrs = make_attrs(get_font(), black);
        CFAttributedStringRef attrStr = CFAttributedStringCreate(NULL, CFSTR("M"), attrs);
        CTLineRef line = CTLineCreateWithAttributedString(attrStr);
        g_char_width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
        CFRelease(line);
        CFRelease(attrStr);
        CFRelease(attrs);
        CGColorRelease(black);
    }
    return g_char_width;
}

static void draw_gutter(CGContextRef ctx, CGRect bounds, int line_count, CTFontRef font) {
    CGContextSetRGBFillColor(ctx, 0.92, 0.92, 0.92, 1.0);
    CGContextFillRect(ctx, CGRectMake(0, 0, GUTTER_WIDTH, bounds.size.height));

    CGContextSetRGBStrokeColor(ctx, 0.8, 0.8, 0.8, 1.0);
    CGContextSetLineWidth(ctx, 1.0);
    CGPoint divider[2] = { { GUTTER_WIDTH, 0 }, { GUTTER_WIDTH, bounds.size.height } };
    CGContextStrokeLineSegments(ctx, divider, 2);

    CGColorRef gray = CGColorCreateGenericRGB(0.55, 0.55, 0.55, 1.0);
    CFDictionaryRef attrs = make_attrs(font, gray);

    for (int i = 0; i < line_count; i++) {
        char num[16];
        snprintf(num, sizeof(num), "%d", i + 1);
        CFStringRef numStr = CFStringCreateWithCString(NULL, num, kCFStringEncodingUTF8);
        CFAttributedStringRef attrStr = CFAttributedStringCreate(NULL, numStr, attrs);
        CTLineRef line = CTLineCreateWithAttributedString(attrStr);

        double textWidth = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
        double y = bounds.size.height - TOP_PADDING - (i + 1) * LINE_HEIGHT + 4.0;
        CGContextSetTextPosition(ctx, GUTTER_WIDTH - 8.0 - textWidth, y);
        CTLineDraw(line, ctx);

        CFRelease(line);
        CFRelease(attrStr);
        CFRelease(numStr);
    }

    CFRelease(attrs);
    CGColorRelease(gray);
}

void btn_render_frame(CGContextRef ctx, CGRect bounds, Editor *ed) {
    CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
    CGContextFillRect(ctx, bounds);

    CTFontRef font = get_font();
    double char_width = get_char_width();

    CGColorRef black = CGColorCreateGenericRGB(0.1, 0.1, 0.1, 1.0);
    CFDictionaryRef attrs = make_attrs(font, black);

    size_t total_len;
    char *text = editor_copy_all(ed, &total_len);

    int has_sel = editor_has_selection(ed);
    size_t sel_start = has_sel ? editor_selection_start(ed) : 0;
    size_t sel_end = has_sel ? editor_selection_end(ed) : 0;

    size_t line_index = 0;
    size_t line_start = 0;
    for (size_t i = 0; i <= total_len; i++) {
        if (i == total_len || text[i] == '\n') {
            size_t line_len = i - line_start;
            size_t line_end = line_start + line_len;
            double top_y = bounds.size.height - TOP_PADDING - (line_index + 1) * LINE_HEIGHT;

            if (has_sel) {
                size_t hi_from = sel_start > line_start ? sel_start : line_start;
                size_t hi_to = sel_end < line_end ? sel_end : line_end;
                int extends_past_line = (sel_end > line_end) && (sel_start <= line_end);
                if (hi_from < hi_to || extends_past_line) {
                    if (hi_to < hi_from) {
                        hi_to = hi_from;
                    }
                    double hx = GUTTER_WIDTH + LEFT_PADDING + (double)(hi_from - line_start) * char_width;
                    double hw = (double)(hi_to - hi_from) * char_width;
                    if (extends_past_line) {
                        hw += char_width * 0.5;
                    }
                    if (hw < 2.0) {
                        hw = 2.0;
                    }
                    CGContextSetRGBFillColor(ctx, 0.68, 0.82, 1.0, 0.55);
                    CGContextFillRect(ctx, CGRectMake(hx, top_y, hw, LINE_HEIGHT));
                }
            }

            if (line_len > 0) {
                CFStringRef lineStr = CFStringCreateWithBytes(NULL, (const UInt8 *)(text + line_start),
                                                               (CFIndex)line_len, kCFStringEncodingUTF8, false);
                CFAttributedStringRef attrStr = CFAttributedStringCreate(NULL, lineStr, attrs);
                CTLineRef ctLine = CTLineCreateWithAttributedString(attrStr);
                CGContextSetTextPosition(ctx, GUTTER_WIDTH + LEFT_PADDING, top_y + 4.0);
                CTLineDraw(ctLine, ctx);
                CFRelease(ctLine);
                CFRelease(attrStr);
                CFRelease(lineStr);
            }

            line_index++;
            line_start = i + 1;
        }
    }

    free(text);

    if (!has_sel) {
        size_t cur_line = editor_offset_to_line(ed, ed->cursor);
        size_t cstart, clen;
        editor_line_bounds(ed, cur_line, &cstart, &clen);
        size_t col = ed->cursor - cstart;
        double cx = GUTTER_WIDTH + LEFT_PADDING + (double)col * char_width;
        double cy = bounds.size.height - TOP_PADDING - (cur_line + 1) * LINE_HEIGHT;
        CGContextSetRGBFillColor(ctx, 0.1, 0.1, 0.1, 1.0);
        CGContextFillRect(ctx, CGRectMake(cx, cy, 1.4, LINE_HEIGHT - 2));
    }

    draw_gutter(ctx, bounds, (int)editor_line_count(ed), font);

    CFRelease(attrs);
    CGColorRelease(black);
}

size_t btn_hit_test(Editor *ed, CGRect bounds, double x, double y) {
    double char_width = get_char_width();

    double rel_y = bounds.size.height - TOP_PADDING - y;
    long line = (long)(rel_y / LINE_HEIGHT);
    if (line < 0) {
        line = 0;
    }
    size_t line_count = editor_line_count(ed);
    if ((size_t)line >= line_count) {
        line = (long)line_count - 1;
    }

    size_t start, len;
    editor_line_bounds(ed, (size_t)line, &start, &len);

    double rel_x = x - (GUTTER_WIDTH + LEFT_PADDING);
    long col = (long)(rel_x / char_width + 0.5);
    if (col < 0) {
        col = 0;
    }
    if ((size_t)col > len) {
        col = (long)len;
    }

    return start + (size_t)col;
}
