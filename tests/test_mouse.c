/* Maus und Scrollen (Phase 5): Scrollbalken-Geometrie und ihre Umkehrung,
 * I-Beam-Flaechen, Autoscroll-Stufen und on_mouse() aus main.c mit echtem
 * Layout/Hit-Test aus render.c - Markieren mit Autoscroll-Takt, Knopf
 * ziehen, seitenweise blaettern. Zeichenbreite im Test fest 8pt. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "eol.h"
#include "render.h"
#include "shim.h"
#include "doc_extracted.h"
#include "focus_extracted.h"
#include "drag_type_extracted.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static double get_char_width(void) { return 8.0; }
#include "mouse_render_extracted.h"
#include "render_pure_extracted.h"

/* ---- Umgebung von on_mouse() ---- */
static Document g_doc;
static CGRect g_bounds = { { 0, 0 }, { 900, 600 } };
static int g_find_bar_visible = 0;
static BtnFocus g_focus = BTN_FOCUS_DOCUMENT;
static BtnDrag g_drag = BTN_DRAG_NONE;
static double g_drag_knob_offset;
static int g_autoscroll_on, g_tab_clicks, g_find_clicks, g_commits;
static Document *active_doc(void) { return &g_doc; }
static void commit_marked(void) { g_commits++; }
static void handle_tab_bar_click(double x) { (void)x; g_tab_clicks++; }
static void handle_find_bar_click(double x) { (void)x; g_find_clicks++; }
void btn_app_set_autoscroll(int on) { g_autoscroll_on = on; }
void btn_text_input_invalidate(void) {}
void btn_app_request_redraw(void) {}
#include "mouse_extracted.h"

/* Geometrie im Test: Inhaltsbereich 900 x 568 (Fenster 600 minus Tableiste),
 * erste Row oben bei 560, 29 Rows sichtbar, letzte endet bei 38. */
static double row_y(long k) { return 568.0 - TOP_PADDING - LINE_HEIGHT * (double)k - LINE_HEIGHT / 2.0; }
static double col_x(long c) { return GUTTER_WIDTH + LEFT_PADDING + 8.0 * (double)c; }

static void set_lines(int n) {
    char *buf = malloc((size_t)n * 9 + 1);
    for (int i = 0; i < n; i++) {
        char line[16];
        snprintf(line, sizeof line, "line %03d\n", i % 1000);
        memcpy(buf + i * 9, line, 9);
    }
    editor_set_text(&g_doc.editor, buf, (size_t)n * 9);
    free(buf);
    g_doc.scroll_row = 0;
}

