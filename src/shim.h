#ifndef BTN_SHIM_H
#define BTN_SHIM_H

#include <CoreGraphics/CoreGraphics.h>
#include <stddef.h>
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
    BTN_MENU_HELP
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
void btn_app_build_menu(void);

/* Baut das "Zuletzt geoeffnet"-Untermenue komplett neu aus paths[0..count)
 * auf (paths[0] = neuester Eintrag). main.c ruft das nach jedem Laden/
 * Sichern und einmal beim Start auf; count == 0 zeigt einen deaktivierten
 * Platzhaltereintrag. count wird intern auf BTN_MAX_RECENT_FILES gekappt. */
void btn_app_set_recent_files(const char **paths, int count);

void btn_app_request_redraw(void);
void btn_app_run(void);

/* Systemweite Zwischenablage. btn_pasteboard_copy_string gibt einen neu
 * allokierten String zurueck (caller muss free() aufrufen) und schreibt
 * die tatsaechliche Byte-Laenge nach *out_len - wichtig, weil strlen() bei
 * einem eingebetteten NUL-Byte im Zwischenablage-Inhalt vorzeitig abbrechen
 * und den Rest stillschweigend verwerfen wuerde. */
void btn_pasteboard_set_string(const char *utf8);
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

/* Zeigt die Tastenkuerzel-Uebersicht (Hilfe-Menue) als NSAlert - wie die
 * anderen Systemdialoge reine Chrome, kein eigenes Content-Fenster noetig
 * fuer eine simple, statische Liste. */
void btn_show_help_alert(void);

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
