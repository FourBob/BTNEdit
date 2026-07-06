#ifndef BTN_SHIM_H
#define BTN_SHIM_H

#include <CoreGraphics/CoreGraphics.h>
#include <stddef.h>

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
    BTN_MENU_SELECT_ALL
};

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

void btn_app_init(void);
void btn_app_set_draw_callback(btn_draw_callback cb);
void btn_app_set_key_callback(btn_key_callback cb);
void btn_app_set_resize_callback(btn_resize_callback cb);
void btn_app_set_menu_callback(btn_menu_callback cb);
void btn_app_set_mouse_callback(btn_mouse_callback cb);
void btn_app_set_scroll_callback(btn_scroll_callback cb);
void btn_app_set_should_close_callback(btn_should_close_callback cb);
void btn_app_build_menu(void);
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

void btn_set_window_title(const char *title);
void btn_app_set_document_edited(int edited);
void btn_app_close_window(void);

#ifdef __cplusplus
}
#endif

#endif /* BTN_SHIM_H */
