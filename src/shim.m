/*
 * Duenner ObjC-Shim: nur Fenster, Menue, Event-Weiterleitung und die
 * Systemzwischenablage (NSPasteboard ist wie NSWindow/NSMenu ein reiner
 * Systemdienst, kein Content-Widget). Kein NSTextView, kein NSButton, kein
 * NSTabView - der eigentliche Editor-Inhalt wird komplett in C ueber Core
 * Graphics/Core Text gezeichnet (siehe render.c). Kompiliert ohne ARC
 * (-fno-objc-arc).
 */
#import <Cocoa/Cocoa.h>
#include "shim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static btn_draw_callback g_draw_cb = NULL;
static btn_key_callback g_key_cb = NULL;
static btn_resize_callback g_resize_cb = NULL;
static btn_menu_callback g_menu_cb = NULL;
static btn_mouse_callback g_mouse_cb = NULL;
static btn_scroll_callback g_scroll_cb = NULL;
static btn_should_close_callback g_should_close_cb = NULL;

static NSWindow *g_window = nil;
static NSMenu *g_recentMenu = nil;

@interface BTNContentView : NSView
@end

@implementation BTNContentView

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (g_draw_cb) {
        CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
        g_draw_cb(ctx, NSRectToCGRect(self.bounds));
    }
}

- (void)keyDown:(NSEvent *)event {
    if (g_key_cb) {
        const char *chars = [[event characters] UTF8String];
        g_key_cb(chars, [event keyCode], (unsigned long)[event modifierFlags]);
    }
}

- (void)mouseDown:(NSEvent *)event {
    if (g_mouse_cb) {
        NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
        g_mouse_cb(BTN_MOUSE_DOWN, p.x, p.y, (int)[event clickCount], (unsigned long)[event modifierFlags]);
    }
}

- (void)mouseDragged:(NSEvent *)event {
    if (g_mouse_cb) {
        NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
        g_mouse_cb(BTN_MOUSE_DRAGGED, p.x, p.y, (int)[event clickCount], (unsigned long)[event modifierFlags]);
    }
}

- (void)mouseUp:(NSEvent *)event {
    if (g_mouse_cb) {
        NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
        g_mouse_cb(BTN_MOUSE_UP, p.x, p.y, (int)[event clickCount], (unsigned long)[event modifierFlags]);
    }
}

- (void)scrollWheel:(NSEvent *)event {
    if (g_scroll_cb) {
        g_scroll_cb([event scrollingDeltaY]);
    }
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    if (g_resize_cb) {
        g_resize_cb(NSSizeToCGSize(newSize));
    }
    [self setNeedsDisplay:YES];
}

@end

static BTNContentView *g_view = nil;

@interface BTNAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation BTNAppDelegate

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

- (BOOL)windowShouldClose:(id)sender {
    (void)sender;
    if (g_should_close_cb) {
        return g_should_close_cb() ? YES : NO;
    }
    return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    if (g_should_close_cb) {
        return g_should_close_cb() ? NSTerminateNow : NSTerminateCancel;
    }
    return NSTerminateNow;
}

@end

@interface BTNMenuTarget : NSObject
- (void)menuAction:(id)sender;
@end

@implementation BTNMenuTarget

- (void)menuAction:(id)sender {
    if (g_menu_cb) {
        NSMenuItem *item = (NSMenuItem *)sender;
        g_menu_cb((int)[item tag]);
    }
}

@end

static BTNMenuTarget *g_menuTarget = nil;

static void add_item(NSMenu *menu, NSString *title, NSString *key, int tag) {
    NSMenuItem *item = [menu addItemWithTitle:title action:@selector(menuAction:) keyEquivalent:key];
    [item setTarget:g_menuTarget];
    [item setTag:tag];
}

/* Kurzform fuer btn_tr() + Umwandlung in NSString - btn_tr() gibt bewusst
 * ein rohes C-const-char* zurueck (siehe strings.h), damit main.c dieselbe
 * Tabelle ohne Foundation nutzen kann; hier im Shim wird daraus erst bei
 * Bedarf ein NSString. */
static NSString *trs(BtnStringId id) {
    return [NSString stringWithUTF8String:btn_tr(id)];
}

BtnUiLang btn_app_detect_system_language(void) {
    @autoreleasepool {
        NSArray<NSString *> *preferred = [NSLocale preferredLanguages];
        NSString *code = preferred.count > 0 ? preferred[0] : @"en";
        return btn_strings_lang_from_code([code UTF8String]);
    }
}

void btn_app_init(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        g_menuTarget = [BTNMenuTarget new];
    }
}

void btn_app_set_draw_callback(btn_draw_callback cb) {
    g_draw_cb = cb;
}

void btn_app_set_key_callback(btn_key_callback cb) {
    g_key_cb = cb;
}

void btn_app_set_resize_callback(btn_resize_callback cb) {
    g_resize_cb = cb;
}

void btn_app_set_menu_callback(btn_menu_callback cb) {
    g_menu_cb = cb;
}

void btn_app_set_mouse_callback(btn_mouse_callback cb) {
    g_mouse_cb = cb;
}

