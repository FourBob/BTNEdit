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
/* Breite des ".*"-Regex-Umschalters UND der beiden neuen Umschalter direkt
 * daneben ("Aa" Gross-/Kleinschreibung, "\b" ganzes Wort) - alle drei sind
 * kurze, nicht uebersetzte Icon-Beschriftungen (2 Zeichen, wie ".*" schon
 * bisher) und teilen sich deshalb dieselbe feste Breite. */
#define BTN_FIND_REGEX_WIDTH 26.0
/* Muss auch die laengste uebersetzte Beschriftung bequem fassen - "Reemplazar
 * todo" (ES, 16 Zeichen) ist bei Menlo 13pt (~7.8pt/Zeichen) ca. 125pt breit;
 * 150pt laesst auf beiden Seiten noch sichtbaren Rand. */
#define BTN_FIND_REPLACE_ALL_WIDTH 150.0

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

/* Legt fest, ob nachfolgende Zeichenaufrufe die Dark- oder die Light-Mode-
 * Farbpalette verwenden. main.c fragt den aktuellen Modus (siehe
 * btn_app_is_dark_mode() in shim.h) bei JEDEM Redraw frisch ab und reicht
 * ihn hier rein, bevor btn_render_frame()/btn_render_tab_bar()/
 * btn_render_find_bar() gerufen werden - kein Notification-Mechanismus
 * noetig, weil ohnehin bei jedem Tastendruck/Resize neu gezeichnet wird.
 * Aendert sich der Modus, werden die gecachten Token-Farben verworfen
 * (siehe g_token_colors in render.c), damit sie nicht in der alten
 * Helligkeit haengen bleiben. */
void btn_render_set_dark_mode(int dark);

/* Schriftgroesse fuer Editor-Text/Gutter/Statuszeile/Tab-/Suchleiste (nicht
 * fuers Drucken - ein Ausdruck soll unabhaengig von der Bildschirm-Zoomstufe
 * immer dieselbe Papiergroesse ergeben). Aendert sich die Groesse, werden
 * Font/Zeichenbreite-Caches (siehe g_font/g_char_width in render.c)
 * verworfen und bei Bedarf neu vermessen. size wird auf
 * [BTN_MIN_FONT_SIZE, BTN_MAX_FONT_SIZE] geklemmt. */
#define BTN_MIN_FONT_SIZE 8.0
#define BTN_MAX_FONT_SIZE 32.0
#define BTN_DEFAULT_FONT_SIZE 13.0
void btn_render_set_font_size(double size);
double btn_render_get_font_size(void);
/* Kurzformen fuer Cmd+/Cmd-/Cmd+0 (main.c's Zoom-Menuepunkte) - je 1pt
 * Schritt, Reset auf BTN_DEFAULT_FONT_SIZE. */
/* Uebersetzte Statuszeilen-Formate (Zeiger muessen gueltig bleiben, z.B.
 * aus btn_tr()): pos_fmt mit zwei "%zu" (Zeile, Spalte), stats_fmt mit drei
 * (Zeilen, Woerter, Zeichen). Ein Format mit anderen Konversionen wird
 * ignoriert (btn_footer_format_ok()). */
void btn_render_set_footer_formats(const char *pos_fmt, const char *stats_fmt);
/* Rechts an die Statuszeile angehaengt, z.B. "CRLF" (wird kopiert). */
void btn_render_set_footer_eol(const char *label);
int btn_footer_format_ok(const char *fmt, int n);

void btn_render_zoom_in(void);
void btn_render_zoom_out(void);
void btn_render_zoom_reset(void);

/* Verfuegbare Breite fuer Text (Fensterbreite minus Gutter/Polsterung und
 * Scrollbalken-Streifen). */
double btn_layout_text_width(CGRect bounds);

/* Berechnet das komplette Zeilenumbruch-Layout fuer den aktuellen
 * Pufferinhalt bei gegebener Textbreite als eigene Kopie; Caller muss
 * btn_layout_free() aufrufen. Nur fuers Drucken gedacht, das die Rows ueber
 * mehrere Callbacks hinweg haelt. */
BtnRow *btn_layout_build(Editor *ed, double text_width, size_t *out_row_count);
void btn_layout_free(BtnRow *rows);

/* Dasselbe Layout, aber aus einem Cache, der nur bei geaendertem Inhalt
 * (edit_seq) oder geaenderter Zeichen-pro-Zeile-Zahl neu gebaut wird -
 * fuer Zeichnen, Hit-Testing und main.c's Cursor-Bewegung/Scrolling. Der
 * Zeiger gehoert dem Cache: NICHT freigeben, und nicht ueber einen
 * weiteren btn_layout_get()-Aufruf hinaus benutzen, der neu bauen koennte -
 * das passiert bei geaendertem Inhalt, anderer Breite/Schriftgroesse oder
 * einem anderen Editor, und gibt das vorige Array frei. */
