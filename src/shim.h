#ifndef BTN_SHIM_H
#define BTN_SHIM_H

#include <CoreGraphics/CoreGraphics.h>
#include <stddef.h>
#include <stdint.h>
#include "strings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Menu item tags, shared between the ObjC shim and the C application code. */
enum {
    BTN_MENU_NEW = 1,
    BTN_MENU_OPEN,
    BTN_MENU_SAVE,
    BTN_MENU_SAVE_AS,
    BTN_MENU_CLOSE,
    BTN_MENU_PRINT,
    BTN_MENU_FIND,
    BTN_MENU_UNDO,
    BTN_MENU_REDO,
    BTN_MENU_CUT,
    BTN_MENU_COPY,
    BTN_MENU_PASTE,
    BTN_MENU_SELECT_ALL,
    BTN_MENU_HELP,
    BTN_MENU_GOTO_LINE,
    BTN_MENU_ZOOM_IN,
    BTN_MENU_ZOOM_OUT,
    BTN_MENU_ZOOM_RESET,
    /* Ablage > Zeilenenden - Reihenfolge wie BtnEol in eol.h
     * (BTN_MENU_EOL_LF + BTN_EOL_CRLF == BTN_MENU_EOL_CRLF). */
    BTN_MENU_EOL_LF,
    BTN_MENU_EOL_CRLF,
    BTN_MENU_EOL_CR,
    BTN_MENU_FIND_NEXT,
    BTN_MENU_FIND_PREVIOUS,
    BTN_MENU_USE_SELECTION_FOR_FIND,
    BTN_MENU_NEXT_TAB,
    BTN_MENU_PREVIOUS_TAB
};

/* Tags fuer die dynamischen "Zuletzt geoeffnet"-Menuepunkte liegen ab hier,
 * weit oberhalb der festen BTN_MENU_*-Werte oben, damit sich die beiden
 * Tag-Raeume nie ueberschneiden - main.c erkennt sie per tag >=
 * BTN_MENU_RECENT_BASE und leitet den Index (tag - BTN_MENU_RECENT_BASE)
 * in die eigene Recent-Files-Liste zurueck. */
#define BTN_MENU_RECENT_BASE 1000
#define BTN_MAX_RECENT_FILES 10

/* Rohe NSEvent.ModifierFlags-Bitwerte (von Apple dokumentiert/stabil), damit
 * main.c ohne Cocoa-Header auskommt. */
#define BTN_MOD_SHIFT   (1u << 17)
#define BTN_MOD_CONTROL (1u << 18)
#define BTN_MOD_OPTION  (1u << 19)
#define BTN_MOD_COMMAND (1u << 20)

typedef enum {
    BTN_MOUSE_DOWN,
    BTN_MOUSE_DRAGGED,
    BTN_MOUSE_UP
} btn_mouse_phase;

typedef void (*btn_draw_callback)(CGContextRef ctx, CGRect bounds);
typedef void (*btn_key_callback)(const char *characters, unsigned short keycode, unsigned long modifierFlags);
typedef void (*btn_resize_callback)(CGSize newSize);
typedef void (*btn_menu_callback)(int tag);
typedef void (*btn_mouse_callback)(btn_mouse_phase phase, double x, double y, int clickCount, unsigned long modifierFlags);
/* delta_y in Punkten, positiv = nach oben scrollen (Trackpad-"natural
 * scrolling" ist bereits vom System eingerechnet). */
typedef void (*btn_scroll_callback)(double delta_y);
/* Rueckgabe: 1 = Fenster schliessen/App beenden erlauben, 0 = abbrechen.
 * Wird sowohl vom roten Schliessen-Knopf (windowShouldClose:) als auch von
 * Cmd+Q/"Beende" (applicationShouldTerminate:) aufgerufen, damit keiner
 * dieser beiden System-Wege den Ungesichert-Dialog umgehen kann. */
typedef int (*btn_should_close_callback)(void);