static void test_geometry(void) {
    CGRect cb = CGRectMake(0, 0, 900, 568);
    long cap = btn_visible_row_capacity(568);
    CGRect k;
    CHECK(cap == 29, "capacity 29 (%ld)", cap);
    CHECK(!btn_scrollbar_knob(cb, (size_t)cap, 0, &k), "no knob when everything fits");
    CHECK(!btn_scrollbar_knob(cb, 0, 0, &k), "no knob for empty layout");
    CHECK(btn_scrollbar_knob(cb, (size_t)cap + 1, 0, &k), "knob as soon as one row is hidden");

    double track_top = 568 - SCROLLBAR_INSET, track_bottom = BTN_FOOTER_HEIGHT + SCROLLBAR_INSET;
    double track_h = track_top - track_bottom;
    btn_scrollbar_knob(cb, 290, 0, &k);
    CHECK(fabs(k.size.height - track_h * 29.0 / 290.0) < 1e-9, "knob height = visible share (%.2f)", k.size.height);
    CHECK(fabs(k.origin.y + k.size.height - track_top) < 1e-9, "scroll 0: knob at the top");
    CHECK(k.origin.x >= 900 - BTN_SCROLLBAR_WIDTH && k.origin.x + k.size.width <= 900 && k.size.width > 0,
          "knob inside the right strip");
    CHECK(GUTTER_WIDTH + LEFT_PADDING + btn_layout_text_width(cb) <= k.origin.x - 2.0, "text wraps before the knob");
    btn_scrollbar_knob(cb, 290, 290 - 29, &k);
    CHECK(fabs(k.origin.y - track_bottom) < 1e-9, "last scroll row: knob at the bottom");
    btn_scrollbar_knob(cb, 290, 5000, &k);
    CHECK(fabs(k.origin.y - track_bottom) < 1e-9, "scroll beyond the end clamps");
    btn_scrollbar_knob(cb, 1000000, 0, &k);
    CHECK(k.size.height == BTN_SCROLLBAR_MIN_KNOB, "huge document: minimum knob height");
    CHECK(!btn_scrollbar_knob(CGRectMake(0, 0, 900, 40), 1000, 0, &k), "window too low: no knob");
    CHECK(btn_scrollbar_row_for_knob_top(CGRectMake(0, 0, 900, 40), 1000, 10) == 0, "window too low: row 0");

    /* Umkehrung fuer jede Scroll-Position, mehrere Hoehen/Laengen */
    double heights[] = { 120, 568, 1000 };
    size_t counts[] = { 30, 31, 100, 1000, 100000 };
    long bad = 0, nonmono = 0;
    for (int h = 0; h < 3; h++) {
        CGRect b = CGRectMake(0, 0, 700, heights[h]);
        long c = btn_visible_row_capacity(heights[h]);
        for (int n = 0; n < 5; n++) {
            long max = (long)counts[n] - c;
            double prev = 1e9;
            for (long r = 0; r <= max; r++) {
                if (!btn_scrollbar_knob(b, counts[n], r, &k)) {
                    bad++;
                    break;
                }
                double top = k.origin.y + k.size.height;
                if (btn_scrollbar_row_for_knob_top(b, counts[n], top) != r) {
                    bad++;
                }
                if (top > prev) {
                    nonmono++;
                }
                prev = top;
            }
            CHECK(btn_scrollbar_row_for_knob_top(b, counts[n], 1e6) == 0, "knob dragged far up: row 0");
            CHECK(btn_scrollbar_row_for_knob_top(b, counts[n], -1e6) == (max > 0 ? max : 0), "knob dragged far down: last row");
        }
    }
    CHECK(bad == 0, "knob position -> scroll row round trip (%ld mismatches)", bad);
    CHECK(nonmono == 0, "knob moves down as scroll row grows (%ld)", nonmono);

    /* I-Beam-Flaechen */
    CGRect r[3];
    CGRect win = CGRectMake(0, 0, 900, 600);
    int n = btn_text_cursor_rects(win, cb, 0, 1, r);
    CHECK(n == 1 && r[0].origin.x == GUTTER_WIDTH && r[0].origin.x + r[0].size.width == 900 - BTN_SCROLLBAR_WIDTH &&
          r[0].origin.y == BTN_FOOTER_HEIGHT && r[0].origin.y + r[0].size.height == 568,
          "text area without gutter, scrollbar, footer and tab bar");
    n = btn_text_cursor_rects(win, cb, 0, 0, r);
    CHECK(n == 1 && r[0].origin.x + r[0].size.width == 900, "no knob: the strip is text (I-beam up to the edge)");
    CGRect cb2 = CGRectMake(0, 0, 900, 568 - BTN_FIND_BAR_HEIGHT);
    n = btn_text_cursor_rects(win, cb2, 1, 1, r);
    double bar_bottom = 600 - BTN_TAB_BAR_HEIGHT - BTN_FIND_BAR_HEIGHT, bar_top = 600 - BTN_TAB_BAR_HEIGHT;
    CHECK(n == 3 && r[0].origin.y + r[0].size.height == bar_bottom, "find bar open: text area ends below it");
    for (int i = 1; i < n; i++) {
        CHECK(r[i].origin.y >= bar_bottom && r[i].origin.y + r[i].size.height <= bar_top && r[i].size.width == BTN_FIND_FIELD_WIDTH,
              "find field %d inside the find bar", i);
    }
    CHECK(n == 3 && r[1].origin.x + r[1].size.width <= r[2].origin.x, "search and replace field side by side");
    CHECK(btn_text_cursor_rects(CGRectMake(0, 0, 50, 600), CGRectMake(0, 0, 50, 568), 0, 1, r) == 0,
          "window narrower than gutter + scrollbar: no text area");
}

