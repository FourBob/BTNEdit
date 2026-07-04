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

/* CoreText hat eine eigene, von uns unabhaengige Tab-Stop-Logik - wuerden
 * wir ein rohes '\t'-Byte durchreichen, wuerde die Zeichenposition nicht
 * mehr zu unserer eigenen (tab-bewussten) Spaltenrechnung passen, die auch
 * fuer Cursor/Selektion/Hit-Testing gilt. Deshalb wird pro Zeile eine reine
 * Anzeige-Kopie erzeugt, in der Tabs bereits zu Leerzeichen expandiert sind. */
static char *expand_tabs_for_display(const char *line, size_t line_len, size_t *out_len) {
    size_t cap = line_len + 1;
    char *out = malloc(cap);
    size_t n = 0;
    size_t col = 0;

    for (size_t i = 0; i < line_len; i++) {
        char c = line[i];
        size_t needed = (c == '\t') ? (((col / BTN_TAB_WIDTH) + 1) * BTN_TAB_WIDTH - col) : 1;
        if (n + needed > cap) {
            cap = (n + needed) * 2;
            out = realloc(out, cap);
        }
        if (c == '\t') {
            for (size_t s = 0; s < needed; s++) {
                out[n++] = ' ';
            }
            col += needed;
        } else {
            out[n++] = c;
            col++;
        }
    }

    *out_len = n;
    return out;
}

static void draw_gutter(CGContextRef ctx, CGRect bounds, int line_count, CTFontRef font) {
    CGContextSetRGBFillColor(ctx, 0.92, 0.92, 0.92, 1.0);
    CGContextFillRect(ctx, CGRectMake(0, BTN_FOOTER_HEIGHT, GUTTER_WIDTH, bounds.size.height - BTN_FOOTER_HEIGHT));

    CGContextSetRGBStrokeColor(ctx, 0.8, 0.8, 0.8, 1.0);
    CGContextSetLineWidth(ctx, 1.0);
    CGPoint divider[2] = { { GUTTER_WIDTH, BTN_FOOTER_HEIGHT }, { GUTTER_WIDTH, bounds.size.height } };
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

static void draw_footer(CGContextRef ctx, CGRect bounds, Editor *ed) {
    CGContextSetRGBFillColor(ctx, 0.94, 0.94, 0.94, 1.0);
    CGContextFillRect(ctx, CGRectMake(0, 0, bounds.size.width, BTN_FOOTER_HEIGHT));

    CGContextSetRGBStrokeColor(ctx, 0.8, 0.8, 0.8, 1.0);
    CGContextSetLineWidth(ctx, 1.0);
    CGPoint divider[2] = { { 0, BTN_FOOTER_HEIGHT }, { bounds.size.width, BTN_FOOTER_HEIGHT } };
    CGContextStrokeLineSegments(ctx, divider, 2);

    CGColorRef gray = CGColorCreateGenericRGB(0.35, 0.35, 0.35, 1.0);
    CFDictionaryRef attrs = make_attrs(get_font(), gray);

    size_t cur_line = editor_offset_to_line(ed, ed->cursor);
    size_t col = editor_visual_column(ed, ed->cursor);

    char left[64];
    snprintf(left, sizeof(left), "Zeile %zu, Spalte %zu", cur_line + 1, col + 1);

    char right[160];
    snprintf(right, sizeof(right), "%zu Zeilen | %zu Woerter | %zu Zeichen | UTF-8",
             editor_line_count(ed), editor_word_count(ed), editor_length(ed));

    double text_y = (BTN_FOOTER_HEIGHT - FONT_SIZE) / 2.0 + 3.0;

    CFStringRef leftStr = CFStringCreateWithCString(NULL, left, kCFStringEncodingUTF8);
    CFAttributedStringRef leftAttrStr = CFAttributedStringCreate(NULL, leftStr, attrs);
    CTLineRef leftLine = CTLineCreateWithAttributedString(leftAttrStr);
    CGContextSetTextPosition(ctx, LEFT_PADDING, text_y);
    CTLineDraw(leftLine, ctx);
    CFRelease(leftLine);
    CFRelease(leftAttrStr);
    CFRelease(leftStr);

    CFStringRef rightStr = CFStringCreateWithCString(NULL, right, kCFStringEncodingUTF8);
    CFAttributedStringRef rightAttrStr = CFAttributedStringCreate(NULL, rightStr, attrs);
    CTLineRef rightLine = CTLineCreateWithAttributedString(rightAttrStr);
    double rightWidth = CTLineGetTypographicBounds(rightLine, NULL, NULL, NULL);
    CGContextSetTextPosition(ctx, bounds.size.width - LEFT_PADDING - rightWidth, text_y);
    CTLineDraw(rightLine, ctx);
    CFRelease(rightLine);
    CFRelease(rightAttrStr);
    CFRelease(rightStr);

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
                    size_t col_from = editor_visual_column(ed, hi_from);
                    size_t col_to = editor_visual_column(ed, hi_to);
                    double hx = GUTTER_WIDTH + LEFT_PADDING + (double)col_from * char_width;
                    double hw = (double)(col_to - col_from) * char_width;
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
                size_t disp_len;
                char *disp = expand_tabs_for_display(text + line_start, line_len, &disp_len);
                CFStringRef lineStr = CFStringCreateWithBytes(NULL, (const UInt8 *)disp,
                                                               (CFIndex)disp_len, kCFStringEncodingUTF8, false);
                CFAttributedStringRef attrStr = CFAttributedStringCreate(NULL, lineStr, attrs);
                CTLineRef ctLine = CTLineCreateWithAttributedString(attrStr);
                CGContextSetTextPosition(ctx, GUTTER_WIDTH + LEFT_PADDING, top_y + 4.0);
                CTLineDraw(ctLine, ctx);
                CFRelease(ctLine);
                CFRelease(attrStr);
                CFRelease(lineStr);
                free(disp);
            }

            line_index++;
            line_start = i + 1;
        }
    }

    free(text);

    if (!has_sel) {
        size_t cur_line = editor_offset_to_line(ed, ed->cursor);
        size_t col = editor_visual_column(ed, ed->cursor);
        double cx = GUTTER_WIDTH + LEFT_PADDING + (double)col * char_width;
        double cy = bounds.size.height - TOP_PADDING - (cur_line + 1) * LINE_HEIGHT;
        CGContextSetRGBFillColor(ctx, 0.1, 0.1, 0.1, 1.0);
        CGContextFillRect(ctx, CGRectMake(cx, cy, 1.4, LINE_HEIGHT - 2));
    }

    draw_gutter(ctx, bounds, (int)editor_line_count(ed), font);
    draw_footer(ctx, bounds, ed);

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

    double rel_x = x - (GUTTER_WIDTH + LEFT_PADDING);
    long col = (long)(rel_x / char_width + 0.5);
    if (col < 0) {
        col = 0;
    }

    return editor_offset_for_column(ed, (size_t)line, (size_t)col);
}