const BtnRow *btn_layout_get(Editor *ed, double text_width, size_t *out_row_count);

/* Row-Index, in dem der gegebene Puffer-Offset liegt (Grenzfaelle an
 * einem Zeilenumbruch werden dem Zeilenanfang der naechsten Row
 * zugeschlagen, nicht dem Ende der vorherigen). */
size_t btn_layout_row_for_offset(const BtnRow *rows, size_t row_count, size_t offset);

/* Offset fuer eine visuelle Spalte in Row row (auf das Row-Ende geklemmt,
 * (size_t)-1 = Row-Ende). Das Ende einer umgebrochenen Row ist zugleich der
 * Anfang der naechsten und wuerde dort gezeichnet - deshalb landet das
 * Ergebnis dann vor dem letzten Zeichen der Row. Gemeinsam fuer Auf/Ab,
 * Ende/Cmd+Rechts und Mausklick. */
size_t btn_row_offset_for_column(Editor *ed, const BtnRow *rows, size_t row_count, size_t row, size_t col);

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
 * BTN_FIND_BAR_HEIGHT). search_label/replace_label/replace_all_label kommen
 * von main.c (uebersetzt via strings.h) - render.c selbst kennt wie beim
 * Rest der App keine Bediensprache. search_ed/replace_ed sind die beiden
 * Feld-Editoren (siehe editor.h) - liefern Inhalt, Cursor und Selektion in
 * einem. Beide sind garantiert einzeilig (main.c fuegt nie '\n' ein),
 * deshalb genuegt hier reine Byte-Spalten-Mathematik ohne Wortumbruch/
 * Zeilen-Konzept. focus_field: 0 = keins der beiden Felder fokussiert (nur
 * Inhalt zeigen, kein Cursor), 1 = Suchfeld, 2 = Ersetzen-Feld.
 * regex_mode/case_sensitive/whole_word steuern die drei Umschalter-Knoepfe
 * (".*"/"Aa"/"\b") direkt rechts vom Suchfeld, in genau dieser Reihenfolge -
 * main.c's handle_find_bar_click() testet dieselben drei Positionen. Der
 * "Alle ersetzen"-Knopf sitzt bei derselben x-Position (replace_field_x +
 * BTN_FIND_FIELD_WIDTH + BTN_FIND_BAR_PADDING, Breite
 * BTN_FIND_REPLACE_ALL_WIDTH), die main.c beim Mausklick testet (siehe
 * handle_find_bar_click()). status darf leer sein. */
void btn_render_find_bar(CGContextRef ctx, CGRect bounds, const char *search_label, Editor *search_ed,
                          const char *replace_label, Editor *replace_ed,
                          const char *replace_all_label,
                          int regex_mode, int case_sensitive, int whole_word,
                          int focus_field, const char *status);

/* Zeichnet einen Frame: Hintergrund, Suchtreffer-Hervorhebung, Selektion,
 * Text (optional per lang syntax-hervorgehoben), Cursor, Zeilennummern-
 * Gutter und die Statusleiste. scroll_row ist der (0-basierte) oberste
 * sichtbare Row-Index; lang darf NULL sein (keine Hervorhebung).
 * match_starts/match_ends sind match_count nach Start aufsteigend
 * sortierte, nicht ueberlappende Byte-Bereiche (siehe main.c's
 * collect_all_matches()) - werden gelb hervorgehoben, VOR der eigentlichen
 * Selektion gezeichnet, damit ein aktuell selektierter Treffer weiterhin in
 * der gewohnten Selektionsfarbe darueber erscheint. match_count darf 0 sein
 * (dann werden match_starts/match_ends nicht gelesen, duerfen also auch
 * NULL sein). */
void btn_render_frame(CGContextRef ctx, CGRect bounds, Editor *ed, long scroll_row, const BtnLangSpec *lang,
                       const size_t *match_starts, const size_t *match_ends, size_t match_count);

/* Bildet einen View-Punkt (Ursprung unten links, wie bei einer
 * nicht geflippten NSView) auf einen logischen Buffer-Offset ab,
 * unter Beruecksichtigung der aktuellen Scroll-Position. */
size_t btn_hit_test(Editor *ed, CGRect bounds, double x, double y, long scroll_row);

/* Vorlaeufiger Text einer Eingabemethode (siehe textinput.h): wird bis zum
 * Festschreiben als Overlay am Cursor des Ziels gezeichnet (kopiert).
 * caret = Cursor darin in Bytes. len 0 = keiner. */
enum { BTN_MARKED_NONE = 0, BTN_MARKED_SEARCH = 1, BTN_MARKED_REPLACE = 2, BTN_MARKED_DOCUMENT = 3 };
void btn_render_set_marked_text(const char *utf8, size_t len, size_t caret, int target);

