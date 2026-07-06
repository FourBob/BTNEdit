#ifndef BTN_RENDER_H
#define BTN_RENDER_H

#include <CoreGraphics/CoreGraphics.h>
#include "editor.h"
#include "highlight.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Layout-Konstanten - oeffentlich, weil main.c sie fuers Scroll-Mass
 * (wie viele Zeilen passen ins Fenster) und fuer Klick-Ausschluss braucht. */
#define BTN_LINE_HEIGHT 18.0
#define BTN_FOOTER_HEIGHT 22.0

/* Tableiste oben im Fenster (mehrere Dokumente) - main.c braucht diese
 * Masse sowohl zum Reservieren der Content-Flaeche (bounds.size.height um
 * BTN_TAB_BAR_HEIGHT verkleinern, siehe content_bounds() dort) als auch
 * fuers eigene Hit-Testing von Tab-Klicks/Schliessen-Kreuz/"+"-Knopf -
 * beides muss dieselben Masse verwenden wie das Zeichnen hier. */
#define BTN_TAB_BAR_HEIGHT 32.0
#define BTN_TAB_ITEM_WIDTH 160.0
#define BTN_TAB_CLOSE_WIDTH 22.0
#define BTN_TAB_NEW_WIDTH 32.0

/* Suchen/Ersetzen-Leiste, sitzt direkt unter der Tableiste, wenn sichtbar -
 * main.c braucht dieselben Masse fuers Reservieren der Content-Flaeche
 * (content_bounds()) und fuers eigene Hit-Testing des Regex-Umschalters,
 * genau wie bei der Tableiste oben. */
#define BTN_FIND_BAR_HEIGHT 30.0
#define BTN_FIND_BAR_PADDING 8.0
#define BTN_FIND_LABEL_WIDTH 70.0
#define BTN_FIND_FIELD_WIDTH 200.0
#define BTN_FIND_REGEX_WIDTH 26.0

/* Eine visuelle Zeile (Row) nach Wortumbruch: [start, start+len) im
 * Puffer. logical_line ist die zugehoerige "echte" Zeile (fuer die
 * Gutter-Nummerierung); is_continuation markiert Folge-Rows einer per
 * Wortumbruch aufgeteilten logischen Zeile (die keine eigene Nummer
 * bekommen). */
typedef struct {
    size_t start;
    size_t len;
    size_t logical_line;
    int is_continuation;
} BtnRow;

/* Verfuegbare Breite fuer Text (Fensterbreite minus Gutter/Polsterung). */
double btn_layout_text_width(CGRect bounds);

/* Berechnet das komplette Zeilenumbruch-Layout fuer den aktuellen
 * Pufferinhalt bei gegebener Textbreite. Caller muss btn_layout_free()
 * aufrufen. Wird sowohl vom Zeichnen als auch von main.c fuer
 * wortumbruch-bewusste Cursor-Bewegung/Scrolling/Hit-Testing genutzt. */
BtnRow *btn_layout_build(Editor *ed, double text_width, size_t *out_row_count);
void btn_layout_free(BtnRow *rows);

/* Row-Index, in dem der gegebene Puffer-Offset liegt (Grenzfaelle an
 * einem Zeilenumbruch werden dem Zeilenanfang der naechsten Row
 * zugeschlagen, nicht dem Ende der vorherigen). */
size_t btn_layout_row_for_offset(const BtnRow *rows, size_t row_count, size_t offset);

/* Tatsaechliche Breite eines einzelnen Tabs bei gegebener Tab-Anzahl und
 * Fensterbreite - volle BTN_TAB_ITEM_WIDTH, solange alles hineinpasst,
 * sonst gleichmaessig geschrumpft (nie unter 40pt). main.c's Hit-Testing
 * (handle_tab_bar_click) MUSS dieselbe Funktion aufrufen wie das Zeichnen
 * hier, sonst laufen Klick-Trefferpruefung und Darstellung auseinander,
 * sobald so viele Tabs offen sind, dass sie nicht mehr in voller Breite
 * passen. */
