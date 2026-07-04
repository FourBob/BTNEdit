/*
 * Reines Rendering ueber Core Graphics/Core Text - kein AppKit-Widget
 * beteiligt. Platzhalter-Layout fuer Meilenstein 1: fester Zeilenabstand,
 * kein Word-Wrap, kein Scrolling. Wird in Meilenstein 2 durch die
 * eigentliche Text-Engine (Gap Buffer/Piece Table) ersetzt.
 */
#include "render.h"
#include <CoreText/CoreText.h>
#include <string.h>
#include <stdio.h>

#define GUTTER_WIDTH 44.0
#define LINE_HEIGHT 18.0
#define FONT_SIZE 13.0
#define LEFT_PADDING 8.0
#define TOP_PADDING 8.0

static CFDictionaryRef make_attrs(CTFontRef font, CGColorRef color) {
    CFStringRef keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    CFTypeRef values[] = { font, color };
    return CFDictionaryCreate(NULL, (const void **)keys, (const void **)values, 2,
                               &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
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

void btn_render_frame(CGContextRef ctx, CGRect bounds, const char *text) {
    CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
    CGContextFillRect(ctx, bounds);

    CTFontRef font = CTFontCreateWithName(CFSTR("Menlo"), FONT_SIZE, NULL);
    CGColorRef black = CGColorCreateGenericRGB(0.1, 0.1, 0.1, 1.0);
    CFDictionaryRef attrs = make_attrs(font, black);

    size_t len = text ? strlen(text) : 0;

    int total_lines = 1;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            total_lines++;
        }
    }

    int line_index = 0;
    size_t line_start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            size_t line_len = i - line_start;
            CFStringRef lineStr = CFStringCreateWithBytes(NULL, (const UInt8 *)(text + line_start),
                                                           (CFIndex)line_len, kCFStringEncodingUTF8, false);
            CFAttributedStringRef attrStr = CFAttributedStringCreate(NULL, lineStr, attrs);
            CTLineRef ctLine = CTLineCreateWithAttributedString(attrStr);

            double y = bounds.size.height - TOP_PADDING - (line_index + 1) * LINE_HEIGHT + 4.0;
            CGContextSetTextPosition(ctx, GUTTER_WIDTH + LEFT_PADDING, y);
            CTLineDraw(ctLine, ctx);

            CFRelease(ctLine);
            CFRelease(attrStr);
            CFRelease(lineStr);

            line_index++;
            line_start = i + 1;
        }
    }

    draw_gutter(ctx, bounds, total_lines, font);

    CFRelease(attrs);
    CGColorRelease(black);
    CFRelease(font);
}
