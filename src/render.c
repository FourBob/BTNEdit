/*
 * Reines Rendering ueber Core Graphics/Core Text - kein AppKit-Widget
 * beteiligt. Monospace-Schrift (Menlo), fester Zeilenabstand. Wortumbruch
 * wird hier (nicht in editor.c) als "Row-Layout" berechnet, weil die
 * Umbruchpunkte von der Fensterbreite/Zeichenbreite abhaengen - editor.c
 * bleibt bewusst praesentationsunabhaengig und kennt nur logische Zeilen
 * (durch '\n' getrennt). Eine Row ist eine visuelle Zeile nach Umbruch;
 * main.c nutzt dasselbe Row-Layout fuer Cursor-Bewegung/Scrollen, damit
 * Zeichnen und Interaktion nie auseinanderlaufen (siehe render.h).
 */
#include "render.h"
#include <CoreText/CoreText.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define GUTTER_WIDTH 44.0
#define LINE_HEIGHT BTN_LINE_HEIGHT
#define FONT_SIZE 13.0
#define LEFT_PADDING 8.0
#define TOP_PADDING 8.0
#define BTN_MAX_TOKENS_PER_LINE 512

static CTFontRef g_font = NULL;
static double g_char_width = 0.0;
static CGColorRef g_token_colors[6] = { NULL, NULL, NULL, NULL, NULL, NULL };

static CGColorRef get_token_color(BtnTokenKind kind) {
    if (!g_token_colors[kind]) {
        switch (kind) {
            case BTN_TOK_KEYWORD:
                g_token_colors[kind] = CGColorCreateGenericRGB(0.64, 0.11, 0.57, 1.0);
                break;
            case BTN_TOK_STRING:
                g_token_colors[kind] = CGColorCreateGenericRGB(0.77, 0.10, 0.09, 1.0);
                break;
            case BTN_TOK_COMMENT:
                g_token_colors[kind] = CGColorCreateGenericRGB(0.24, 0.50, 0.26, 1.0);
                break;
            case BTN_TOK_NUMBER:
                g_token_colors[kind] = CGColorCreateGenericRGB(0.11, 0.0, 0.87, 1.0);
                break;
            case BTN_TOK_PREPROCESSOR:
                g_token_colors[kind] = CGColorCreateGenericRGB(0.50, 0.28, 0.09, 1.0);
                break;
            default:
                g_token_colors[kind] = CGColorCreateGenericRGB(0.1, 0.1, 0.1, 1.0);
                break;
        }
    }
    return g_token_colors[kind];
}

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
 * fuer Cursor/Selektion/Hit-Testing gilt. Deshalb wird pro Row eine reine
 * Anzeige-Kopie erzeugt, in der Tabs bereits zu Leerzeichen expandiert sind. */
