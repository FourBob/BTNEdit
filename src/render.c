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
#define LEFT_PADDING 8.0
#define TOP_PADDING 8.0
#define BTN_MAX_TOKENS_PER_LINE 512
#define PRINT_MARGIN 24.0

/* FONT_SIZE war frueher ein fixes #define - main.c braucht jetzt eine
 * Laufzeit-Schriftgroesse fuer Cmd+/Cmd-/Cmd+0 (siehe btn_render_set_font_size()
 * in render.h), deshalb eine gecachte Variable statt eines Makros. */
static double g_font_size = BTN_DEFAULT_FONT_SIZE;
static CTFontRef g_font = NULL;
static double g_char_width = 0.0;
static CGColorRef g_token_colors[6] = { NULL, NULL, NULL, NULL, NULL, NULL };

/* Gecachte Text-/Dim-/Akzent-/Gutter-/Footer-Farben und ihre fertigen
 * CFDictionaryRef-Attribute (Font+Farbe) - ohne diesen Cache wuerden
 * draw_gutter()/draw_footer()/btn_render_tab_bar()/btn_render_find_bar()/
 * btn_render_frame() bei JEDEM Redraw (also bei jedem Tastendruck, da die
 * App das komplette Fenster neu zeichnet statt nur die betroffene Region)
 * dieselben CGColorRef/CFDictionaryRef-Objekte neu anlegen und sofort
 * wieder freigeben - reiner Overhead, da Font und Farben sich zwischen zwei
 * Tastendruecken so gut wie nie aendern. Analog zu get_token_color() unten. */
static CGColorRef g_text_color = NULL;
static CGColorRef g_dim_color = NULL;
static CGColorRef g_accent_color = NULL;
static CGColorRef g_gutter_text_color = NULL;
static CGColorRef g_footer_text_color = NULL;
static CFDictionaryRef g_text_attrs = NULL;
static CFDictionaryRef g_dim_attrs = NULL;
static CFDictionaryRef g_accent_attrs = NULL;
static CFDictionaryRef g_gutter_attrs = NULL;
static CFDictionaryRef g_footer_attrs = NULL;

/* 0 = Light Mode, 1 = Dark Mode - main.c setzt das bei jedem Redraw frisch
 * (siehe btn_render_set_dark_mode() in render.h). */
static int g_dark_mode = 0;

typedef struct { double r, g, b, a; } BtnColor;

/* Kompakter Weg, ohne fuer jede der ~20 semantischen Farben unten eine
 * eigene if/else-Funktion zu schreiben: ein Light/Dark-Wertepaar rein,
 * passend zu g_dark_mode raus. Alle Farbrollen (Hintergrund, Text, Trenn-
 * linien, Hervorhebungen, ...) sind als kleine Funktionen direkt darunter
 * definiert, die diesen Helfer mit ihrem jeweiligen Wertepaar aufrufen -
 * ein Farbwechsel betrifft so nur EINE Zeile statt vieler verstreuter
 * CGContextSetRGB*Color-Aufrufe wie vor dem Dark-Mode-Support. */
static BtnColor pick_color(double lr, double lg, double lb, double la,
                            double dr, double dg, double db, double da) {
    BtnColor c;
    if (g_dark_mode) {
        c.r = dr; c.g = dg; c.b = db; c.a = da;
    } else {
        c.r = lr; c.g = lg; c.b = lb; c.a = la;
    }
    return c;
}

static void set_fill(CGContextRef ctx, BtnColor c) {
    CGContextSetRGBFillColor(ctx, c.r, c.g, c.b, c.a);
}

static void set_stroke(CGContextRef ctx, BtnColor c) {
    CGContextSetRGBStrokeColor(ctx, c.r, c.g, c.b, c.a);
}

static CGColorRef create_cg_color(BtnColor c) {
    return CGColorCreateGenericRGB(c.r, c.g, c.b, c.a);
}

/* ---- Farbpalette: je eine Funktion pro semantischer Rolle, Light-Werte
 * sind die vor dem Dark-Mode-Support fest verdrahteten Original-Werte
 * (unveraendert), Dark-Werte sind neu gewaehlt fuer ausreichend Kontrast
 * auf dunklem Grund. ---- */
static BtnColor col_bg(void)              { return pick_color(1.0, 1.0, 1.0, 1.0,   0.12, 0.12, 0.13, 1.0); }
static BtnColor col_text(void)            { return pick_color(0.1, 0.1, 0.1, 1.0,   0.88, 0.88, 0.88, 1.0); }
static BtnColor col_dim(void)             { return pick_color(0.45, 0.45, 0.45, 1.0, 0.62, 0.62, 0.64, 1.0); }
static BtnColor col_divider(void)         { return pick_color(0.8, 0.8, 0.8, 1.0,   0.30, 0.30, 0.33, 1.0); }
static BtnColor col_gutter_bg(void)       { return pick_color(0.92, 0.92, 0.92, 1.0, 0.16, 0.16, 0.18, 1.0); }
static BtnColor col_gutter_text(void)     { return pick_color(0.55, 0.55, 0.55, 1.0, 0.55, 0.55, 0.58, 1.0); }
static BtnColor col_footer_bg(void)       { return pick_color(0.94, 0.94, 0.94, 1.0, 0.16, 0.16, 0.18, 1.0); }
static BtnColor col_footer_text(void)     { return pick_color(0.35, 0.35, 0.35, 1.0, 0.65, 0.65, 0.68, 1.0); }
static BtnColor col_tab_bar_bg(void)      { return pick_color(0.80, 0.80, 0.80, 1.0, 0.18, 0.18, 0.20, 1.0); }
static BtnColor col_tab_active_bg(void)   { return pick_color(0.97, 0.97, 0.97, 1.0, 0.24, 0.24, 0.27, 1.0); }
static BtnColor col_tab_divider(void)     { return pick_color(0.65, 0.65, 0.65, 1.0, 0.30, 0.30, 0.33, 1.0); }
static BtnColor col_find_bar_bg(void)     { return pick_color(0.90, 0.90, 0.90, 1.0, 0.18, 0.18, 0.20, 1.0); }
static BtnColor col_accent(void)          { return pick_color(0.20, 0.40, 0.85, 1.0, 0.40, 0.60, 0.98, 1.0); }
static BtnColor col_toggle_bg(int active) {
    return active ? pick_color(0.80, 0.85, 0.97, 1.0, 0.25, 0.33, 0.48, 1.0)
                  : pick_color(0.90, 0.90, 0.90, 1.0, 0.18, 0.18, 0.20, 1.0);
}
static BtnColor col_button_bg(void)       { return pick_color(0.80, 0.80, 0.80, 1.0, 0.22, 0.22, 0.25, 1.0); }
static BtnColor col_field_selection(void) { return pick_color(0.68, 0.82, 1.0, 0.55, 0.25, 0.42, 0.68, 0.55); }
static BtnColor col_cursor(void)          { return pick_color(0.1, 0.1, 0.1, 1.0,   0.92, 0.92, 0.92, 1.0); }
static BtnColor col_match_highlight(void) { return pick_color(1.0, 0.85, 0.25, 0.45, 0.60, 0.48, 0.08, 0.55); }
static BtnColor col_selection(void)       { return pick_color(0.68, 0.82, 1.0, 0.55, 0.25, 0.42, 0.68, 0.55); }
static BtnColor col_bracket(void)         { return pick_color(0.75, 0.85, 1.0, 0.6,  0.30, 0.45, 0.68, 0.6); }
static BtnColor col_scroll_knob(int active) {
    return active ? pick_color(0.0, 0.0, 0.0, 0.55, 1.0, 1.0, 1.0, 0.55)
                  : pick_color(0.0, 0.0, 0.0, 0.28, 1.0, 1.0, 1.0, 0.28);
}

static CGColorRef get_token_color(BtnTokenKind kind) {
    if (!g_token_colors[kind]) {
        BtnColor c;
        switch (kind) {
            case BTN_TOK_KEYWORD:
                c = pick_color(0.64, 0.11, 0.57, 1.0,  0.94, 0.50, 0.85, 1.0);
                break;
            case BTN_TOK_STRING:
                c = pick_color(0.77, 0.10, 0.09, 1.0,  0.98, 0.55, 0.52, 1.0);
                break;
            case BTN_TOK_COMMENT:
                c = pick_color(0.24, 0.50, 0.26, 1.0,  0.45, 0.78, 0.48, 1.0);
                break;
            case BTN_TOK_NUMBER:
                c = pick_color(0.11, 0.0, 0.87, 1.0,   0.60, 0.60, 1.0, 1.0);
                break;
            case BTN_TOK_PREPROCESSOR:
                c = pick_color(0.50, 0.28, 0.09, 1.0,  0.90, 0.62, 0.38, 1.0);
                break;
            default:
                c = col_text();
                break;
        }
        g_token_colors[kind] = create_cg_color(c);
    }
    return g_token_colors[kind];
}

/* Verwirft alle gecachten Ressourcen, die von Schriftgroesse/Modus abhaengen -
 * gemeinsam genutzt von btn_render_set_dark_mode() (Farben aendern sich) und
 * btn_render_set_font_size() (die Attribut-Dicts enthalten den Font, der
 * Farbanteil bleibt zwar gleich, wird hier der Einfachheit halber aber
 * mitinvalidiert statt zwei separate Cache-Ebenen zu pflegen). */