void btn_app_set_scroll_callback(btn_scroll_callback cb) {
    g_scroll_cb = cb;
}

void btn_app_set_should_close_callback(btn_should_close_callback cb) {
    g_should_close_cb = cb;
}

void btn_app_build_menu(void) {
    @autoreleasepool {
        NSMenu *menubar = [NSMenu new];
        NSMenuItem *appMenuItem = [NSMenuItem new];
        [menubar addItem:appMenuItem];
        [NSApp setMainMenu:menubar];

        NSString *appName = @"BTNEdit";
        NSMenu *appMenu = [NSMenu new];
        [appMenu addItemWithTitle:[trs(BTN_STR_ABOUT_PREFIX) stringByAppendingString:appName] action:nil keyEquivalent:@""];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:[trs(BTN_STR_QUIT_PREFIX) stringByAppendingString:appName]
                            action:@selector(terminate:)
                     keyEquivalent:@"q"];
        [appMenuItem setSubmenu:appMenu];

        NSMenuItem *fileMenuItem = [NSMenuItem new];
        [menubar addItem:fileMenuItem];
        NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:trs(BTN_STR_FILE_MENU)];
        add_item(fileMenu, trs(BTN_STR_NEW), @"n", BTN_MENU_NEW);
        add_item(fileMenu, trs(BTN_STR_OPEN), @"o", BTN_MENU_OPEN);
        g_recentMenu = [[NSMenu alloc] initWithTitle:trs(BTN_STR_RECENT)];
        NSMenuItem *recentItem = [fileMenu addItemWithTitle:trs(BTN_STR_RECENT) action:nil keyEquivalent:@""];
        [recentItem setSubmenu:g_recentMenu];
        [fileMenu addItem:[NSMenuItem separatorItem]];
        add_item(fileMenu, trs(BTN_STR_SAVE), @"s", BTN_MENU_SAVE);
        add_item(fileMenu, trs(BTN_STR_SAVE_AS), @"S", BTN_MENU_SAVE_AS);
        [fileMenu addItem:[NSMenuItem separatorItem]];
        add_item(fileMenu, trs(BTN_STR_CLOSE), @"w", BTN_MENU_CLOSE);
        [fileMenu addItem:[NSMenuItem separatorItem]];
        add_item(fileMenu, trs(BTN_STR_PRINT), @"p", BTN_MENU_PRINT);
        [fileMenuItem setSubmenu:fileMenu];

        NSMenuItem *editMenuItem = [NSMenuItem new];
        [menubar addItem:editMenuItem];
        NSMenu *editMenu = [[NSMenu alloc] initWithTitle:trs(BTN_STR_EDIT_MENU)];
        add_item(editMenu, trs(BTN_STR_UNDO), @"z", BTN_MENU_UNDO);
        add_item(editMenu, trs(BTN_STR_REDO), @"Z", BTN_MENU_REDO);
        [editMenu addItem:[NSMenuItem separatorItem]];
        add_item(editMenu, trs(BTN_STR_CUT), @"x", BTN_MENU_CUT);
        add_item(editMenu, trs(BTN_STR_COPY), @"c", BTN_MENU_COPY);
        add_item(editMenu, trs(BTN_STR_PASTE), @"v", BTN_MENU_PASTE);
        add_item(editMenu, trs(BTN_STR_SELECT_ALL), @"a", BTN_MENU_SELECT_ALL);
        [editMenu addItem:[NSMenuItem separatorItem]];
        add_item(editMenu, trs(BTN_STR_FIND), @"f", BTN_MENU_FIND);
        [editMenuItem setSubmenu:editMenu];

        NSMenuItem *helpMenuItem = [NSMenuItem new];
        [menubar addItem:helpMenuItem];
        NSMenu *helpMenu = [[NSMenu alloc] initWithTitle:trs(BTN_STR_HELP_MENU)];
        add_item(helpMenu, trs(BTN_STR_HELP_SHORTCUTS), @"", BTN_MENU_HELP);
        [helpMenuItem setSubmenu:helpMenu];
    }
}

void btn_app_set_recent_files(const char **paths, int count) {
    @autoreleasepool {
        if (count > BTN_MAX_RECENT_FILES) {
            count = BTN_MAX_RECENT_FILES;
        }
        [g_recentMenu removeAllItems];
        for (int i = 0; i < count; i++) {
            NSString *full = [NSString stringWithUTF8String:paths[i]];
            NSMenuItem *item = [g_recentMenu addItemWithTitle:[full lastPathComponent]
                                                        action:@selector(menuAction:)
                                                 keyEquivalent:@""];
            [item setTarget:g_menuTarget];
            [item setTag:BTN_MENU_RECENT_BASE + i];
            [item setToolTip:full];
        }
        if (count == 0) {
            NSMenuItem *empty = [g_recentMenu addItemWithTitle:trs(BTN_STR_RECENT_EMPTY) action:nil keyEquivalent:@""];
            [empty setEnabled:NO];
        }
    }
}

void btn_app_request_redraw(void) {
    [g_view setNeedsDisplay:YES];
}