static char *expand_tabs_for_display(const char *text, size_t len, size_t *out_len) {
    size_t cap = len + 1;
    char *out = malloc(cap);
    size_t n = 0;
    size_t col = 0;

    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        size_t needed = (c == '\t') ? (editor_tab_advance(col) - col) : 1;
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

/* CFAttributedString-Ranges zaehlen in UTF-16-Code-Units, waehrend unsere
 * eigene Spaltenrechnung (editor_visual_column_in_range) in UTF-8-Bytes
 * zaehlt. Fuer reinen ASCII-Text ist das identisch, aber jedes mehrbytige
 * Zeichen wuerde sonst eine zu lange/falsche CFRange erzeugen. Dekodiert
 * einfach den Praefix bis byte_offset erneut und misst dessen echte Laenge -
 * das ist fuer die kurzen Zeilen, die wir hier behandeln, guenstig genug. */
static CFIndex utf16_offset_for_byte_offset(const char *utf8, size_t byte_offset) {
    if (byte_offset == 0) {
        return 0;
    }
    CFStringRef prefix = CFStringCreateWithBytes(NULL, (const UInt8 *)utf8, (CFIndex)byte_offset,
                                                  kCFStringEncodingUTF8, false);
    CFIndex len = prefix ? CFStringGetLength(prefix) : 0;
    if (prefix) {
        CFRelease(prefix);
    }
    return len;
}

/* ---- Wortumbruch-Layout ---- */

double btn_layout_text_width(CGRect bounds) {
    double w = bounds.size.width - GUTTER_WIDTH - LEFT_PADDING - LEFT_PADDING;
    return w > 1.0 ? w : 1.0;
}

static long chars_per_row_for(double text_width) {
    long n = (long)(text_width / get_char_width());
    return n > 0 ? n : 1;
}

static void rows_push(BtnRow **rows, size_t *count, size_t *cap,
                       size_t start, size_t len, size_t logical_line, int is_continuation) {
    if (*count == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *rows = realloc(*rows, (*cap) * sizeof(BtnRow));
    }
    (*rows)[*count].start = start;
    (*rows)[*count].len = len;
    (*rows)[*count].logical_line = logical_line;
    (*rows)[*count].is_continuation = is_continuation;
    (*count)++;
}

/* Ein einziger Vorwaertsdurchlauf ueber den ganzen Puffer statt "pro
 * logischer Zeile editor_line_bounds() aufrufen" (das waere O(Zeilen *
 * Zeichen), weil editor_line_bounds selbst jedes Mal von vorn scannt) -
 * Zeilenenden ('\n') und Umbruchpunkte werden in derselben Schleife
 * erkannt, macht die Layout-Berechnung O(Zeichen) statt O(Zeilen*Zeichen). */
BtnRow *btn_layout_build(Editor *ed, double text_width, size_t *out_row_count) {
    long chars_per_row = chars_per_row_for(text_width);

    size_t cap = 0, count = 0;
    BtnRow *rows = NULL;

    size_t total_len = editor_length(ed);
    size_t logical_line = 0;
    size_t line_start = 0;
    size_t row_start = 0;
    size_t last_break = (size_t)-1;
    long col = 0;
    size_t i = 0;

    while (i <= total_len) {
        if (i == total_len || gb_char_at(&ed->buffer, i) == '\n') {
            rows_push(&rows, &count, &cap, row_start, i - row_start, logical_line, row_start != line_start);
            logical_line++;
            line_start = i + 1;
            row_start = line_start;
            last_break = (size_t)-1;
            col = 0;
            i++;
            continue;
        }

        char c = gb_char_at(&ed->buffer, i);
        long new_col = (c == '\t') ? (long)editor_tab_advance((size_t)col) : col + 1;

        if (new_col > chars_per_row && i > row_start) {
            size_t break_at = (last_break != (size_t)-1 && last_break > row_start) ? last_break : i;
            rows_push(&rows, &count, &cap, row_start, break_at - row_start, logical_line, row_start != line_start);
            row_start = break_at;
            col = (long)editor_visual_column_in_range(ed, row_start, i);
            last_break = (size_t)-1;
            continue;
        }

        if (c == ' ' || c == '\t') {
            last_break = i + 1;
        }
        col = new_col;
        i++;
    }

    *out_row_count = count;
    return rows;
}

void btn_layout_free(BtnRow *rows) {
    free(rows);
}

size_t btn_layout_row_for_offset(const BtnRow *rows, size_t row_count, size_t offset) {
    for (size_t i = 0; i + 1 < row_count; i++) {
        if (offset < rows[i + 1].start) {
            return i;
        }
    }
    return row_count > 0 ? row_count - 1 : 0;
}

/* ---- Zeichnen ---- */

static void draw_gutter(CGContextRef ctx, CGRect bounds, const BtnRow *rows, size_t row_count,
                         long scroll_row, CTFontRef font) {
    CGContextSetRGBFillColor(ctx, 0.92, 0.92, 0.92, 1.0);
    CGContextFillRect(ctx, CGRectMake(0, BTN_FOOTER_HEIGHT, GUTTER_WIDTH, bounds.size.height - BTN_FOOTER_HEIGHT));

    CGContextSetRGBStrokeColor(ctx, 0.8, 0.8, 0.8, 1.0);
    CGContextSetLineWidth(ctx, 1.0);
    CGPoint divider[2] = { { GUTTER_WIDTH, BTN_FOOTER_HEIGHT }, { GUTTER_WIDTH, bounds.size.height } };
    CGContextStrokeLineSegments(ctx, divider, 2);

    CGColorRef gray = CGColorCreateGenericRGB(0.55, 0.55, 0.55, 1.0);
    CFDictionaryRef attrs = make_attrs(font, gray);

    for (size_t r = (size_t)scroll_row; r < row_count; r++) {
        /* Dieselbe top_y-Formel und Abbruchbedingung wie btn_render_frame's
         * Zeilen-Schleife (siehe dort) - sonst hoert diese Schleife bei
         * einer anderen Reihe auf als die Text-Zeichnung, und die unterste
         * sichtbare Zeile bekommt Text, aber keine Zeilennummer. */
        double top_y = bounds.size.height - TOP_PADDING - (double)(r - (size_t)scroll_row + 1) * LINE_HEIGHT;
        if (top_y + LINE_HEIGHT < BTN_FOOTER_HEIGHT) {
            break;
        }
        if (rows[r].is_continuation) {
            continue;
        }

        char num[16];
        snprintf(num, sizeof(num), "%zu", rows[r].logical_line + 1);
        CFStringRef numStr = CFStringCreateWithCString(NULL, num, kCFStringEncodingUTF8);
        CFAttributedStringRef attrStr = CFAttributedStringCreate(NULL, numStr, attrs);
        CTLineRef line = CTLineCreateWithAttributedString(attrStr);

        double textWidth = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
        CGContextSetTextPosition(ctx, GUTTER_WIDTH - 8.0 - textWidth, top_y + 4.0);
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

/* Kommentar-Zustand direkt vor logical_line, indem alle vorherigen Zeilen
 * einmal (nur fuers Zustands-Tracking, max_tokens=0) tokenisiert werden -
 * noetig, damit mehrzeilige Blockkommentare beim Scrollen mitten ins
 * Dokument korrekt erkannt werden. Ein einziger Vorwaertsdurchlauf (statt
 * pro Zeile editor_line_bounds() aufzurufen, was selbst wieder von vorn
 * scannt) haelt das bei O(Zeichen) statt O(Zeilen*Zeichen). */
static int comment_state_before_line(Editor *ed, const BtnLangSpec *lang, size_t logical_line) {
    int state = 0;
    if (logical_line == 0) {
        return state;
    }

    size_t total_len = editor_length(ed);
    size_t li = 0;
    size_t line_start = 0;

    for (size_t i = 0; i <= total_len && li < logical_line; i++) {
        if (i == total_len || gb_char_at(&ed->buffer, i) == '\n') {
            size_t line_len = i - line_start;
            char *text = gb_copy_range(&ed->buffer, line_start, line_len);
            int ends;
            btn_highlight_tokenize(text, line_len, lang, state, &ends, NULL, 0);
            free(text);
            state = ends;
            li++;
            line_start = i + 1;
        }
    }
    return state;
}

void btn_render_frame(CGContextRef ctx, CGRect bounds, Editor *ed, long scroll_row, const BtnLangSpec *lang) {
    CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
    CGContextFillRect(ctx, bounds);

    CTFontRef font = get_font();
    double char_width = get_char_width();

    CGColorRef black = CGColorCreateGenericRGB(0.1, 0.1, 0.1, 1.0);
    CFDictionaryRef attrs = make_attrs(font, black);

    double text_width = btn_layout_text_width(bounds);
    size_t row_count;
    BtnRow *rows = btn_layout_build(ed, text_width, &row_count);

    int has_sel = editor_has_selection(ed);
    size_t sel_start = has_sel ? editor_selection_start(ed) : 0;
    size_t sel_end = has_sel ? editor_selection_end(ed) : 0;

    /* Tokens werden nur einmal pro logischer Zeile berechnet und ueber alle
     * ihre umgebrochenen Rows wiederverwendet (Zeilen sind in Dokument-
     * reihenfolge, logical_line ist also innerhalb der sichtbaren Rows
     * monoton steigend). */
    size_t cached_line = (size_t)-1;
    size_t cached_line_start = 0;
    BtnToken tokens[BTN_MAX_TOKENS_PER_LINE];
    size_t token_count = 0;
    int comment_state = 0;
    if (lang && row_count > 0) {
        size_t first_row = (size_t)scroll_row < row_count ? (size_t)scroll_row : row_count - 1;
        comment_state = comment_state_before_line(ed, lang, rows[first_row].logical_line);
    }

    for (size_t r = (size_t)scroll_row; r < row_count; r++) {
        double top_y = bounds.size.height - TOP_PADDING - (double)(r - (size_t)scroll_row + 1) * LINE_HEIGHT;
        if (top_y + LINE_HEIGHT < BTN_FOOTER_HEIGHT) {
            break;
        }

        size_t row_start = rows[r].start;
        size_t row_len = rows[r].len;
        size_t row_end = row_start + row_len;

        if (lang) {
            size_t ll = rows[r].logical_line;
            if (ll != cached_line) {
                size_t ls, llen;
                editor_line_bounds(ed, ll, &ls, &llen);
                char *text = gb_copy_range(&ed->buffer, ls, llen);
                int ends;
                token_count = btn_highlight_tokenize(text, llen, lang, comment_state, &ends,
                                                      tokens, BTN_MAX_TOKENS_PER_LINE);
                if (token_count > BTN_MAX_TOKENS_PER_LINE) {
                    token_count = BTN_MAX_TOKENS_PER_LINE;
                }
                free(text);
                comment_state = ends;
                cached_line = ll;
                cached_line_start = ls;
            }
        }

        if (has_sel) {
            size_t hi_from = sel_start > row_start ? sel_start : row_start;
            size_t hi_to = sel_end < row_end ? sel_end : row_end;
            int extends_past_row = (sel_end > row_end) && (sel_start <= row_end) &&
                                    (r + 1 < row_count) && rows[r + 1].is_continuation;
            if (hi_from < hi_to || extends_past_row) {
                if (hi_to < hi_from) {
                    hi_to = hi_from;
                }
                size_t col_from = editor_visual_column_in_range(ed, row_start, hi_from);
                size_t col_to = editor_visual_column_in_range(ed, row_start, hi_to);
                double hx = GUTTER_WIDTH + LEFT_PADDING + (double)col_from * char_width;
                double hw = (double)(col_to - col_from) * char_width;
                if (extends_past_row) {
                    hw += char_width * 0.5;
                }
                if (hw < 2.0) {
                    hw = 2.0;
                }
                CGContextSetRGBFillColor(ctx, 0.68, 0.82, 1.0, 0.55);
                CGContextFillRect(ctx, CGRectMake(hx, top_y, hw, LINE_HEIGHT));
            }
        }

        if (row_len > 0) {
            char *raw = gb_copy_range(&ed->buffer, row_start, row_len);
            size_t disp_len;
            char *disp = expand_tabs_for_display(raw, row_len, &disp_len);
            free(raw);

            CFStringRef lineStr = CFStringCreateWithBytes(NULL, (const UInt8 *)disp,
                                                           (CFIndex)disp_len, kCFStringEncodingUTF8, false);
            /* CFAttributedString-Ranges zaehlen in UTF-16-Einheiten, waehrend
             * disp_len/col_from/col_to UTF-8-BYTES zaehlen - fuer reinen
             * ASCII-Text ist das zufaellig identisch, aber jedes mehrbytige
             * Zeichen (Umlaut, Akzent, Emoji) wuerde sonst eine zu lange
             * Range anfordern (Absturz/undefiniertes Verhalten) oder falsch
             * eingefaerbte Grenzen erzeugen. utf16_offset_for_byte_offset()
             * rechnet die Byte-Position sauber in die tatsaechliche
             * String-Position um. */
            CFIndex utf16_len = CFStringGetLength(lineStr);
            CFMutableAttributedStringRef attrStr = CFAttributedStringCreateMutable(NULL, 0);
            CFAttributedStringReplaceString(attrStr, CFRangeMake(0, 0), lineStr);
            CFAttributedStringSetAttributes(attrStr, CFRangeMake(0, utf16_len), attrs, true);

            for (size_t t = 0; t < token_count; t++) {
                if (tokens[t].kind == BTN_TOK_NORMAL) {
                    continue;
                }
                size_t tok_abs_start = cached_line_start + tokens[t].start;
                size_t tok_abs_end = tok_abs_start + tokens[t].len;
                size_t clip_start = tok_abs_start > row_start ? tok_abs_start : row_start;
                size_t clip_end = tok_abs_end < row_end ? tok_abs_end : row_end;
                if (clip_start >= clip_end) {
                    continue;
                }
                size_t col_from = editor_visual_column_in_range(ed, row_start, clip_start);
                size_t col_to = editor_visual_column_in_range(ed, row_start, clip_end);
                if (col_to > disp_len) {
                    col_to = disp_len;
                }
                if (col_from >= col_to) {
                    continue;
                }
                CFIndex u16_from = utf16_offset_for_byte_offset(disp, col_from);
                CFIndex u16_to = utf16_offset_for_byte_offset(disp, col_to);
                if (u16_to > utf16_len) {
                    u16_to = utf16_len;
                }
                if (u16_from >= u16_to) {
                    continue;
                }
                CFAttributedStringSetAttribute(attrStr, CFRangeMake(u16_from, u16_to - u16_from),
                                                kCTForegroundColorAttributeName, get_token_color(tokens[t].kind));
            }

            CTLineRef ctLine = CTLineCreateWithAttributedString(attrStr);
            CGContextSetTextPosition(ctx, GUTTER_WIDTH + LEFT_PADDING, top_y + 4.0);
            CTLineDraw(ctLine, ctx);
            CFRelease(ctLine);
            CFRelease(attrStr);
            CFRelease(lineStr);
            free(disp);
        }
    }

    if (!has_sel) {
        size_t cur_row = btn_layout_row_for_offset(rows, row_count, ed->cursor);
        if (cur_row >= (size_t)scroll_row) {
            size_t col = editor_visual_column_in_range(ed, rows[cur_row].start, ed->cursor);
            double cx = GUTTER_WIDTH + LEFT_PADDING + (double)col * char_width;
            double cy = bounds.size.height - TOP_PADDING - (double)(cur_row - (size_t)scroll_row + 1) * LINE_HEIGHT;
            CGContextSetRGBFillColor(ctx, 0.1, 0.1, 0.1, 1.0);
            CGContextFillRect(ctx, CGRectMake(cx, cy, 1.4, LINE_HEIGHT - 2));
        }
    }

    draw_gutter(ctx, bounds, rows, row_count, scroll_row, font);
    draw_footer(ctx, bounds, ed);

    btn_layout_free(rows);
    CFRelease(attrs);
    CGColorRelease(black);
}

size_t btn_hit_test(Editor *ed, CGRect bounds, double x, double y, long scroll_row) {
    double char_width = get_char_width();
    double text_width = btn_layout_text_width(bounds);
    size_t row_count;
    BtnRow *rows = btn_layout_build(ed, text_width, &row_count);

    double rel_y = bounds.size.height - TOP_PADDING - y;
    long row = scroll_row + (long)(rel_y / LINE_HEIGHT);
    if (row < 0) {
        row = 0;
    }
    if ((size_t)row >= row_count) {
        row = (long)row_count - 1;
    }

    double rel_x = x - (GUTTER_WIDTH + LEFT_PADDING);
    long col = (long)(rel_x / char_width + 0.5);
    if (col < 0) {
        col = 0;
    }

    size_t offset = editor_offset_for_column_in_range(ed, rows[row].start, rows[row].len, (size_t)col);
    btn_layout_free(rows);
    return offset;
}