static void invalidate_style_cache(void) {
    for (int i = 0; i < 6; i++) {
        if (g_token_colors[i]) {
            CGColorRelease(g_token_colors[i]);
            g_token_colors[i] = NULL;
        }
    }
    if (g_text_attrs)   { CFRelease(g_text_attrs);   g_text_attrs = NULL; }
    if (g_dim_attrs)    { CFRelease(g_dim_attrs);    g_dim_attrs = NULL; }
    if (g_accent_attrs) { CFRelease(g_accent_attrs); g_accent_attrs = NULL; }
    if (g_gutter_attrs) { CFRelease(g_gutter_attrs); g_gutter_attrs = NULL; }
    if (g_footer_attrs) { CFRelease(g_footer_attrs); g_footer_attrs = NULL; }
    if (g_text_color)        { CGColorRelease(g_text_color);        g_text_color = NULL; }
    if (g_dim_color)         { CGColorRelease(g_dim_color);         g_dim_color = NULL; }
    if (g_accent_color)      { CGColorRelease(g_accent_color);      g_accent_color = NULL; }
    if (g_gutter_text_color) { CGColorRelease(g_gutter_text_color); g_gutter_text_color = NULL; }
    if (g_footer_text_color) { CGColorRelease(g_footer_text_color); g_footer_text_color = NULL; }
}

void btn_render_set_dark_mode(int dark) {
    int new_dark = dark ? 1 : 0;
    if (new_dark != g_dark_mode) {
        g_dark_mode = new_dark;
        invalidate_style_cache();
    }
}

static CTFontRef get_font(void) {
    if (!g_font) {
        g_font = CTFontCreateWithName(CFSTR("Menlo"), g_font_size, NULL);
    }
    return g_font;
}

void btn_render_set_font_size(double size) {
    /* Negiert formuliert, damit auch NaN (z.B. "nan" in ~/.btnedit_prefs,
     * das fscanf("%lf") klaglos liest) geklemmt wird: jeder Vergleich mit
     * NaN ist falsch, "size < MIN" liess es durch. */
    if (!(size >= BTN_MIN_FONT_SIZE)) {
        size = BTN_MIN_FONT_SIZE;
    }
    if (!(size <= BTN_MAX_FONT_SIZE)) {
        size = BTN_MAX_FONT_SIZE;
    }
    if (size == g_font_size) {
        return;
    }
    g_font_size = size;
    if (g_font) {
        CFRelease(g_font);
        g_font = NULL;
    }
    g_char_width = 0.0; /* neu vermessen bei naechstem get_char_width() */
    invalidate_style_cache(); /* Attribut-Dicts referenzieren noch den alten Font */
}

double btn_render_get_font_size(void) {
    return g_font_size;
}

void btn_render_zoom_in(void) {
    btn_render_set_font_size(g_font_size + 1.0);
}

void btn_render_zoom_out(void) {
    btn_render_set_font_size(g_font_size - 1.0);
}

void btn_render_zoom_reset(void) {
    btn_render_set_font_size(BTN_DEFAULT_FONT_SIZE);
}