/* Cursor-Rechteck in View-Koordinaten - dort oeffnet macOS das
 * Kandidatenfenster - fuer den Byte-Offset offset im Dokument (bounds =
 * Inhaltsbereich wie bei btn_render_frame()) bzw. im Suchen-/Ersetzen-Feld
 * (bounds = ganzes Fenster). */
CGRect btn_render_caret_rect(Editor *ed, CGRect bounds, long scroll_row, size_t offset);
CGRect btn_render_find_caret_rect(CGRect bounds, Editor *field, int replace_field, size_t offset);
/* Gesetzte Breite von UTF-8-Text in der Editor-Schrift (vorlaeufiger Text:
 * Position eines Zeichens darin fuer das Kandidatenfenster). */
double btn_render_text_width(const char *utf8, size_t len);

/* Wie viele Rows bei dieser Inhaltshoehe (ohne Tab-/Suchleiste) VOLL ueber
 * dem Footer sichtbar sind - mit demselben oberen Innenabstand, den
 * btn_render_frame() vor die erste Row setzt (mindestens 1). main.c's
 * Scroll-Klemmung und "Cursor sichtbar halten" rechnen damit. */
long btn_visible_row_capacity(double content_height);

/* Senkrechter Bereich der voll sichtbaren Rows (bounds wie bei
 * btn_render_frame()): Oberkante der ersten, Unterkante der letzten. Beim
 * Markieren darueber hinaus endet die Selektion an der Randzeile. */
void btn_text_rows_extent(CGRect bounds, double *top, double *bottom);

/* Scrollbalken: nur ein Knopf (keine Schiene) in einem Streifen dieser
 * Breite am rechten Rand der Textflaeche - der Text bricht davor um. */
#define BTN_SCROLLBAR_WIDTH 12.0
#define BTN_SCROLLBAR_MIN_KNOB 24.0
/* Rechteck des Knopfs fuer row_count Rows ab scroll_row (bounds wie bei
 * btn_render_frame()). Rueckgabe 0 = kein Knopf, alles passt ins Fenster. */
int btn_scrollbar_knob(CGRect bounds, size_t row_count, long scroll_row, CGRect *out_knob);
/* Umkehrung beim Ziehen: scroll_row fuer einen Knopf mit Oberkante knob_top
 * (geklemmt auf [0, row_count - Sichtkapazitaet]). */
long btn_scrollbar_row_for_knob_top(CGRect bounds, size_t row_count, double knob_top);
/* Knopf dunkler zeichnen, solange er gezogen wird. */
void btn_render_set_scrollbar_active(int active);

/* Flaechen, ueber denen der Mauszeiger ein I-Beam ist (View-Koordinaten):
 * Textbereich des Dokuments ohne Zeilennummern, Statuszeile und - falls
 * sichtbar (scrollbar_visible) - Scrollbalken; ohne Knopf ist der Streifen
 * normaler Text. Bei offener Suchleiste auch ihre beiden Felder. window =
 * ganzes Fenster, content = Inhaltsbereich wie bei btn_render_frame().
 * Rueckgabe: Anzahl in out (hoechstens 3). */
int btn_text_cursor_rects(CGRect window, CGRect content, int find_bar_visible, int scrollbar_visible, CGRect out[3]);

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
 * wie bei btn_draw_callback nicht geflippt (Ursprung unten links).
 * start_comment_state ist der Kommentar-Zustand VOR first_row (siehe
 * btn_compute_line_comment_states()) - wird bewusst vom Aufrufer
 * hereingereicht statt hier selbst von Zeile 0 an neu gescannt: die
 * Druckvorschau des Systemdialogs zeichnet Seiten nicht zwingend
 * aufsteigend (der Nutzer kann in der Vorschau zu einer beliebigen Seite
 * springen), ein Vorwaertsscan pro Seite waere sonst O(Seiten * Zeichen)
 * statt O(Zeichen) fuers gesamte Dokument. */
void btn_render_print_page(CGContextRef ctx, CGRect page_rect, Editor *ed, const BtnLangSpec *lang,
                            const BtnRow *rows, size_t row_count, size_t first_row,
                            int start_comment_state);

/* Berechnet fuer JEDE logische Zeile den Kommentar-Zustand VOR ihr, in einem
 * einzigen O(Zeichen)-Vorwaertsdurchlauf (wie comment_state_before_line()
 * intern fuer eine einzelne Zielzeile, hier aber fuer alle auf einmal).
 * out_states muss vom Aufrufer mit mindestens editor_line_count(ed) Eintraegen
 * allokiert sein (out_states[i] = Zustand vor logischer Zeile i). main.c
 * ruft das einmal pro Druckvorgang auf (siehe btn_render_print_page()), statt
 * den Zustand pro Seite einzeln nachzuscannen. */
void btn_compute_line_comment_states(Editor *ed, const BtnLangSpec *lang, int *out_states);

#ifdef __cplusplus
}
#endif

#endif /* BTN_RENDER_H */