void btn_app_run(void) {
    @autoreleasepool {
        NSRect frame = NSMakeRect(200, 200, 900, 600);
        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        g_window = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:style
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
        [g_window setTitle:trs(BTN_STR_UNTITLED)];
        [g_window setMinSize:NSMakeSize(400, 300)];

        g_view = [[BTNContentView alloc] initWithFrame:frame];
        [g_window setContentView:g_view];
        [g_window makeFirstResponder:g_view];

        BTNAppDelegate *delegate = [BTNAppDelegate new];
        [NSApp setDelegate:delegate];
        [g_window setDelegate:delegate];

        [g_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
}

void btn_pasteboard_set_string(const char *utf8) {
    @autoreleasepool {
        NSString *s = [NSString stringWithUTF8String:utf8 ? utf8 : ""];
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        [pb setString:s forType:NSPasteboardTypeString];
    }
}

char *btn_pasteboard_copy_string(size_t *out_len) {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSString *s = [pb stringForType:NSPasteboardTypeString];
        if (!s) {
            char *empty = malloc(1);
            empty[0] = '\0';
            *out_len = 0;
            return empty;
        }
        /* Ueber NSData statt strlen(UTF8String): strlen wuerde bei einem
         * eingebetteten NUL-Byte im Zwischenablage-Inhalt vorzeitig
         * abbrechen und den Rest des Textes stillschweigend verwerfen. */
        NSData *data = [s dataUsingEncoding:NSUTF8StringEncoding];
        size_t len = [data length];
        char *out = malloc(len + 1);
        memcpy(out, [data bytes], len);
        out[len] = '\0';
        *out_len = len;
        return out;
    }
}

char *btn_dup_cstring(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    memcpy(out, s, len + 1);
    return out;
}

char *btn_show_open_panel(void) {
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        if ([panel runModal] == NSModalResponseOK) {
            NSURL *url = [[panel URLs] firstObject];
            return btn_dup_cstring([[url path] UTF8String]);
        }
        return NULL;
    }
}

char *btn_show_save_panel(const char *suggested_path) {
    @autoreleasepool {
        NSSavePanel *panel = [NSSavePanel savePanel];
        if (suggested_path) {
            NSString *s = [NSString stringWithUTF8String:suggested_path];
            [panel setDirectoryURL:[[NSURL fileURLWithPath:s] URLByDeletingLastPathComponent]];
            [panel setNameFieldStringValue:[s lastPathComponent]];
        } else {
            [panel setNameFieldStringValue:[trs(BTN_STR_UNTITLED) stringByAppendingString:@".txt"]];
        }
        if ([panel runModal] == NSModalResponseOK) {
            return btn_dup_cstring([[[panel URL] path] UTF8String]);
        }
        return NULL;
    }
}

int btn_show_unsaved_changes_alert(const char *display_name) {
    @autoreleasepool {
        /* snprintf statt NSString stringWithFormat:/%@, weil btn_tr()s
         * Format-String bewusst "%s" statt "%@" nutzt (main.c braucht
         * dieselbe Tabelle ohne Foundation, siehe strings.h). */
        char msg[512];
        snprintf(msg, sizeof(msg), btn_tr(BTN_STR_SAVE_PROMPT_TITLE_FMT),
                 display_name ? display_name : btn_tr(BTN_STR_UNTITLED));
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:msg]];
        [alert setInformativeText:trs(BTN_STR_SAVE_PROMPT_INFO)];
        [alert addButtonWithTitle:trs(BTN_STR_BTN_SAVE)];
        [alert addButtonWithTitle:trs(BTN_STR_BTN_DONT_SAVE)];
        [alert addButtonWithTitle:trs(BTN_STR_BTN_CANCEL)];
        NSModalResponse resp = [alert runModal];
        if (resp == NSAlertFirstButtonReturn) {
            return 1;
        }
        if (resp == NSAlertSecondButtonReturn) {
            return 2;
        }
        return 0;
    }
}

void btn_show_help_alert(void) {
    @autoreleasepool {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:trs(BTN_STR_HELP_TITLE)];
        [alert setInformativeText:trs(BTN_STR_HELP_BODY)];
        [alert runModal];
    }
}

void btn_set_window_title(const char *title) {
    @autoreleasepool {
        [g_window setTitle:[NSString stringWithUTF8String:title ? title : btn_tr(BTN_STR_UNTITLED)]];
    }
}

void btn_app_set_document_edited(int edited) {
    [g_window setDocumentEdited:edited ? YES : NO];
}

void btn_app_close_window(void) {
    /* performClose: (nicht close!) simuliert den Klick auf den roten
     * Knopf und loest dabei windowShouldClose: aus - [g_window close]
     * wuerde den Delegate-Check stillschweigend umgehen und damit den
     * Ungesichert-Dialog fuer Datei > Schliessen/Cmd+W ausser Kraft
     * setzen, obwohl der rote Knopf selbst (der intern performClose:
     * nutzt) weiterhin fragen wuerde. */
    [g_window performClose:nil];
}