/* Wird einmal pro Datei aufgerufen, die per "Oeffnen mit" (Finder), per
 * Doppelklick auf eine Datei eines registrierten Typs, oder per Drag&Drop
 * aufs Dock-Icon geoeffnet werden soll - sowohl beim Programmstart (App war
 * noch nicht offen) als auch waehrend die App bereits laeuft. path ist ein
 * absoluter Dateisystempfad (kein NSURL, main.c bleibt Cocoa-frei). */
typedef void (*btn_open_file_callback)(const char *path);

/* Eingabemethoden (NSTextInputClient, siehe textinput.h). keyDown: gibt
 * jede Taste an macOS (interpretKeyEvents:): fertiger Text - auch aus
 * Tottasten, Pinyin, Kana, der Emoji-Palette - kommt ueber insert_text;
 * Tasten ohne Text (Pfeile, Return, Tab, Backspace, Escape, Cmd-Kombinationen)
 * kommen unveraendert ueber den btn_key_callback. Bereiche sind UTF-16-
 * Einheiten relativ zu btn_ti_origin(); -1 = keiner (NSNotFound). */
typedef struct {
    /* Text festschreiben; repl_loc >= 0: ersetzt diesen Bereich (Akzent-
     * Menue beim Gedrueckthalten ersetzt das Zeichen vor dem Cursor). */
    void (*insert_text)(const char *utf8, long repl_loc, long repl_len);
    /* Vorlaeufigen Text setzen ("" = keiner mehr); sel_*: Cursor darin. */
    void (*set_marked_text)(const char *utf8, long sel_loc, long sel_len, long repl_loc, long repl_len);
    /* Vorlaeufigen Text so festschreiben, wie er ist. */
    void (*unmark_text)(void);
    /* Aktuelle Selektion und vorlaeufiger Bereich (marked_loc -1 = keiner). */
    void (*query)(long *sel_loc, long *sel_len, long *marked_loc, long *marked_len);
    /* Text eines Bereichs als UTF-16 (malloc, NULL = keiner). */
    uint16_t *(*substring)(long loc, long len, long *actual_loc, size_t *n);
    /* Rechteck der Position loc (-1 = Cursor) in View-Koordinaten - dort
     * oeffnet macOS das Kandidatenfenster bzw. das Akzent-Menue. */
    CGRect (*caret_rect)(long loc);
} BtnTextInputCallbacks;

/* Liest NSLocale.preferredLanguages (Systemeinstellung, nicht der App
 * eigene Auswahl - kein Sprachumschalter im Menue, ganz im Sinne der
 * schlanken Notepad.exe-Philosophie) und ordnet die bevorzugte Sprache
 * einer BtnUiLang zu. main() ruft das einmal beim Start auf, bevor
 * btn_strings_set_language() und btn_app_build_menu() folgen. */
BtnUiLang btn_app_detect_system_language(void);

void btn_app_init(void);
void btn_app_set_draw_callback(btn_draw_callback cb);
void btn_app_set_key_callback(btn_key_callback cb);
void btn_app_set_resize_callback(btn_resize_callback cb);
void btn_app_set_menu_callback(btn_menu_callback cb);
void btn_app_set_mouse_callback(btn_mouse_callback cb);
void btn_app_set_scroll_callback(btn_scroll_callback cb);
void btn_app_set_should_close_callback(btn_should_close_callback cb);
void btn_app_set_open_file_callback(btn_open_file_callback cb);
void btn_app_set_text_input_callbacks(const BtnTextInputCallbacks *cb);
/* Sagt der Eingabemethode, dass ihr vorlaeufiger Text verworfen ist (main.c
 * hat ihn selbst festgeschrieben, z.B. vor einem Tab-Wechsel). */
void btn_text_input_discard(void);
/* Nach Scrollen/Groessenaenderung: ein offenes Kandidatenfenster soll der
 * neuen Cursorposition folgen. */
void btn_text_input_invalidate(void);
void btn_app_build_menu(void);