static void test_autoscroll_rows(void) {
    double top = 560, bottom = 38;
    CHECK(autoscroll_rows(300, top, bottom, 29) == 0, "inside: no autoscroll");
    CHECK(autoscroll_rows(top, top, bottom, 29) == 0 && autoscroll_rows(bottom, top, bottom, 29) == 0, "edges count as inside");
    CHECK(autoscroll_rows(top + 0.5, top, bottom, 29) == -1, "just above: one row up");
    CHECK(autoscroll_rows(top + 18.5, top, bottom, 29) == -2, "one row height further: two rows");
    CHECK(autoscroll_rows(bottom - 0.5, top, bottom, 29) == 1, "just below: one row down");
    CHECK(autoscroll_rows(bottom - 40, top, bottom, 29) == 3, "40pt below: three rows");
    CHECK(autoscroll_rows(top + 1e6, top, bottom, 29) == -29 && autoscroll_rows(-1e6, top, bottom, 29) == 29,
          "far outside: capped at one page");
}

static void mouse(btn_mouse_phase phase, double x, double y) { on_mouse(phase, x, y, 1, 0); }

static void test_drag_select(void) {
    Editor *ed = &g_doc.editor;
    CGRect cb = content_bounds();
    double top, bottom;
    btn_text_rows_extent(cb, &top, &bottom);
    CHECK(top == 560 && bottom == 38, "rows extent 560..38 (%.1f..%.1f)", top, bottom);

    set_lines(200); /* 201 Rows (letzte leer), max scroll 172 */
    g_doc.scroll_row = 50;
    mouse(BTN_MOUSE_DOWN, col_x(2), row_y(3));
    CHECK(ed->cursor == 53 * 9 + 2 && g_drag == BTN_DRAG_TEXT && !g_autoscroll_on, "click: cursor line 53 col 2, dragging");

    for (int i = 0; i < 10; i++) {
        mouse(BTN_MOUSE_DRAGGED, col_x(4), top + 5 + i);
    }
    CHECK(g_doc.scroll_row == 50, "moving the mouse above the text does not scroll by itself (%ld)", g_doc.scroll_row);
    CHECK(g_autoscroll_on, "above the text: autoscroll requested");
    CHECK(ed->cursor == 50 * 9 + 4 && editor_selection_end(ed) == 53 * 9 + 2, "selection reaches the top visible row");

    mouse(BTN_MOUSE_AUTOSCROLL, col_x(4), top + 5);
    CHECK(g_doc.scroll_row == 49 && ed->cursor == 49 * 9 + 4, "tick: one row up, selection follows (%ld)", g_doc.scroll_row);
    mouse(BTN_MOUSE_AUTOSCROLL, col_x(4), top + 40);
    CHECK(g_doc.scroll_row == 46 && ed->cursor == 46 * 9 + 4, "40pt above: three rows per tick (%ld)", g_doc.scroll_row);
    for (int i = 0; i < 10; i++) {
        mouse(BTN_MOUSE_AUTOSCROLL, col_x(4), top + 1e6);
    }
    CHECK(g_doc.scroll_row == 0 && ed->cursor == 4 && editor_selection_end(ed) == 53 * 9 + 2,
          "ticks stop at the top, anchor kept (%ld)", g_doc.scroll_row);
    CHECK(!g_autoscroll_on, "at the top: no more ticks requested");
    mouse(BTN_MOUSE_DRAGGED, col_x(4), top + 30);
    CHECK(!g_autoscroll_on && ed->cursor == 4, "above the text at the top: selection to row 0, no timer");

    mouse(BTN_MOUSE_DRAGGED, col_x(4), row_y(5));
    CHECK(!g_autoscroll_on && g_doc.scroll_row == 0 && ed->cursor == 5 * 9 + 4, "back inside: autoscroll off");

    /* Angeschnittene Row ueber der Statuszeile: gehoert zur letzten ganzen,
     * kein Autoscroll, kein Scrollen */
    for (int i = 0; i < 5; i++) {
        mouse(BTN_MOUSE_DRAGGED, col_x(1 + i), bottom - 1 - i);
    }
    CHECK(!g_autoscroll_on && g_doc.scroll_row == 0 && ed->cursor == 28 * 9 + 5,
          "partial row above the footer: last full row, no autoscroll (%ld, %zu)", g_doc.scroll_row, ed->cursor);
    double below = BTN_FOOTER_HEIGHT - 1;
    mouse(BTN_MOUSE_DRAGGED, col_x(1), below);
    CHECK(g_autoscroll_on && g_doc.scroll_row == 0 && ed->cursor == 28 * 9 + 1, "on the footer: selection to the last full row, autoscroll");
    mouse(BTN_MOUSE_AUTOSCROLL, col_x(1), below);
    CHECK(g_doc.scroll_row == 1 && ed->cursor == 29 * 9 + 1, "tick below: one row down (%ld)", g_doc.scroll_row);
    for (int i = 0; i < 300; i++) {
        mouse(BTN_MOUSE_AUTOSCROLL, col_x(1), below);
    }
    CHECK(g_doc.scroll_row == 172 && ed->cursor == 1800, "ticks stop at the end (%ld, cursor %zu)", g_doc.scroll_row, ed->cursor);
    CHECK(!g_autoscroll_on, "at the end: no more ticks requested");

    mouse(BTN_MOUSE_UP, col_x(1), below);
    CHECK(!g_autoscroll_on && g_drag == BTN_DRAG_NONE, "mouse up: autoscroll off");
    size_t cur = ed->cursor;
    g_doc.scroll_row = 100;
    mouse(BTN_MOUSE_AUTOSCROLL, col_x(1), 5);
    mouse(BTN_MOUSE_DRAGGED, col_x(1), row_y(2));
    CHECK(g_doc.scroll_row == 100 && ed->cursor == cur, "after mouse up: late tick/drag ignored");

    /* Doppelklick markiert ein Wort, Ziehen danach aendert nichts */
    on_mouse(BTN_MOUSE_DOWN, col_x(1), row_y(0), 2, 0);
    size_t s0 = editor_selection_start(ed), e0 = editor_selection_end(ed);
    CHECK(g_drag == BTN_DRAG_NONE && e0 - s0 == 4, "double click selects a word, no drag");
    mouse(BTN_MOUSE_DRAGGED, col_x(1), row_y(10));
    CHECK(editor_selection_start(ed) == s0 && editor_selection_end(ed) == e0, "drag after double click ignored");
    mouse(BTN_MOUSE_UP, 0, 0);

    /* Neuer Klick stoppt einen laufenden Autoscroll */
    mouse(BTN_MOUSE_DOWN, col_x(1), row_y(1));
    mouse(BTN_MOUSE_DRAGGED, col_x(1), top + 5);
    CHECK(g_autoscroll_on, "autoscroll running");
    on_mouse(BTN_MOUSE_DOWN, 10, 599, 1, 0); /* Tableiste */
    CHECK(!g_autoscroll_on && g_drag == BTN_DRAG_NONE && g_tab_clicks == 1, "click elsewhere stops the drag");
}

