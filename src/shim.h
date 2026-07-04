#ifndef BTN_SHIM_H
#define BTN_SHIM_H

#include <CoreGraphics/CoreGraphics.h>

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
    BTN_MENU_FIND
};

typedef void (*btn_draw_callback)(CGContextRef ctx, CGRect bounds);
typedef void (*btn_key_callback)(const char *characters, unsigned short keycode, unsigned long modifierFlags);
typedef void (*btn_resize_callback)(CGSize newSize);
typedef void (*btn_menu_callback)(int tag);

void btn_app_init(void);
void btn_app_set_draw_callback(btn_draw_callback cb);
void btn_app_set_key_callback(btn_key_callback cb);
void btn_app_set_resize_callback(btn_resize_callback cb);
void btn_app_set_menu_callback(btn_menu_callback cb);
void btn_app_build_menu(void);
void btn_app_request_redraw(void);
void btn_app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* BTN_SHIM_H */