/* Baut das "Zuletzt geoeffnet"-Untermenue komplett neu aus paths[0..count)
 * auf (paths[0] = neuester Eintrag). main.c ruft das nach jedem Laden/
 * Sichern und einmal beim Start auf; count == 0 zeigt einen deaktivierten
 * Platzhaltereintrag. count wird intern auf BTN_MAX_RECENT_FILES gekappt. */
void btn_app_set_recent_files(const char **paths, int count);

void btn_app_request_redraw(void);

/* Systemton (z.B. Weitersuchen ohne Treffer bei geschlossener Suchleiste). */
void btn_beep(void);

/* Setzt das Haekchen im Untermenue Ablage > Zeilenenden auf Eintrag index
 * (0 = LF, 1 = CRLF, 2 = CR, wie BtnEol; -1 = keins, z.B. bei gemischten
 * Zeilenenden). enabled = 0 sperrt die drei Eintraege (Binaerdatei). */
void btn_app_set_line_ending_menu(int index, int enabled);
void btn_app_run(void);

/* Systemweite Zwischenablage. btn_pasteboard_set_string nimmt bytes+len
 * (nicht NUL-terminiert, darf NUL enthalten): der Inhalt wird als UTF-8
 * uebernommen, bei ungueltigem UTF-8 (Latin-1-/Binaerdatei per "Trotzdem
 * oeffnen") als ISO-8859-1 - so wird nie stillschweigend nichts kopiert.
 * Rueckgabe 1, wenn die Zwischenablage den Text uebernommen hat, sonst 0;
 * Ausschneiden loescht die Selektion nur bei 1. btn_pasteboard_copy_string
 * gibt einen neu allokierten String zurueck (caller muss free() aufrufen)
 * und schreibt die tatsaechliche Byte-Laenge nach *out_len - wichtig, weil
 * strlen() bei einem eingebetteten NUL-Byte im Zwischenablage-Inhalt
 * vorzeitig abbrechen und den Rest stillschweigend verwerfen wuerde. */
int btn_pasteboard_set_string(const char *bytes, size_t len);
char *btn_pasteboard_copy_string(size_t *out_len);

/* Dupliziert einen NUL-terminierten C-String in einen neu allokierten
 * Puffer (caller muss free() aufrufen). Kleiner gemeinsamer Helfer, den
 * sowohl der Shim (Pfade aus NSOpenPanel/NSSavePanel) als auch main.c
 * (Dateipfad-Tracking) brauchen. */
char *btn_dup_cstring(const char *s);

/* Systemdialoge (NSOpenPanel/NSSavePanel/NSAlert) - wie NSWindow/NSMenu
 * reine Chrome/Systemdienste, kein Content-Widget. Die *_panel-Funktionen
 * geben einen neu allokierten Pfad zurueck (caller muss free() aufrufen)
 * oder NULL, wenn der Dialog abgebrochen wurde. */
char *btn_show_open_panel(void);
char *btn_show_save_panel(const char *suggested_path);

/* Rueckgabe: 0 = Abbrechen, 1 = Sichern, 2 = Nicht sichern. */
int btn_show_unsaved_changes_alert(const char *display_name);

/* Warnt vor dem Laden einer vermutlich binaeren Datei (main.c erkennt das
 * grob anhand eingebetteter NUL-Bytes, siehe looks_binary() dort) - der
 * Editor hat keine Erkennung/keinen Nur-Lese-Modus fuer solchen Inhalt,
 * ohne diese Warnung koennte ein Nutzer ihn versehentlich bearbeiten und
 * mit Cmd+S ueberschreiben. Rueckgabe: 1 = trotzdem oeffnen, 0 = abbrechen
 * (main.c laedt die Datei dann nicht). */
int btn_show_binary_file_warning(const char *display_name);

/* Zeigt einen Systemdialog (NSAlert mit einem Zahlen-Eingabefeld als
 * Accessory View, wie die anderen Alerts hier reine Chrome) zum Springen an
 * eine bestimmte Zeile. Rueckgabe: 1 = bestaetigt (dann steht die
 * eingegebene, auf [1, max_line] geklemmte Zeilennummer in *out_line), 0 =
 * abgebrochen (dann bleibt *out_line unveraendert). */