static CFDictionaryRef make_attrs(CTFontRef font, CGColorRef color) {
    CFStringRef keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    CFTypeRef values[] = { font, color };
    return CFDictionaryCreate(NULL, (const void **)keys, (const void **)values, 2,
                               &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

/* ---- Gecachte Farben/Attribute (siehe Kommentar bei den g_*-Globalen oben) ---- */

static CGColorRef get_text_color(void) {
    if (!g_text_color) {
        g_text_color = create_cg_color(col_text());
    }
    return g_text_color;
}

static CGColorRef get_dim_color(void) {
    if (!g_dim_color) {
        g_dim_color = create_cg_color(col_dim());
    }
    return g_dim_color;
}

static CGColorRef get_accent_color(void) {
    if (!g_accent_color) {
        g_accent_color = create_cg_color(col_accent());
    }
    return g_accent_color;
}

static CGColorRef get_gutter_text_color(void) {
    if (!g_gutter_text_color) {
        g_gutter_text_color = create_cg_color(col_gutter_text());
    }
    return g_gutter_text_color;
}

static CGColorRef get_footer_text_color(void) {
    if (!g_footer_text_color) {
        g_footer_text_color = create_cg_color(col_footer_text());
    }
    return g_footer_text_color;
}

static CFDictionaryRef get_text_attrs(void) {
    if (!g_text_attrs) {
        g_text_attrs = make_attrs(get_font(), get_text_color());
    }
    return g_text_attrs;
}

static CFDictionaryRef get_dim_attrs(void) {
    if (!g_dim_attrs) {
        g_dim_attrs = make_attrs(get_font(), get_dim_color());
    }
    return g_dim_attrs;
}

static CFDictionaryRef get_accent_attrs(void) {
    if (!g_accent_attrs) {
        g_accent_attrs = make_attrs(get_font(), get_accent_color());
    }
    return g_accent_attrs;
}

static CFDictionaryRef get_gutter_attrs(void) {
    if (!g_gutter_attrs) {
        g_gutter_attrs = make_attrs(get_font(), get_gutter_text_color());
    }
    return g_gutter_attrs;
}

static CFDictionaryRef get_footer_attrs(void) {
    if (!g_footer_attrs) {
        g_footer_attrs = make_attrs(get_font(), get_footer_text_color());
    }
    return g_footer_attrs;
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

/* Dekodiert eine Row fuer CoreText in UTF-16 - Zeichen fuer Zeichen nach
 * derselben Regel wie Cursor, Spalten und Umbruch (btn_utf8_char_len()):
 * gueltige UTF-8-Sequenz -> ihr Codepoint, jedes andere Byte -> das
 * gleichwertige ISO-8859-1-Zeichen. Vorher wurde die ganze Row entweder als
 * UTF-8 oder (bei einem einzigen ungueltigen Byte) komplett als Latin-1
 * dekodiert - dann wurde auch ein korrektes "ae" auf derselben Zeile als zwei
 * Zeichen gezeichnet, waehrend der Cursor es als eines zaehlte. Tabs werden
 * gleich hier zu Leerzeichen bis zum naechsten Tabstopp expandiert (CoreText
 * hat eine eigene Tab-Logik, die nicht zu unserer Spaltenrechnung passt).
 * byte_to_u16[k] ist der UTF-16-Index des Zeichens, zu dem Byte k gehoert
 * (byte_to_u16[len] = Gesamtlaenge) - daraus werden die Farbbereiche der
 * Syntax-Tokens direkt abgelesen. out braucht Platz fuer
 * len * max(BTN_TAB_WIDTH, 2) Einheiten. */
static size_t decode_row_for_display(const unsigned char *raw, size_t len, UniChar *out, size_t *byte_to_u16) {
    size_t n = 0;
    size_t col = 0;
    size_t i = 0;
    while (i < len) {
        size_t clen = btn_utf8_char_len(raw + i, len - i);
        for (size_t k = 0; k < clen; k++) {
            byte_to_u16[i + k] = n;
        }
        unsigned char b = raw[i];
        if (b == '\t') {
            size_t stop = editor_tab_advance(col);
            while (col < stop) {
                out[n++] = ' ';
                col++;
            }
        } else {
            unsigned long cp;
            if (clen == 1) {
                cp = b;
            } else if (clen == 2) {
                cp = ((unsigned long)(b & 0x1F) << 6) | (raw[i + 1] & 0x3F);
            } else if (clen == 3) {
                cp = ((unsigned long)(b & 0x0F) << 12) | ((unsigned long)(raw[i + 1] & 0x3F) << 6) | (raw[i + 2] & 0x3F);
            } else {
                cp = ((unsigned long)(b & 0x07) << 18) | ((unsigned long)(raw[i + 1] & 0x3F) << 12) |
                     ((unsigned long)(raw[i + 2] & 0x3F) << 6) | (raw[i + 3] & 0x3F);
            }
            if (cp >= 0x10000) {
                cp -= 0x10000;
                out[n++] = (UniChar)(0xD800 + (cp >> 10));
                out[n++] = (UniChar)(0xDC00 + (cp & 0x3FF));
            } else {
                out[n++] = (UniChar)cp;
            }
            col++;
        }
        i += clen;
    }
    byte_to_u16[len] = n;
    return n;
}

/* ---- Wortumbruch-Layout ---- */

double btn_layout_text_width(CGRect bounds) {
    double w = bounds.size.width - GUTTER_WIDTH - LEFT_PADDING - BTN_SCROLLBAR_WIDTH;
    return w > 1.0 ? w : 1.0;
}

static long chars_per_row_for(double text_width) {
    long n = (long)(text_width / get_char_width());
    return n > 0 ? n : 1;
}

static void rows_push(BtnRow **rows, size_t *count, size_t *cap,
                       size_t start, size_t len, size_t logical_line, int is_continuation) {
    if (*count == *cap) {
        *cap = *cap ? btn_xmul(*cap, 2) : 64;
        *rows = btn_xrealloc(*rows, btn_xmul(*cap, sizeof(BtnRow)));
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
static BtnRow *layout_build(Editor *ed, long chars_per_row, size_t *out_row_count, size_t *out_word_count,
                            size_t *out_char_count) {
    size_t cap = 0, count = 0;
    /* Woerter und Zeichen im selben Durchlauf zaehlen (Woerter nach
     * derselben Regel wie editor_word_count(), Zeichen nach
     * btn_utf8_char_len() inkl. '\n') - spart der Statuszeile eigene
     * Vollscans pro Frame. Gezaehlt wird nur dort, wo i tatsaechlich
     * vorrueckt: nach einem erzwungenen Umbruch wird dasselbe Zeichen erneut
     * betrachtet. Zeichenweise statt byteweise Woerter zu zaehlen ergibt
     * dieselbe Zahl, weil kein Byte >= 0x80 als Wortzeichen gilt. */
    size_t words = 0;
    size_t chars = 0;
    int in_word = 0;
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
            in_word = 0;
            if (i < total_len) {
                chars++;
            }
            i++;
            continue;
        }

        char c = gb_char_at(&ed->buffer, i);
        /* Zeichenweise (btn_utf8_char_len()), nicht byteweise - ein
         * mehrbytiges Zeichen ist genau EINE Spalte, exakt wie
         * editor_visual_column_in_range() fuer Cursor/Selektion und
         * decode_row_for_display() fuers Zeichnen zaehlen. i steht dadurch
         * immer auf einem Zeichenanfang, ein Umbruch kann nie mitten in
         * einem Zeichen landen. */
        /* ASCII ist immer genau ein Zeichen - Schnellpfad, der Umbruch laeuft
         * bei jeder Inhaltsaenderung ueber das ganze Dokument. */
        size_t clen = ((unsigned char)c < 0x80) ? 1 : editor_char_len(ed, i, total_len);
        long new_col = (c == '\t') ? (long)editor_tab_advance((size_t)col) : col + 1;

        if (new_col > chars_per_row && i > row_start) {
            /* Umbruch hinter dem letzten Leerzeichen/Tab der Row, sonst
             * (ein Wort breiter als die Zeile) erzwungen vor diesem Zeichen. */
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
        int w = editor_is_word_char(c);
        if (w && !in_word) {
            words++;
        }
        in_word = w;
        col = new_col;
        chars++;
        i += clen;
    }

    *out_row_count = count;
    if (out_word_count) {
        *out_word_count = words;
    }
    if (out_char_count) {
        *out_char_count = chars;
    }
    return rows;
}

BtnRow *btn_layout_build(Editor *ed, double text_width, size_t *out_row_count) {
    return layout_build(ed, chars_per_row_for(text_width), out_row_count, NULL, NULL);
}

/* Ein-Eintrags-Cache fuer das Bildschirm-Layout des gerade gezeichneten
 * Dokuments. Vorher baute jede Taste das komplette Layout (voller Durchlauf
 * ueber den Puffer) zwei- bis dreimal: sync_scroll_to_cursor(), das Zeichnen
 * selbst, beim Ziehen mit der Maus zusaetzlich btn_hit_test(). Das Layout
 * haengt nur vom Inhalt (edit_seq - global eindeutig, siehe editor.h) und
 * von chars_per_row ab; Schriftgroesse/Fensterbreite stecken beide in
 * chars_per_row. */
static struct {
    int valid;
    size_t edit_seq;
    long chars_per_row;
    BtnRow *rows;
    size_t row_count;
    size_t word_count;
    size_t char_count;
} g_layout;

const BtnRow *btn_layout_get(Editor *ed, double text_width, size_t *out_row_count) {
    long chars_per_row = chars_per_row_for(text_width);
    if (!g_layout.valid || g_layout.edit_seq != ed->edit_seq || g_layout.chars_per_row != chars_per_row) {
        free(g_layout.rows);
        g_layout.rows = layout_build(ed, chars_per_row, &g_layout.row_count, &g_layout.word_count,
                                     &g_layout.char_count);
        g_layout.edit_seq = ed->edit_seq;
        g_layout.chars_per_row = chars_per_row;
        g_layout.valid = 1;
    }
    *out_row_count = g_layout.row_count;
    return g_layout.rows;
}

void btn_layout_free(BtnRow *rows) {
    free(rows);
}

/* Letzte Row mit start <= offset. Die Row-Starts sind streng monoton
 * steigend (jede Row beginnt hinter der vorherigen, siehe layout_build()),
 * deshalb binaere statt linearer Suche - wird pro Tastendruck mehrfach
 * aufgerufen, bei 100k Zeilen waren das sonst 100k Vergleiche pro Aufruf. */
size_t btn_layout_row_for_offset(const BtnRow *rows, size_t row_count, size_t offset) {
    if (row_count == 0) {
        return 0;
    }
    size_t lo = 0, hi = row_count;
    while (hi - lo > 1) {
        size_t mid = lo + (hi - lo) / 2;
        if (rows[mid].start <= offset) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

size_t btn_row_offset_for_column(Editor *ed, const BtnRow *rows, size_t row_count, size_t row, size_t col) {
    size_t row_end = rows[row].start + rows[row].len;
    size_t offset = editor_offset_for_column_in_range(ed, rows[row].start, rows[row].len, col);
    /* Beim Umbruch an einem Leerzeichen (Normalfall) steht der Cursor damit
     * direkt hinter dem letzten Wort; bei einem erzwungenen Umbruch mitten
     * in einem ueberlangen Wort ein Zeichen vor dem Row-Ende. */
    if (offset == row_end && rows[row].len > 0 && row + 1 < row_count && rows[row + 1].is_continuation) {
        offset = editor_utf8_seq_start(ed, row_end - 1);
    }
    return offset;
}

/* Index der ersten Row der logischen Zeile, zu der Row r gehoert. */
static size_t first_row_of_line(const BtnRow *rows, size_t r) {
    while (r > 0 && rows[r].is_continuation) {
        r--;
    }
    return r;
}

/* Beginn und Laenge (ohne '\n') der logischen Zeile, zu der Row r gehoert -
 * aus den Rows abgeleitet statt per editor_line_bounds(), das bei JEDEM
 * Aufruf ab Byte 0 zaehlt (pro sichtbarer Zeile ein Vollscan des Dokuments,
 * bei 9 MB rund 0,8 s pro Frame). Die Rows einer Zeile liegen lueckenlos
 * hintereinander, die letzte endet genau vor dem '\n'. */
static void line_bounds_from_rows(const BtnRow *rows, size_t row_count, size_t r,
                                  size_t *out_start, size_t *out_len) {
    size_t first = first_row_of_line(rows, r);
    size_t last = r;
    while (last + 1 < row_count && rows[last + 1].is_continuation) {
        last++;
    }
    *out_start = rows[first].start;
    *out_len = rows[last].start + rows[last].len - rows[first].start;
}

/* Index der ersten Row der logischen Zeile line (binaere Suche - logical_line
 * ist ueber die Rows monoton steigend, jede Zeile hat mindestens eine Row). */
static size_t row_of_line_start(const BtnRow *rows, size_t row_count, size_t line) {
    size_t lo = 0, hi = row_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (rows[mid].logical_line < line) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < row_count ? lo : row_count - 1;
}

/* ---- Zeichnen ---- */

static void draw_gutter(CGContextRef ctx, CGRect bounds, const BtnRow *rows, size_t row_count,
                         long scroll_row) {
    set_fill(ctx, col_gutter_bg());
    CGContextFillRect(ctx, CGRectMake(0, BTN_FOOTER_HEIGHT, GUTTER_WIDTH, bounds.size.height - BTN_FOOTER_HEIGHT));

    set_stroke(ctx, col_divider());
    CGContextSetLineWidth(ctx, 1.0);
    CGPoint divider[2] = { { GUTTER_WIDTH, BTN_FOOTER_HEIGHT }, { GUTTER_WIDTH, bounds.size.height } };
    CGContextStrokeLineSegments(ctx, divider, 2);

    CFDictionaryRef attrs = get_gutter_attrs();

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
    /* attrs ist gecacht (siehe get_gutter_attrs()) - keine Freigabe hier. */
}

/* Uebersetzte Formate der Statuszeile - render.c kennt strings.h bewusst
 * nicht, main.c reicht sie nach btn_strings_set_language() einmal herein
 * (wie die Suchleisten-Beschriftungen). Englisch, bis das passiert. */
static const char *g_footer_pos_fmt = "Line %zu, Column %zu";
static const char *g_footer_stats_fmt = "%zu lines | %zu words | %zu characters | UTF-8";

/* 1, wenn fmt genau n Mal "%zu" und sonst nur "%%" als Konversion enthaelt -
 * die Formate gehen an snprintf() mit genau so vielen size_t-Argumenten. */
int btn_footer_format_ok(const char *fmt, int n) {
    if (!fmt) {
        return 0;
    }
    int found = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            continue;
        }
        if (p[1] == '%') {
            p++;
        } else if (p[1] == 'z' && p[2] == 'u') {
            found++;
            p += 2;
        } else {
            return 0;
        }
    }
    return found == n;
}

/* Zeilenenden des aktiven Dokuments ("CRLF", "CRLF (gemischt)") - main.c
 * setzt das vor jedem Zeichnen, render.c kennt keine Dokumente. */
static char g_footer_eol[64] = "LF";

void btn_render_set_footer_eol(const char *label) {
    snprintf(g_footer_eol, sizeof(g_footer_eol), "%s", label ? label : "");
}

void btn_render_set_footer_formats(const char *pos_fmt, const char *stats_fmt) {
    /* Ein fehlerhaft uebersetztes Format fiele sonst erst als Absturz in
     * snprintf() auf - dann bleibt es beim bisherigen. */
    if (btn_footer_format_ok(pos_fmt, 2)) {
        g_footer_pos_fmt = pos_fmt;
    }
    if (btn_footer_format_ok(stats_fmt, 3)) {
        g_footer_stats_fmt = stats_fmt;
    }
}

static void draw_footer(CGContextRef ctx, CGRect bounds, Editor *ed, const BtnRow *rows, size_t row_count) {
    set_fill(ctx, col_footer_bg());
    CGContextFillRect(ctx, CGRectMake(0, 0, bounds.size.width, BTN_FOOTER_HEIGHT));

    set_stroke(ctx, col_divider());
    CGContextSetLineWidth(ctx, 1.0);
    CGPoint divider[2] = { { 0, BTN_FOOTER_HEIGHT }, { bounds.size.width, BTN_FOOTER_HEIGHT } };
    CGContextStrokeLineSegments(ctx, divider, 2);

    CFDictionaryRef attrs = get_footer_attrs();

    /* Alles aus dem ohnehin gebauten Layout statt vier Vollscans pro Frame
     * (offset_to_line, visual_column mit zwei weiteren, line_count,
     * word_count). Die Spalte zaehlt ab dem Anfang der LOGISCHEN Zeile (erste
     * Row der Zeile), nicht ab der umgebrochenen Row - wie vorher. */
    size_t cur_row = btn_layout_row_for_offset(rows, row_count, ed->cursor);
    size_t cur_line = rows[cur_row].logical_line;
    size_t col = editor_visual_column_in_range(ed, rows[first_row_of_line(rows, cur_row)].start, ed->cursor);
    size_t line_count = rows[row_count - 1].logical_line + 1;

    char left[64];
    snprintf(left, sizeof(left), g_footer_pos_fmt, cur_line + 1, col + 1);

    char right[256];
    int n = snprintf(right, sizeof(right), g_footer_stats_fmt, line_count, g_layout.word_count, g_layout.char_count);
    if (n > 0 && (size_t)n < sizeof(right) && g_footer_eol[0] != '\0') {
        snprintf(right + n, sizeof(right) - (size_t)n, " | %s", g_footer_eol);
    }

    double text_y = (BTN_FOOTER_HEIGHT - g_font_size) / 2.0 + 3.0;

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
    /* attrs ist gecacht (siehe get_footer_attrs()) - keine Freigabe hier. */
}

/* Zeichnet str linksbuendig bei (x,y) mit den gegebenen Attributen und gibt
 * die gemessene Textbreite zurueck. Uebernimmt str (gibt es frei); NULL
 * (fehlgeschlagene Konvertierung) zeichnet nichts. */
static double draw_cfstring_at(CGContextRef ctx, CFStringRef str, double x, double y, CFDictionaryRef attrs) {
    if (!str) {
        return 0.0;
    }
    CFAttributedStringRef attrStr = CFAttributedStringCreate(NULL, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attrStr);
    double width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
    CGContextSetTextPosition(ctx, x, y);
    CTLineDraw(line, ctx);
    CFRelease(line);
    CFRelease(attrStr);
    CFRelease(str);
    return width;
}

/* draw_cfstring_at() fuer einen UTF-8-C-String - fuer die festen Texte der
 * Oberflaeche (Tab-Label, Schliessen-Kreuz, "+"-Knopf, Beschriftungen).
 * Ungueltiges UTF-8 wird als Latin-1 gezeichnet (Tab-Labels aus Dateinamen
 * fremder Volumes); gueltige Labels kuerzt utf8_safe_cut() nur an
 * Zeichengrenzen. Nutzertext (Dokument, Suchfelder) geht dagegen
 * ueber decode_row_for_display(). */
static double draw_text_at(CGContextRef ctx, const char *text, double x, double y, CFDictionaryRef attrs) {
    CFStringRef str = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
    if (!str) {
        /* Dateiname auf SMB/NFS/FAT ohne gueltiges UTF-8: als Latin-1
         * zeigen (wie der Fenstertitel in shim.m) statt gar nicht. */
        str = CFStringCreateWithCString(NULL, text, kCFStringEncodingISOLatin1);
    }
    return draw_cfstring_at(ctx, str, x, y, attrs);
}

/* Wie draw_text_at(), aber hoechstens max_width breit - laengerer Text wird
 * am Ende mit "..." gekuerzt statt ueber den Fensterrand zu laufen. */
static void draw_text_truncated_at(CGContextRef ctx, const char *text, double x, double y, double max_width,
                                   CFDictionaryRef attrs) {
    if (max_width <= 0.0) {
        return;
    }
    CFStringRef str = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
    if (!str) {
        return;
    }
    CFAttributedStringRef attrStr = CFAttributedStringCreate(NULL, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attrStr);
    CFStringRef ellipsis = CFStringCreateWithCString(NULL, "\xE2\x80\xA6", kCFStringEncodingUTF8); /* U+2026 */
    CFAttributedStringRef tokenStr = CFAttributedStringCreate(NULL, ellipsis, attrs);
    CTLineRef token = CTLineCreateWithAttributedString(tokenStr);
    /* NULL, wenn nicht einmal das "..." passt - dann gar nichts zeichnen. */
    CTLineRef truncated = CTLineCreateTruncatedLine(line, max_width, kCTLineTruncationEnd, token);
    if (truncated) {
        CGContextSetTextPosition(ctx, x, y);
        CTLineDraw(truncated, ctx);
        CFRelease(truncated);
    }
    CFRelease(token);
    CFRelease(tokenStr);
    CFRelease(ellipsis);
    CFRelease(line);
    CFRelease(attrStr);
    CFRelease(str);
}

/* Rueckt pos zurueck, bis es nicht mehr auf ein UTF-8-Fortsetzungsbyte
 * (10xxxxxx) zeigt - dieselbe Idee wie editor_utf8_seq_start() in editor.c,
 * aber fuer einen rohen C-String statt einen GapBuffer (render.c bekommt
 * hier nur main.c's fertig formatierte Tab-Labels, keinen Editor). Genutzt
 * beim Kuerzen von Labels, damit ein Schnitt nie mitten in einem
 * mehrbytigen Zeichen landet. */
static size_t utf8_safe_cut(const char *s, size_t pos) {
    size_t steps = 0;
    while (steps < 3 && pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) {
        pos--;
        steps++;
    }
    return pos;
}

/* Byte-Laenge des Praefixes von s, das die ersten max_chars Zeichen
 * (Codepoints, nicht Bytes) umfasst - kuerzer, falls s weniger Zeichen hat.
 * Zaehlt wie editor_visual_column_in_range() nur Lead-Bytes (alles ausser
 * 10xxxxxx), landet also per Konstruktion immer auf einer Zeichengrenze.
 * Fuer die Tab-Beschriftung: ein Dateiname mit Umlauten braucht in der
 * Monospace-Schrift pro ZEICHEN eine Zelle, nicht pro Byte - mit strlen()
 * als Mass wuerde "Muellerstrasse" mit echten Umlauten frueher als noetig
 * mit "..." gekuerzt. */
static size_t utf8_prefix_bytes(const char *s, size_t max_chars) {
    size_t i = 0;
    size_t chars = 0;
    while (s[i] != '\0') {
        if (((unsigned char)s[i] & 0xC0) != 0x80) {
            if (chars == max_chars) {
                break;
            }
            chars++;
        }
        i++;
    }
    return i;
}

/* Volle Tab-Breite, solange alle Tabs + der "+"-Knopf in window_width
 * passen; sonst wird jeder Tab gleichmaessig schmaler (nie unter 40pt,
 * sonst waere ein Tab nicht mehr bedienbar) - main.c's Hit-Testing ruft
 * dieselbe Funktion auf, damit Zeichnen und Klick-Trefferpruefung nie
 * auseinanderlaufen, egal wie viele Tabs gerade offen sind. */
double btn_tab_width_for(int count, double window_width) {
    if (count <= 0) {
        return BTN_TAB_ITEM_WIDTH;
    }
    double total_needed = (double)count * BTN_TAB_ITEM_WIDTH + BTN_TAB_NEW_WIDTH;
    if (total_needed <= window_width) {
        return BTN_TAB_ITEM_WIDTH;
    }
    double shrunk = (window_width - BTN_TAB_NEW_WIDTH) / (double)count;
    return shrunk < 40.0 ? 40.0 : shrunk;
}

void btn_render_tab_bar(CGContextRef ctx, CGRect bounds, const char *const *labels, int count, int active) {
    double bar_top = bounds.size.height - BTN_TAB_BAR_HEIGHT;
    double text_y = bar_top + (BTN_TAB_BAR_HEIGHT - g_font_size) / 2.0 + 3.0;
    double tab_width = btn_tab_width_for(count, bounds.size.width);

    set_fill(ctx, col_tab_bar_bg());
    CGContextFillRect(ctx, CGRectMake(0, bar_top, bounds.size.width, BTN_TAB_BAR_HEIGHT));

    CFDictionaryRef attrs = get_text_attrs();
    CFDictionaryRef dimAttrs = get_dim_attrs();

    for (int i = 0; i < count; i++) {
        double tab_x = (double)i * tab_width;
        int is_active = (i == active);

        set_fill(ctx, is_active ? col_tab_active_bg() : col_tab_bar_bg());
        CGContextFillRect(ctx, CGRectMake(tab_x, bar_top, tab_width, BTN_TAB_BAR_HEIGHT));

        set_stroke(ctx, col_tab_divider());
        CGContextSetLineWidth(ctx, 1.0);
        CGPoint divider[2] = { { tab_x, bar_top }, { tab_x, bar_top + BTN_TAB_BAR_HEIGHT } };
        CGContextStrokeLineSegments(ctx, divider, 2);

        /* Label ggf. kuerzen ("...") bis es in die verfuegbare Breite passt.
         * Monospace-Schrift -> Breite pro ZEICHEN ist konstant
         * (get_char_width()), eine einzige Kapazitaetsrechnung reicht statt
         * iterativem Neumessen. Gemessen wird in Codepoints (siehe
         * utf8_prefix_bytes()), nicht in Bytes - sonst wuerde ein Dateiname
         * mit Umlauten frueher als noetig gekuerzt. utf8_prefix_bytes()
         * schneidet per Konstruktion nur an Zeichengrenzen; utf8_safe_cut()
         * sichert das zusaetzlich fuer den reinen Byte-Deckel von buf ab
         * (sonst wuerde draw_text_at() ungueltiges UTF-8 bekommen). */
        double avail = tab_width - 2.0 * LEFT_PADDING - BTN_TAB_CLOSE_WIDTH;
        size_t max_chars = (size_t)(avail / get_char_width());
        if (max_chars < 1) {
            max_chars = 1;
        }
        char buf[256];
        size_t label_len = utf8_prefix_bytes(labels[i], max_chars);
        int truncated = labels[i][label_len] != '\0';
        if (truncated) {
            label_len = utf8_prefix_bytes(labels[i], max_chars > 3 ? max_chars - 3 : max_chars);
        }
        if (label_len >= sizeof(buf) - 4) {
            label_len = utf8_safe_cut(labels[i], sizeof(buf) - 4);
        }
        memcpy(buf, labels[i], label_len);
        buf[label_len] = '\0';
        if (truncated) {
            strcat(buf, "...");
        }
        draw_text_at(ctx, buf, tab_x + LEFT_PADDING, text_y, attrs);

        double close_x = tab_x + tab_width - BTN_TAB_CLOSE_WIDTH / 2.0 - 4.0;
        draw_text_at(ctx, "×", close_x, text_y, dimAttrs);
    }

    double new_x = (double)count * tab_width;
    draw_text_at(ctx, "+", new_x + (BTN_TAB_NEW_WIDTH - get_char_width()) / 2.0, text_y, dimAttrs);

    set_stroke(ctx, col_tab_divider());
    CGContextSetLineWidth(ctx, 1.0);
    CGPoint bottom_divider[2] = { { 0, bar_top }, { bounds.size.width, bar_top } };
    CGContextStrokeLineSegments(ctx, bottom_divider, 2);
    /* attrs/dimAttrs sind gecacht (siehe get_text_attrs()/get_dim_attrs()) -
     * keine Freigabe hier. */
}

/* x-Positionen der Suchleiste - gemeinsam fuer Zeichnen, Mausklick-Tests
 * in main.c (dieselben Konstanten) und das Cursor-Rechteck fuer
 * Eingabemethoden (btn_render_find_caret_rect()). */
typedef struct {
    double search_label_x, search_field_x, regex_x, case_x, word_x;
    double replace_label_x, replace_field_x, replace_all_x, status_x;
} FindBarGeometry;

static FindBarGeometry find_bar_geometry(void) {
    FindBarGeometry g;
    g.search_label_x = BTN_FIND_BAR_PADDING;
    g.search_field_x = g.search_label_x + BTN_FIND_LABEL_WIDTH;
    g.regex_x = g.search_field_x + BTN_FIND_FIELD_WIDTH + BTN_FIND_BAR_PADDING;
    g.case_x = g.regex_x + BTN_FIND_REGEX_WIDTH + BTN_FIND_BAR_PADDING;
    g.word_x = g.case_x + BTN_FIND_REGEX_WIDTH + BTN_FIND_BAR_PADDING;
    g.replace_label_x = g.word_x + BTN_FIND_REGEX_WIDTH + BTN_FIND_BAR_PADDING * 2.0;
    g.replace_field_x = g.replace_label_x + BTN_FIND_LABEL_WIDTH;
    g.replace_all_x = g.replace_field_x + BTN_FIND_FIELD_WIDTH + BTN_FIND_BAR_PADDING;
    g.status_x = g.replace_all_x + BTN_FIND_REPLACE_ALL_WIDTH + BTN_FIND_BAR_PADDING * 2.0;
    return g;
}

/* ---- Vorlaeufiger Text einer Eingabemethode (siehe textinput.h) ----
 * Steht nicht im Puffer; main.c reicht ihn vor jedem Zeichnen herein und
 * er wird als Overlay am Cursor gezeichnet: Hintergrund, Text,
 * Unterstreichung, Cursor darin. Das Layout dahinter verschiebt sich nicht
 * (wie in Terminal-Programmen) - folgender Text ist so lange verdeckt. */
static struct {
    char *text;
    size_t len;
    size_t caret;
    int target; /* BTN_MARKED_* */
} g_marked;

void btn_render_set_marked_text(const char *utf8, size_t len, size_t caret, int target) {
    if (len == 0 || !utf8) {
        free(g_marked.text);
        g_marked.text = NULL;
        g_marked.len = 0;
        g_marked.target = BTN_MARKED_NONE;
        return;
    }
    if (len != g_marked.len || !g_marked.text || memcmp(g_marked.text, utf8, len) != 0) {
        char *copy = malloc(len);
        if (!copy) {
            return;
        }
        memcpy(copy, utf8, len);
        free(g_marked.text);
        g_marked.text = copy;
        g_marked.len = len;
    }
    g_marked.caret = caret <= len ? caret : len;
    g_marked.target = target;
}

/* Zeile fuer UTF-8-Text (Zeichenregel wie im Dokument); *map (falls nicht
 * NULL) bildet Bytes auf UTF-16-Indizes ab. NULL bei Speichermangel. */
static CTLineRef make_line(const char *utf8, size_t len, CFDictionaryRef attrs, size_t **map_out) {
    UniChar *u16 = malloc((len ? len : 1) * (BTN_TAB_WIDTH > 2 ? BTN_TAB_WIDTH : 2) * sizeof(UniChar));
    size_t *map = malloc((len + 1) * sizeof(size_t));
    if (!u16 || !map) {
        free(u16);
        free(map);
        return NULL;
    }
    size_t n = decode_row_for_display((const unsigned char *)utf8, len, u16, map);
    CFStringRef str = CFStringCreateWithCharacters(NULL, u16, (CFIndex)n);
    free(u16);
    CFAttributedStringRef as = CFAttributedStringCreate(NULL, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(as);
    CFRelease(as);
    CFRelease(str);
    if (map_out) {
        *map_out = map;
    } else {
        free(map);
    }
    return line;
}

double btn_render_text_width(const char *utf8, size_t len) {
    if (len == 0) {
        return 0.0;
    }
    CTLineRef line = make_line(utf8, len, get_text_attrs(), NULL);
    if (!line) {
        return 0.0;
    }
    double w = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
    CFRelease(line);
    return w;
}

/* Breite und Cursor aus der tatsaechlich gesetzten Zeile, nicht Zeichen mal
 * Spaltenbreite: chinesische/japanische Glyphen sind breiter als eine
 * Menlo-Spalte, Box, Unterstreichung und Cursor lagen sonst daneben. */
static void draw_marked_overlay(CGContextRef ctx, double x, double box_y, double box_h, double text_y,
                                CFDictionaryRef attrs, BtnColor bg) {
    size_t *map = NULL;
    CTLineRef line = make_line(g_marked.text, g_marked.len, attrs, &map);
    if (!line) {
        return;
    }
    double width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
    double caret_x = x + CTLineGetOffsetForStringIndex(line, (CFIndex)map[g_marked.caret], NULL);
    set_fill(ctx, bg);
    CGContextFillRect(ctx, CGRectMake(x, box_y, width + 1.0, box_h));
    CGContextSetTextPosition(ctx, x, text_y);
    CTLineDraw(line, ctx);
    set_fill(ctx, col_cursor());
    CGContextFillRect(ctx, CGRectMake(x, box_y + 1.0, width, 1.0));          /* Unterstreichung */
    CGContextFillRect(ctx, CGRectMake(caret_x, box_y + 2.0, 1.4, box_h - 4.0)); /* Cursor darin */
    CFRelease(line);
    free(map);
}

/* Zeichnet den Inhalt eines Suchen/Ersetzen-Feldes samt Selektions-
 * Hervorhebung und (falls focused und ohne Selektion) Cursor - dieselbe
 * Zeichen-Regel wie beim Hauptdokument, aber ohne Wortumbruch, weil diese
 * Felder nie ein '\n' enthalten (siehe editor_set_single_line();
 * BTN_MOVE_DOC_START/END sind dort bereits genau Pos1/Ende). Tabs rechnen
 * Spalten- und Zeichenlogik beide mit Tabstopps ab Spalte 0. */
static void draw_find_field(CGContextRef ctx, Editor *ed, double field_x, double text_y, double bar_top,
                             CFDictionaryRef attrs, double char_width, int focused, int marked_target) {
    int has_sel = editor_has_selection(ed);
    /* Byte-Offsets (Cursor/Selektion) in Zeichenspalten umrechnen - dieselbe
     * Logik wie beim Hauptdokument (editor_visual_column_in_range zaehlt
     * Codepoints, nicht Bytes), sonst wandert der Cursor bei jedem Umlaut
     * im Suchfeld eine Spalte zu weit nach rechts. Das Feld ist eine
     * einzige Zeile ab Spalte 0, range_start=0 reicht. */
    if (has_sel) {
        size_t sel_start_col = editor_visual_column_in_range(ed, 0, editor_selection_start(ed));
        size_t sel_end_col = editor_visual_column_in_range(ed, 0, editor_selection_end(ed));
        double sx = field_x + (double)sel_start_col * char_width;
        double sw = (double)(sel_end_col - sel_start_col) * char_width;
        if (sw < 2.0) {
            sw = 2.0;
        }
        set_fill(ctx, col_field_selection());
        CGContextFillRect(ctx, CGRectMake(sx, bar_top + 4.0, sw, BTN_FIND_BAR_HEIGHT - 8.0));
    }

    /* Wie eine Dokument-Row dekodieren, nicht als UTF-8-C-String: nach
     * Cmd+F mit einer Selektion aus einer Latin-1-Datei (oder mit einem
     * NUL-Byte darin) stand hier sonst gar nichts bzw. nur der Text bis zum
     * NUL, waehrend Cursor und Selektion nach der Zeichen-Regel weiterliefen. */
    size_t len;
    char *text = editor_copy_all(ed, &len);
    if (text && len > 0) {
        UniChar *u16 = malloc(len * (BTN_TAB_WIDTH > 2 ? BTN_TAB_WIDTH : 2) * sizeof(UniChar));
        size_t *byte_to_u16 = malloc((len + 1) * sizeof(size_t));
        if (u16 && byte_to_u16) {
            size_t n = decode_row_for_display((const unsigned char *)text, len, u16, byte_to_u16);
            draw_cfstring_at(ctx, CFStringCreateWithCharacters(NULL, u16, (CFIndex)n), field_x, text_y, attrs);
        }
        free(u16);
        free(byte_to_u16);
    }
    free(text);

    if (focused && !has_sel) {
        size_t cursor_col = editor_visual_column_in_range(ed, 0, ed->cursor);
        double cx = field_x + (double)cursor_col * char_width;
        if (g_marked.target == marked_target) {
            draw_marked_overlay(ctx, cx, bar_top + 4.0, BTN_FIND_BAR_HEIGHT - 8.0, text_y, attrs, col_find_bar_bg());
        } else {
            set_fill(ctx, col_cursor());
            CGContextFillRect(ctx, CGRectMake(cx, bar_top + 6.0, 1.4, BTN_FIND_BAR_HEIGHT - 12.0));
        }
    }
}

/* Zeichnet einen der drei kurzen Umschalter-Knoepfe (".*"/"Aa"/"\b") an
 * derselben Position, die main.c beim Mausklick testet (siehe
 * handle_find_bar_click()) - gemeinsamer Helfer statt dreifacher
 * Kopie, da alle drei bis auf Position/Beschriftung/Zustand identisch
 * aussehen. */
static void draw_toggle_button(CGContextRef ctx, double x, double bar_top, double text_y,
                                const char *label, int active,
                                CFDictionaryRef dimAttrs, CFDictionaryRef accentAttrs) {
    set_fill(ctx, col_toggle_bg(active));
    CGContextFillRect(ctx, CGRectMake(x, bar_top + 4.0, BTN_FIND_REGEX_WIDTH, BTN_FIND_BAR_HEIGHT - 8.0));
    draw_text_at(ctx, label, x + 3.0, text_y, active ? accentAttrs : dimAttrs);
}

void btn_render_find_bar(CGContextRef ctx, CGRect bounds, const char *search_label, Editor *search_ed,
                          const char *replace_label, Editor *replace_ed,
                          const char *replace_all_label,
                          int regex_mode, int case_sensitive, int whole_word,
                          int focus_field, const char *status) {
    double bar_top = bounds.size.height - BTN_TAB_BAR_HEIGHT - BTN_FIND_BAR_HEIGHT;
    double text_y = bar_top + (BTN_FIND_BAR_HEIGHT - g_font_size) / 2.0 + 3.0;

    set_fill(ctx, col_find_bar_bg());
    CGContextFillRect(ctx, CGRectMake(0, bar_top, bounds.size.width, BTN_FIND_BAR_HEIGHT));

    double char_width = get_char_width();
    CFDictionaryRef attrs = get_text_attrs();
    CFDictionaryRef dimAttrs = get_dim_attrs();
    CFDictionaryRef accentAttrs = get_accent_attrs();

    FindBarGeometry g = find_bar_geometry();
    double search_label_x = g.search_label_x, search_field_x = g.search_field_x;
    double regex_x = g.regex_x, case_x = g.case_x, word_x = g.word_x;
    double replace_label_x = g.replace_label_x, replace_field_x = g.replace_field_x;
    double replace_all_x = g.replace_all_x, status_x = g.status_x;

    draw_text_at(ctx, search_label, search_label_x, text_y, dimAttrs);
    draw_find_field(ctx, search_ed, search_field_x, text_y, bar_top, attrs, char_width, focus_field == 1,
                    BTN_MARKED_SEARCH);

    /* Drei Umschalter fuer die Suche - main.c testet dieselben Positionen
     * (regex_x/case_x/word_x, je Breite BTN_FIND_REGEX_WIDTH) beim
     * Mausklick, siehe handle_find_bar_click(). */
    draw_toggle_button(ctx, regex_x, bar_top, text_y, ".*", regex_mode, dimAttrs, accentAttrs);
    draw_toggle_button(ctx, case_x, bar_top, text_y, "Aa", case_sensitive, dimAttrs, accentAttrs);
    draw_toggle_button(ctx, word_x, bar_top, text_y, "\\b", whole_word, dimAttrs, accentAttrs);

    draw_text_at(ctx, replace_label, replace_label_x, text_y, dimAttrs);
    draw_find_field(ctx, replace_ed, replace_field_x, text_y, bar_top, attrs, char_width, focus_field == 2,
                    BTN_MARKED_REPLACE);

    /* "Alle ersetzen"-Knopf - main.c testet dieselbe Position (replace_all_x,
     * Breite BTN_FIND_REPLACE_ALL_WIDTH) beim Mausklick, siehe
     * handle_find_bar_click(). Bisher nur per Cmd+Return im Ersetzen-Feld
     * erreichbar - dieser Knopf macht die Aktion zusaetzlich sichtbar/
     * klickbar. */
    set_fill(ctx, col_button_bg());
    CGContextFillRect(ctx, CGRectMake(replace_all_x, bar_top + 4.0, BTN_FIND_REPLACE_ALL_WIDTH, BTN_FIND_BAR_HEIGHT - 8.0));
    set_stroke(ctx, col_divider());
    CGContextSetLineWidth(ctx, 1.0);
    CGContextStrokeRect(ctx, CGRectMake(replace_all_x, bar_top + 4.0, BTN_FIND_REPLACE_ALL_WIDTH, BTN_FIND_BAR_HEIGHT - 8.0));
    draw_text_at(ctx, replace_all_label, replace_all_x + 6.0, text_y, attrs);

    if (status && status[0] != '\0') {
        /* Rest der Fensterbreite - bei der Mindestbreite von 1080pt gut
         * 230pt, zu wenig fuer manche Uebersetzungen oder "Treffer 100000
         * von 100000". */
        draw_text_truncated_at(ctx, status, status_x, text_y, bounds.size.width - status_x - BTN_FIND_BAR_PADDING,
                               dimAttrs);
    }

    set_stroke(ctx, col_divider());
    CGContextSetLineWidth(ctx, 1.0);
    CGPoint bottom_divider[2] = { { 0, bar_top }, { bounds.size.width, bar_top } };
    CGContextStrokeLineSegments(ctx, bottom_divider, 2);
    /* attrs/dimAttrs/accentAttrs sind gecacht (siehe get_text_attrs()/
     * get_dim_attrs()/get_accent_attrs()) - keine Freigabe hier. */
}

/* Kommentar-Zustand VOR jeder logischen Zeile (states[i] = Zustand vor
 * Zeile i), inkrementell gepflegt. Vorher wurde bei JEDEM Frame jede Zeile
 * oberhalb des Sichtfensters neu tokenisiert (bei 9 MB rund 0,25 s pro
 * Tastendruck). Jetzt: nach einer Aenderung bleiben alle Zustaende fuer
 * Zeilen gueltig, die vor dem kleinsten geaenderten Offset beginnen (die
 * Bytes davor sind unveraendert, siehe editor_changed_from()); ab dort wird
 * nur so weit neu tokenisiert, wie das Sichtfenster es braucht. Der Zustand
 * haengt nur von den vorherigen Zeilen ab, daher ist das exakt. */
static struct {
    size_t edit_seq;
    const BtnLangSpec *lang;
    int *states;
    size_t known;
    size_t cap;
} g_cstate;

static int cstate_reserve(size_t n) {
    if (n <= g_cstate.cap) {
        return 1;
    }
    size_t cap = g_cstate.cap ? g_cstate.cap : 256;
    while (cap < n) {
        cap *= 2;
    }
    int *grown = realloc(g_cstate.states, cap * sizeof(int));
    if (!grown) {
        return 0;
    }
    g_cstate.states = grown;
    g_cstate.cap = cap;
    return 1;
}

static int comment_state_before_line(Editor *ed, const BtnLangSpec *lang, const BtnRow *rows,
                                     size_t row_count, size_t target_line) {
    size_t known = 0;
    if (g_cstate.known > 0 && g_cstate.lang == lang) {
        size_t change_at = editor_changed_from(ed, g_cstate.edit_seq);
        if (change_at == (size_t)-1) {
            known = g_cstate.known;
        } else {
            size_t len = editor_length(ed);
            if (change_at > len) {
                change_at = len;
            }
            /* Zeilen, die bei oder vor change_at beginnen, haengen nur von
             * unveraenderten Bytes ab: das sind die Zeilen 0..(Anzahl '\n'
             * vor change_at) - und diese Anzahl ist die logische Zeile der
             * Row, in der change_at liegt. */
            size_t unchanged_lines = rows[btn_layout_row_for_offset(rows, row_count, change_at)].logical_line + 1;
            known = g_cstate.known < unchanged_lines ? g_cstate.known : unchanged_lines;
        }
    }
    g_cstate.edit_seq = ed->edit_seq;
    g_cstate.lang = lang;
    editor_rebase_changes(ed);

    if (known == 0) {
        if (!cstate_reserve(1)) {
            g_cstate.known = 0;
            return 0;
        }
        g_cstate.states[0] = 0;
        known = 1;
    }
    g_cstate.known = known;
    if (target_line < known) {
        return g_cstate.states[target_line];
    }

    size_t line = known - 1;
    int state = g_cstate.states[line];
    size_t line_start = rows[row_of_line_start(rows, row_count, line)].start;
    size_t total_len = editor_length(ed);
    for (size_t i = line_start; i < total_len && line < target_line; i++) {
        if (gb_char_at(&ed->buffer, i) != '\n') {
            continue;
        }
        size_t line_len = i - line_start;
        char *text = gb_copy_range(&ed->buffer, line_start, line_len);
        int ends;
        btn_highlight_tokenize(text, line_len, lang, state, &ends, NULL, 0);
        free(text);
        state = ends;
        line++;
        line_start = i + 1;
        /* Kein Speicher fuers Merken: trotzdem weiterrechnen, damit der
         * zurueckgegebene Zustand stimmt - nur eben ohne Cache-Gewinn. */
        if (g_cstate.known == line && cstate_reserve(line + 1)) {
            g_cstate.states[line] = state;
            g_cstate.known = line + 1;
        }
    }
    return state;
}

void btn_compute_line_comment_states(Editor *ed, const BtnLangSpec *lang, int *out_states) {
    size_t line_count = editor_line_count(ed);
    if (line_count == 0) {
        return;
    }
    out_states[0] = 0;
    if (!lang) {
        for (size_t i = 1; i < line_count; i++) {
            out_states[i] = 0;
        }
        return;
    }

    int state = 0;
    size_t total_len = editor_length(ed);
    size_t li = 0;
    size_t line_start = 0;

    for (size_t i = 0; i <= total_len && li + 1 < line_count; i++) {
        if (i == total_len || gb_char_at(&ed->buffer, i) == '\n') {
            size_t line_len = i - line_start;
            char *text = gb_copy_range(&ed->buffer, line_start, line_len);
            int ends;
            btn_highlight_tokenize(text, line_len, lang, state, &ends, NULL, 0);
            free(text);
            state = ends;
            li++;
            line_start = i + 1;
            out_states[li] = state;
        }
    }
}

/* Tokenisiert (falls lang != NULL, mit Zeilen-Cache ueber cached_line) und
 * zeichnet genau eine Row als CTLine bei (x, top_y) - der Kern von
 * btn_render_frame()s Zeilenschleife, ausgelagert, damit btn_render_print_page()
 * (Druck) dieselbe Hervorhebungs-/Zeichenlogik nutzt statt sie zu duplizieren.
 * Kennt bewusst keine Selektion/Cursor/Klammer-Hervorhebung - das bleibt
 * Sache der jeweiligen Aufrufer (Bildschirm hat sie, Druck nicht). */
static void draw_row_line(CGContextRef ctx, Editor *ed, const BtnLangSpec *lang,
                           const BtnRow *rows, size_t row_count, size_t r, double x, double top_y,
                           CFDictionaryRef attrs, size_t *cached_line, size_t *cached_line_start,
                           int *comment_state, BtnToken *tokens, size_t *token_count) {
    size_t row_start = rows[r].start;
    size_t row_len = rows[r].len;
    size_t row_end = row_start + row_len;

    if (lang) {
        size_t ll = rows[r].logical_line;
        if (ll != *cached_line) {
            size_t ls, llen;
            line_bounds_from_rows(rows, row_count, r, &ls, &llen);
            char *text = gb_copy_range(&ed->buffer, ls, llen);
            int ends;
            *token_count = btn_highlight_tokenize(text, llen, lang, *comment_state, &ends,
                                                   tokens, BTN_MAX_TOKENS_PER_LINE);
            if (*token_count > BTN_MAX_TOKENS_PER_LINE) {
                *token_count = BTN_MAX_TOKENS_PER_LINE;
            }
            free(text);
            *comment_state = ends;
            *cached_line = ll;
            *cached_line_start = ls;
        }
    }

    if (row_len == 0) {
        return;
    }

    char *raw = gb_copy_range(&ed->buffer, row_start, row_len);
    size_t u16_cap = row_len * (BTN_TAB_WIDTH > 2 ? BTN_TAB_WIDTH : 2);
    UniChar *u16 = malloc(u16_cap * sizeof(UniChar));
    size_t *byte_to_u16 = malloc((row_len + 1) * sizeof(size_t));
    if (!raw || !u16 || !byte_to_u16) {
        free(raw);
        free(u16);
        free(byte_to_u16);
        return;
    }
    size_t u16_len = decode_row_for_display((const unsigned char *)raw, row_len, u16, byte_to_u16);
    free(raw);
    CFStringRef lineStr = CFStringCreateWithCharacters(NULL, u16, (CFIndex)u16_len);
    free(u16);
    if (!lineStr) {
        free(byte_to_u16);
        return;
    }
    CFIndex utf16_len = CFStringGetLength(lineStr);
    CFMutableAttributedStringRef attrStr = CFAttributedStringCreateMutable(NULL, 0);
    CFAttributedStringReplaceString(attrStr, CFRangeMake(0, 0), lineStr);
    CFAttributedStringSetAttributes(attrStr, CFRangeMake(0, utf16_len), attrs, true);

    for (size_t t = 0; t < *token_count; t++) {
        if (tokens[t].kind == BTN_TOK_NORMAL) {
            continue;
        }
        size_t tok_abs_start = *cached_line_start + tokens[t].start;
        size_t tok_abs_end = tok_abs_start + tokens[t].len;
        size_t clip_start = tok_abs_start > row_start ? tok_abs_start : row_start;
        size_t clip_end = tok_abs_end < row_end ? tok_abs_end : row_end;
        if (clip_start >= clip_end) {
            continue;
        }
        CFIndex u16_from = (CFIndex)byte_to_u16[clip_start - row_start];
        CFIndex u16_to = (CFIndex)byte_to_u16[clip_end - row_start];
        if (u16_from >= u16_to) {
            continue;
        }
        CFAttributedStringSetAttribute(attrStr, CFRangeMake(u16_from, u16_to - u16_from),
                                        kCTForegroundColorAttributeName, get_token_color(tokens[t].kind));
    }
    free(byte_to_u16);

    CTLineRef ctLine = CTLineCreateWithAttributedString(attrStr);
    CGContextSetTextPosition(ctx, x, top_y + 4.0);
    CTLineDraw(ctLine, ctx);
    CFRelease(ctLine);
    CFRelease(attrStr);
    CFRelease(lineStr);
}

long btn_visible_row_capacity(double content_height) {
    /* Row k (0 = oberste sichtbare) hat top_y = H - TOP_PADDING - (k+1) *
     * LINE_HEIGHT (siehe btn_render_frame()) und ist ganz sichtbar, solange
     * top_y >= BTN_FOOTER_HEIGHT. */
    double usable = content_height - TOP_PADDING - BTN_FOOTER_HEIGHT;
    long n = usable > 0.0 ? (long)(usable / LINE_HEIGHT) : 0;
    return n > 0 ? n : 1;
}

void btn_text_rows_extent(CGRect bounds, double *top, double *bottom) {
    *top = bounds.size.height - TOP_PADDING;
    *bottom = *top - (double)btn_visible_row_capacity(bounds.size.height) * LINE_HEIGHT;
}

/* ---- Scrollbalken ----
 * Die (unsichtbare) Schiene reicht ueber die Inhaltshoehe oberhalb der
 * Statuszeile, der Knopf ist so hoch wie der sichtbare Anteil (mindestens
 * BTN_SCROLLBAR_MIN_KNOB) und mittig im Streifen am rechten Rand. */
#define SCROLLBAR_INSET 2.0
#define SCROLLBAR_KNOB_WIDTH 6.0

typedef struct {
    double top, range; /* Oberkante der Schiene, Weg der Knopf-Oberkante */
    double knob_h;
    long max_scroll;
} ScrollbarTrack;

/* 0 = kein Knopf (alles sichtbar oder Fenster zu niedrig). */
static int scrollbar_track(CGRect bounds, size_t row_count, ScrollbarTrack *t) {
    long capacity = btn_visible_row_capacity(bounds.size.height);
    double bottom = BTN_FOOTER_HEIGHT + SCROLLBAR_INSET;
    t->top = bounds.size.height - SCROLLBAR_INSET;
    double track_h = t->top - bottom;
    t->max_scroll = (long)row_count - capacity;
    if (t->max_scroll <= 0 || track_h < BTN_SCROLLBAR_MIN_KNOB) {
        return 0;
    }
    t->knob_h = track_h * (double)capacity / (double)row_count;
    if (t->knob_h < BTN_SCROLLBAR_MIN_KNOB) {
        t->knob_h = BTN_SCROLLBAR_MIN_KNOB;
    }
    t->range = track_h - t->knob_h;
    return 1;
}

int btn_scrollbar_knob(CGRect bounds, size_t row_count, long scroll_row, CGRect *out_knob) {
    ScrollbarTrack t;
    if (!scrollbar_track(bounds, row_count, &t)) {
        return 0;
    }
    double frac = (double)scroll_row / (double)t.max_scroll;
    frac = frac < 0.0 ? 0.0 : frac > 1.0 ? 1.0 : frac;
    double knob_top = t.top - frac * t.range;
    double x = bounds.size.width - (BTN_SCROLLBAR_WIDTH + SCROLLBAR_KNOB_WIDTH) / 2.0;
    *out_knob = CGRectMake(x, knob_top - t.knob_h, SCROLLBAR_KNOB_WIDTH, t.knob_h);
    return 1;
}

long btn_scrollbar_row_for_knob_top(CGRect bounds, size_t row_count, double knob_top) {
    ScrollbarTrack t;
    if (!scrollbar_track(bounds, row_count, &t) || t.range <= 0.0) {
        return 0;
    }
    double frac = (t.top - knob_top) / t.range;
    frac = frac < 0.0 ? 0.0 : frac > 1.0 ? 1.0 : frac;
    return (long)(frac * (double)t.max_scroll + 0.5);
}

static int g_scrollbar_active = 0;

void btn_render_set_scrollbar_active(int active) {
    g_scrollbar_active = active;
}

static void draw_scroll_knob(CGContextRef ctx, CGRect knob) {
    set_fill(ctx, col_scroll_knob(g_scrollbar_active));
    double radius = SCROLLBAR_KNOB_WIDTH / 2.0;
    CGPathRef path = CGPathCreateWithRoundedRect(knob, radius, radius, NULL);
    CGContextAddPath(ctx, path);
    CGContextFillPath(ctx);
    CGPathRelease(path);
}

int btn_text_cursor_rects(CGRect window, CGRect content, int find_bar_visible, int scrollbar_visible, CGRect out[3]) {
    int n = 0;
    double w = content.size.width - GUTTER_WIDTH - (scrollbar_visible ? BTN_SCROLLBAR_WIDTH : 0.0);
    double h = content.size.height - BTN_FOOTER_HEIGHT;
    if (w > 0.0 && h > 0.0) {
        out[n++] = CGRectMake(GUTTER_WIDTH, BTN_FOOTER_HEIGHT, w, h);
    }
    if (find_bar_visible) {
        /* Wie draw_find_field()/btn_render_find_caret_rect() */
        FindBarGeometry g = find_bar_geometry();
        double y = window.size.height - BTN_TAB_BAR_HEIGHT - BTN_FIND_BAR_HEIGHT + 4.0;
        out[n++] = CGRectMake(g.search_field_x, y, BTN_FIND_FIELD_WIDTH, BTN_FIND_BAR_HEIGHT - 8.0);
        out[n++] = CGRectMake(g.replace_field_x, y, BTN_FIND_FIELD_WIDTH, BTN_FIND_BAR_HEIGHT - 8.0);
    }
    return n;
}

void btn_render_frame(CGContextRef ctx, CGRect bounds, Editor *ed, long scroll_row, const BtnLangSpec *lang,
                       const size_t *match_starts, const size_t *match_ends, size_t match_count) {
    set_fill(ctx, col_bg());
    CGContextFillRect(ctx, bounds);

    double char_width = get_char_width();
    CFDictionaryRef attrs = get_text_attrs();

    double text_width = btn_layout_text_width(bounds);
    size_t row_count;
    const BtnRow *rows = btn_layout_get(ed, text_width, &row_count);

    int has_sel = editor_has_selection(ed);
    size_t sel_start = has_sel ? editor_selection_start(ed) : 0;
    size_t sel_end = has_sel ? editor_selection_end(ed) : 0;

    /* Wandert nur vorwaerts durch match_starts/match_ends mit, passend zur
     * ebenfalls aufsteigenden Row-Reihenfolge der Schleife unten -
     * O(Rows + Treffer) statt O(Rows * Treffer). */
    size_t match_idx = 0;

    /* Klammer-Hervorhebung: nur ohne aktive Selektion (wie beim Cursor
     * selbst weiter unten) - klebt der Cursor an einer Klammer, werden sie
     * und ihre Gegenklammer markiert. */
    int has_bracket_match = 0;
    size_t bracket_a = 0, bracket_b = 0;
    if (!has_sel) {
        size_t adj;
        if (editor_cursor_adjacent_bracket(ed, &adj)) {
            has_bracket_match = editor_find_matching_bracket(ed, adj, &bracket_b);
            bracket_a = adj;
        }
    }

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
        comment_state = comment_state_before_line(ed, lang, rows, row_count, rows[first_row].logical_line);
    }

    for (size_t r = (size_t)scroll_row; r < row_count; r++) {
        double top_y = bounds.size.height - TOP_PADDING - (double)(r - (size_t)scroll_row + 1) * LINE_HEIGHT;
        if (top_y + LINE_HEIGHT < BTN_FOOTER_HEIGHT) {
            break;
        }

        size_t row_start = rows[r].start;
        size_t row_len = rows[r].len;
        size_t row_end = row_start + row_len;

        if (match_count > 0) {
            while (match_idx < match_count && match_ends[match_idx] <= row_start) {
                match_idx++;
            }
            for (size_t mi = match_idx; mi < match_count && match_starts[mi] < row_end; mi++) {
                size_t hi_from = match_starts[mi] > row_start ? match_starts[mi] : row_start;
                size_t hi_to = match_ends[mi] < row_end ? match_ends[mi] : row_end;
                if (hi_from >= hi_to) {
                    continue;
                }
                size_t col_from = editor_visual_column_in_range(ed, row_start, hi_from);
                size_t col_to = editor_visual_column_in_range(ed, row_start, hi_to);
                double hx = GUTTER_WIDTH + LEFT_PADDING + (double)col_from * char_width;
                double hw = (double)(col_to - col_from) * char_width;
                if (hw < 2.0) {
                    hw = 2.0;
                }
                set_fill(ctx, col_match_highlight());
                CGContextFillRect(ctx, CGRectMake(hx, top_y, hw, LINE_HEIGHT));
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
                set_fill(ctx, col_selection());
                CGContextFillRect(ctx, CGRectMake(hx, top_y, hw, LINE_HEIGHT));
            }
        }

        if (has_bracket_match) {
            for (int b = 0; b < 2; b++) {
                size_t pos = (b == 0) ? bracket_a : bracket_b;
                if (pos >= row_start && pos < row_end) {
                    size_t col = editor_visual_column_in_range(ed, row_start, pos);
                    double bx = GUTTER_WIDTH + LEFT_PADDING + (double)col * char_width;
                    set_fill(ctx, col_bracket());
                    CGContextFillRect(ctx, CGRectMake(bx, top_y, char_width, LINE_HEIGHT));
                }
            }
        }

        draw_row_line(ctx, ed, lang, rows, row_count, r, GUTTER_WIDTH + LEFT_PADDING, top_y, attrs,
                       &cached_line, &cached_line_start, &comment_state, tokens, &token_count);
    }

    if (!has_sel) {
        size_t cur_row = btn_layout_row_for_offset(rows, row_count, ed->cursor);
        if (cur_row >= (size_t)scroll_row) {
            size_t col = editor_visual_column_in_range(ed, rows[cur_row].start, ed->cursor);
            double cx = GUTTER_WIDTH + LEFT_PADDING + (double)col * char_width;
            double cy = bounds.size.height - TOP_PADDING - (double)(cur_row - (size_t)scroll_row + 1) * LINE_HEIGHT;
            if (g_marked.target == BTN_MARKED_DOCUMENT) {
                draw_marked_overlay(ctx, cx, cy, LINE_HEIGHT, cy + 4.0, attrs, col_bg());
            } else {
                set_fill(ctx, col_cursor());
                CGContextFillRect(ctx, CGRectMake(cx, cy, 1.4, LINE_HEIGHT - 2));
            }
        }
    }

    draw_gutter(ctx, bounds, rows, row_count, scroll_row);
    CGRect knob;
    if (btn_scrollbar_knob(bounds, row_count, scroll_row, &knob)) {
        draw_scroll_knob(ctx, knob);
    }
    draw_footer(ctx, bounds, ed, rows, row_count);
    /* rows gehoert dem Layout-Cache (btn_layout_get()) - nicht freigeben. */
    /* attrs ist gecacht (siehe get_text_attrs()) - keine Freigabe hier. */
}

size_t btn_rows_per_page(double page_height) {
    double usable = page_height - 2.0 * PRINT_MARGIN;
    long n = (long)(usable / LINE_HEIGHT);
    return n > 0 ? (size_t)n : 1;
}

double btn_print_text_width(double page_width) {
    double w = page_width - 2.0 * PRINT_MARGIN;
    return w > 1.0 ? w : 1.0;
}

/* Zeichnet die Rows [first_row, first_row + btn_rows_per_page(page_rect.height))
 * einer Druckseite - Text mit Syntax-Hervorhebung wie am Bildschirm, aber
 * bewusst ohne Gutter/Cursor/Selektion/Statuszeile (fuer den Ausdruck
 * irrelevant). page_rect ist wie bei btn_draw_callback nicht geflippt
 * (Ursprung unten links); main.c/shim.m sorgen per CTM-Verschiebung dafuer,
 * dass hier stets bei (0,0) beginnende Seitenkoordinaten ankommen (siehe
 * btn_print_pages() in shim.m). */
void btn_render_print_page(CGContextRef ctx, CGRect page_rect, Editor *ed, const BtnLangSpec *lang,
                            const BtnRow *rows, size_t row_count, size_t first_row,
                            int start_comment_state) {
    /* Eine Druckseite ist immer weisses Papier/schwarze Tinte, unabhaengig
     * vom aktuellen Bildschirm-Erscheinungsbild - Hintergrund/Textfarbe
     * unten sind schon immer fest verdrahtet, aber get_token_color() haengt
     * am globalen g_dark_mode und wuerde im Dark Mode seine fuer dunklen
     * Bildschirmhintergrund gedachten, hellen Varianten auf weissem Papier
     * zeichnen (schlechter Kontrast). Fuer die Dauer dieser Funktion auf
     * Light Mode zwingen, danach wiederherstellen. */
    int saved_dark_mode = g_dark_mode;
    if (g_dark_mode) {
        g_dark_mode = 0;
        invalidate_style_cache();
    }

    CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
    CGContextFillRect(ctx, page_rect);

    CTFontRef font = get_font();
    CGColorRef black = CGColorCreateGenericRGB(0.0, 0.0, 0.0, 1.0);
    CFDictionaryRef attrs = make_attrs(font, black);

    size_t cached_line = (size_t)-1;
    size_t cached_line_start = 0;
    BtnToken tokens[BTN_MAX_TOKENS_PER_LINE];
    size_t token_count = 0;
    int comment_state = start_comment_state;

    double x = page_rect.origin.x + PRINT_MARGIN;
    size_t r = first_row;
    for (size_t i = 0; r < row_count; i++, r++) {
        double top_y = page_rect.origin.y + page_rect.size.height - PRINT_MARGIN - (double)(i + 1) * LINE_HEIGHT;
        if (top_y < page_rect.origin.y + PRINT_MARGIN) {
            break;
        }
        draw_row_line(ctx, ed, lang, rows, row_count, r, x, top_y, attrs,
                       &cached_line, &cached_line_start, &comment_state, tokens, &token_count);
    }

    CFRelease(attrs);
    CGColorRelease(black);

    if (saved_dark_mode) {
        g_dark_mode = 1;
        invalidate_style_cache();
    }
}

size_t btn_hit_test(Editor *ed, CGRect bounds, double x, double y, long scroll_row) {
    double char_width = get_char_width();
    double text_width = btn_layout_text_width(bounds);
    size_t row_count;
    const BtnRow *rows = btn_layout_get(ed, text_width, &row_count);

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

    return btn_row_offset_for_column(ed, rows, row_count, (size_t)row, (size_t)col);
}

CGRect btn_render_caret_rect(Editor *ed, CGRect bounds, long scroll_row, size_t offset) {
    double char_width = get_char_width();
    size_t row_count;
    const BtnRow *rows = btn_layout_get(ed, btn_layout_text_width(bounds), &row_count);
    size_t cur_row = btn_layout_row_for_offset(rows, row_count, offset);
    size_t col = editor_visual_column_in_range(ed, rows[cur_row].start, offset);
    double x = GUTTER_WIDTH + LEFT_PADDING + (double)col * char_width;
    double y = bounds.size.height - TOP_PADDING - ((double)cur_row - (double)scroll_row + 1.0) * LINE_HEIGHT;
    return CGRectMake(x, y, char_width, LINE_HEIGHT);
}

CGRect btn_render_find_caret_rect(CGRect bounds, Editor *field, int replace_field, size_t offset) {
    FindBarGeometry g = find_bar_geometry();
    double bar_top = bounds.size.height - BTN_TAB_BAR_HEIGHT - BTN_FIND_BAR_HEIGHT;
    double field_x = replace_field ? g.replace_field_x : g.search_field_x;
    size_t col = editor_visual_column_in_range(field, 0, offset);
    double char_width = get_char_width();
    return CGRectMake(field_x + (double)col * char_width, bar_top + 4.0, char_width, BTN_FIND_BAR_HEIGHT - 8.0);
}