static void test_scrollbar_clicks(void) {
    Editor *ed = &g_doc.editor;
    CGRect cb = content_bounds();
    set_lines(200);
    editor_set_cursor(ed, 0, 0);
    CGRect k;
    btn_scrollbar_knob(cb, 201, 0, &k);
    double kx = k.origin.x + k.size.width / 2, ky = k.origin.y + k.size.height / 2;
    int commits = g_commits;
    mouse(BTN_MOUSE_DOWN, kx, ky);
    CHECK(g_commits == commits, "scrollbar click keeps a running input-method composition");
    CHECK(g_drag == BTN_DRAG_SCROLLBAR && ed->cursor == 0 && !editor_has_selection(ed) && g_doc.scroll_row == 0,
          "click on the knob: knob drag, cursor unchanged");
    mouse(BTN_MOUSE_AUTOSCROLL, kx, ky - 100);
    CHECK(g_doc.scroll_row == 0, "autoscroll tick ignored while dragging the knob");
    mouse(BTN_MOUSE_DRAGGED, kx, ky - 100);
    long expect = btn_scrollbar_row_for_knob_top(cb, 201, k.origin.y + k.size.height - 100);
    CHECK(g_doc.scroll_row == expect && expect > 0 && ed->cursor == 0, "dragging the knob scrolls (%ld), cursor stays", g_doc.scroll_row);
    mouse(BTN_MOUSE_DRAGGED, kx + 300, -500); /* weit nach unten und aus dem Streifen */
    CHECK(g_doc.scroll_row == 172, "dragged past the bottom: last page (%ld)", g_doc.scroll_row);
    mouse(BTN_MOUSE_UP, kx, -500);
    CHECK(g_doc.scroll_row == 172 && g_drag == BTN_DRAG_NONE, "mouse up keeps the scroll position (no jump back to the cursor)");

    g_doc.scroll_row = 0;
    mouse(BTN_MOUSE_DOWN, kx, BTN_FOOTER_HEIGHT + 3);
    CHECK(g_doc.scroll_row == 28 && g_drag == BTN_DRAG_NONE && ed->cursor == 0, "click below the knob: one page down (%ld)", g_doc.scroll_row);
    mouse(BTN_MOUSE_UP, kx, BTN_FOOTER_HEIGHT + 3);
    mouse(BTN_MOUSE_DOWN, kx, 565);
    CHECK(g_doc.scroll_row == 0, "click above the knob: one page up (%ld)", g_doc.scroll_row);
    mouse(BTN_MOUSE_UP, kx, 565);
    g_doc.scroll_row = 170;
    mouse(BTN_MOUSE_DOWN, kx, BTN_FOOTER_HEIGHT + 3);
    CHECK(g_doc.scroll_row == 172, "paging clamps at the end");
    mouse(BTN_MOUSE_UP, kx, BTN_FOOTER_HEIGHT + 3);

    /* Rechter Rand der Tab-/Suchleiste gehoert nicht zum Scrollbalken */
    g_find_bar_visible = 1;
    int finds = g_find_clicks, tabs = g_tab_clicks;
    mouse(BTN_MOUSE_DOWN, kx, 600 - BTN_TAB_BAR_HEIGHT - 5);
    mouse(BTN_MOUSE_DOWN, kx, 600 - 5);
    CHECK(g_find_clicks == finds + 1 && g_tab_clicks == tabs + 1 && g_commits > commits,
          "clicks at the right edge of find/tab bar go to the bars");
    g_find_bar_visible = 0;

    /* Kurzes Dokument: kein Knopf, der Streifen ist normaler Text */
    set_lines(10);
    mouse(BTN_MOUSE_DOWN, 900 - 4, row_y(2));
    CHECK(g_drag == BTN_DRAG_TEXT && ed->cursor == 2 * 9 + 8, "no knob: click in the strip places the cursor (%zu)", ed->cursor);
    mouse(BTN_MOUSE_UP, 0, 0);

    /* Statuszeile: nichts */
    size_t cur = ed->cursor;
    mouse(BTN_MOUSE_DOWN, 900 - 4, 5);
    CHECK(ed->cursor == cur && g_drag == BTN_DRAG_NONE, "click in the footer does nothing");
}

int main(void) {
    editor_init(&g_doc.editor);
    test_geometry();
    test_autoscroll_rows();
    test_drag_select();
    test_scrollbar_clicks();
    editor_free(&g_doc.editor);
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