int btn_show_goto_line_dialog(long max_line, long *out_line);

/* Liefert 1, wenn die App aktuell im Dark-Mode-Erscheinungsbild dargestellt
 * wird (System- oder App-Einstellung), sonst 0. main.c fragt das bei jedem
 * Redraw ab und reicht es an render.c weiter (siehe btn_render_set_dark_mode()
 * in render.h) - kein Notification-Mechanismus noetig, weil ohnehin bei
 * jedem Tastendruck/Resize neu gezeichnet wird; BTNContentViews
 * -viewDidChangeEffectiveAppearance sorgt zusaetzlich fuer einen Redraw, wenn
 * sich das Erscheinungsbild AUCH OHNE Nutzeraktion aendert (z.B.
 * automatischer Wechsel bei Sonnenuntergang, waehrend die App im
 * Hintergrund ist). */
int btn_app_is_dark_mode(void);

/* Zeigt die Tastenkuerzel-Uebersicht (Hilfe-Menue) als NSAlert - wie die
 * anderen Systemdialoge reine Chrome, kein eigenes Content-Fenster noetig
 * fuer eine simple, statische Liste. */
void btn_show_help_alert(void);

/* Einfache Fehlermeldung mit OK-Knopf (z.B. Datei nicht lesbar/zu gross).
 * title/info duerfen ungueltiges UTF-8 enthalten (Latin-1-Fallback). */
void btn_show_error_alert(const char *title, const char *info);

void btn_set_window_title(const char *title);
void btn_app_set_document_edited(int edited);
void btn_app_close_window(void);

/* Wird von btn_print_pages() einmal pro Seite aufgerufen. page_rect nutzt
 * stets denselben (0,0)-basierten, nicht geflippten Koordinatenraum wie
 * btn_draw_callback (siehe btn_app_set_draw_callback) - der Shim verschiebt
 * die CTM intern pro Seite, damit main.c/render.c dieselbe Zeichenlogik wie
 * beim Bildschirm wiederverwenden koennen, ohne die tatsaechliche Position
 * auf dem langen Druck-View zu kennen. page_index ist 0-basiert. */
typedef void (*btn_print_page_callback)(CGContextRef ctx, CGRect page_rect, int page_index);

/* Liefert die Seitenanzahl fuer eine gegebene bedruckbare Flaeche (Papier-
 * format minus Systemraender) zurueck - main.c baut darin das Wortumbruch-/
 * Seiten-Layout (btn_layout_build + btn_rows_per_page) neu auf. Der Shim
 * ruft das aus BTNPrintView's -knowsPageRange: auf, was AppKit erst
 * aufruft, WAEHREND/NACHDEM der Nutzer im Systemdruckdialog Papierformat/
 * Ausrichtung/Raender gewaehlt hat (fuer dessen Live-Vorschau sogar
 * mehrfach) - anders als eine einmalige Berechnung VOR dem Dialog sieht das
 * Layout so immer die tatsaechlich gewaehlten Einstellungen. */
typedef int (*btn_print_layout_callback)(CGSize page_size);

/* Zeigt den System-Druckdialog (NSPrintOperation) und druckt danach.
 * layout_cb liefert die Seitenanzahl fuer eine gegebene Seitengroesse (siehe
 * btn_print_layout_callback), draw_cb zeichnet danach jede einzelne Seite
 * (siehe btn_print_page_callback). Reine Chrome wie die anderen
 * Systemdialoge - kennt weder Editor noch Zeilenumbruch selbst, reicht nur
 * Seitengroesse/CGContext/Seiten-Rect an main.c/render.c durch. */
void btn_print_pages(btn_print_layout_callback layout_cb, btn_print_page_callback draw_cb);

#ifdef __cplusplus
}
#endif

#endif /* BTN_SHIM_H */