double btn_tab_width_for(int count, double window_width);

/* Zeichnet die Tableiste in den obersten BTN_TAB_BAR_HEIGHT Punkten von
 * bounds - labels[i] ist der bereits fertig formatierte Titel des i-ten
 * Tabs (main.c setzt dort z.B. ein Punkt-Praefix bei ungesicherten
 * Aenderungen), active der Index des aktiven Tabs. Reserviert am Ende
 * einen "+"-Knopf zum Anlegen eines neuen Tabs. */
void btn_render_tab_bar(CGContextRef ctx, CGRect bounds, const char *const *labels, int count, int active);

/* Zeichnet die Suchen/Ersetzen-Leiste direkt unter der Tableiste (Hoehe
 * BTN_FIND_BAR_HEIGHT). search_label/replace_label kommen von main.c
 * (uebersetzt via strings.h) - render.c selbst kennt wie beim Rest der App
 * keine Bediensprache. search_focused unterscheidet, in welchem der beiden
 * Felder der blinkende "Cursor" (nur ein Strich am Textende - Suchfelder
 * erlauben nur Anhaengen/Loeschen, keine Navigation mittendrin, siehe
 * main.c) gezeichnet wird. status darf leer sein. */
void btn_render_find_bar(CGContextRef ctx, CGRect bounds, const char *search_label, const char *query,
                          const char *replace_label, const char *replacement,
                          int regex_mode, int search_focused, const char *status);

/* Zeichnet einen Frame: Hintergrund, Selektion, Text (optional per lang
 * syntax-hervorgehoben), Cursor, Zeilennummern-Gutter und die
 * Statusleiste. scroll_row ist der (0-basierte) oberste sichtbare
 * Row-Index; lang darf NULL sein (keine Hervorhebung). */
void btn_render_frame(CGContextRef ctx, CGRect bounds, Editor *ed, long scroll_row, const BtnLangSpec *lang);

/* Bildet einen View-Punkt (Ursprung unten links, wie bei einer
 * nicht geflippten NSView) auf einen logischen Buffer-Offset ab,
 * unter Beruecksichtigung der aktuellen Scroll-Position. */
size_t btn_hit_test(Editor *ed, CGRect bounds, double x, double y, long scroll_row);

/* Wieviele Rows auf eine Druckseite der gegebenen Hoehe (in Punkten) passen -
 * main.c braucht das vor dem Druck, um die per btn_layout_build() erzeugten
 * Rows auf Seiten aufzuteilen, ohne render.c's Layout-Konstanten (Zeilenhoehe,
 * Rand) selbst zu duplizieren. */
size_t btn_rows_per_page(double page_height);

/* Verfuegbare Textbreite auf einer Druckseite (Seitenbreite minus Rand) -
 * das Gegenstueck zu btn_layout_text_width() fuers Drucken. main.c MUSS
 * dieses Ergebnis (nicht die volle Seitenbreite) an btn_layout_build()
 * uebergeben, sonst umbricht das Layout breiter als btn_render_print_page()
 * tatsaechlich Platz laesst. */
double btn_print_text_width(double page_width);

/* Zeichnet eine einzelne Druckseite: Rows [first_row, first_row +
 * btn_rows_per_page(page_rect.height)) mit Syntax-Hervorhebung wie am
 * Bildschirm, aber ohne Gutter/Cursor/Selektion/Statuszeile. page_rect ist
 * wie bei btn_draw_callback nicht geflippt (Ursprung unten links). */
void btn_render_print_page(CGContextRef ctx, CGRect page_rect, Editor *ed, const BtnLangSpec *lang,
                            const BtnRow *rows, size_t row_count, size_t first_row);

#ifdef __cplusplus
}
#endif

#endif /* BTN_RENDER_H */
