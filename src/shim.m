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
#include <stdlib.h>
#include <string.h>

static btn_draw_callback g_draw_cb = NULL;
static btn_key_callback g_key_cb = NULL;
static btn_resize_callback g_resize_cb = NULL;
static btn_menu_callback g_menu_cb = NULL;
static btn_mouse_callback g_mouse_cb = NULL;

static NSWindow *g_window = nil;

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

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    if (g_resize_cb) {
        g_resize_cb(NSSizeToCGSize(newSize));
    }
    [self setNeedsDisplay:YES];
}

@end

static BTNContentView *g_view = nil;

@interface BTNAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation BTNAppDelegate

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
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

void btn_app_build_menu(void) {
    @autoreleasepool {
        NSMenu *menubar = [NSMenu new];
        NSMenuItem *appMenuItem = [NSMenuItem new];
        [menubar addItem:appMenuItem];
        [NSApp setMainMenu:menubar];

        NSString *appName = @"BTNEdit";
        NSMenu *appMenu = [NSMenu new];
        [appMenu addItemWithTitle:[@"Ueber " stringByAppendingString:appName] action:nil keyEquivalent:@""];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:[@"Beende " stringByAppendingString:appName]
                            action:@selector(terminate:)
                     keyEquivalent:@"q"];
        [appMenuItem setSubmenu:appMenu];

        NSMenuItem *fileMenuItem = [NSMenuItem new];
        [menubar addItem:fileMenuItem];
        NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"Ablage"];
        add_item(fileMenu, @"Neu", @"n", BTN_MENU_NEW);
        add_item(fileMenu, @"Oeffnen...", @"o", BTN_MENU_OPEN);
        [fileMenu addItem:[NSMenuItem separatorItem]];
        add_item(fileMenu, @"Sichern", @"s", BTN_MENU_SAVE);
        add_item(fileMenu, @"Sichern unter...", @"S", BTN_MENU_SAVE_AS);
        [fileMenu addItem:[NSMenuItem separatorItem]];
        add_item(fileMenu, @"Schliessen", @"w", BTN_MENU_CLOSE);
        [fileMenu addItem:[NSMenuItem separatorItem]];
        add_item(fileMenu, @"Drucken...", @"p", BTN_MENU_PRINT);
        [fileMenuItem setSubmenu:fileMenu];

        NSMenuItem *editMenuItem = [NSMenuItem new];
        [menubar addItem:editMenuItem];
        NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Bearbeiten"];
        add_item(editMenu, @"Widerrufen", @"z", BTN_MENU_UNDO);
        add_item(editMenu, @"Wiederholen", @"Z", BTN_MENU_REDO);
        [editMenu addItem:[NSMenuItem separatorItem]];
        add_item(editMenu, @"Ausschneiden", @"x", BTN_MENU_CUT);
        add_item(editMenu, @"Kopieren", @"c", BTN_MENU_COPY);
        add_item(editMenu, @"Einfuegen", @"v", BTN_MENU_PASTE);
        add_item(editMenu, @"Alles auswaehlen", @"a", BTN_MENU_SELECT_ALL);
        [editMenu addItem:[NSMenuItem separatorItem]];
        add_item(editMenu, @"Suchen...", @"f", BTN_MENU_FIND);
        [editMenuItem setSubmenu:editMenu];
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
        [g_window setTitle:@"Unbenannt"];
        [g_window setMinSize:NSMakeSize(400, 300)];

        g_view = [[BTNContentView alloc] initWithFrame:frame];
        [g_window setContentView:g_view];
        [g_window makeFirstResponder:g_view];

        BTNAppDelegate *delegate = [BTNAppDelegate new];
        [NSApp setDelegate:delegate];

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

char *btn_pasteboard_copy_string(void) {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSString *s = [pb stringForType:NSPasteboardTypeString];
        const char *utf8 = s ? [s UTF8String] : "";
        size_t len = strlen(utf8);
        char *out = malloc(len + 1);
        memcpy(out, utf8, len + 1);
        return out;
    }
}

static char *copy_cstring(const char *utf8) {
    size_t len = strlen(utf8);
    char *out = malloc(len + 1);
    memcpy(out, utf8, len + 1);
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
            return copy_cstring([[url path] UTF8String]);
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
            [panel setNameFieldStringValue:@"Unbenannt.txt"];
        }
        if ([panel runModal] == NSModalResponseOK) {
            return copy_cstring([[[panel URL] path] UTF8String]);
        }
        return NULL;
    }
}

int btn_show_unsaved_changes_alert(const char *display_name) {
    @autoreleasepool {
        NSString *name = [NSString stringWithUTF8String:display_name ? display_name : "Unbenannt"];
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithFormat:@"Moechtest du die Aenderungen an „%@“ sichern?", name]];
        [alert setInformativeText:@"Deine Aenderungen gehen verloren, wenn du sie nicht sicherst."];
        [alert addButtonWithTitle:@"Sichern"];
        [alert addButtonWithTitle:@"Nicht sichern"];
        [alert addButtonWithTitle:@"Abbrechen"];
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

void btn_set_window_title(const char *title) {
    @autoreleasepool {
        [g_window setTitle:[NSString stringWithUTF8String:title ? title : "Unbenannt"]];
    }
}

void btn_app_set_document_edited(int edited) {
    [g_window setDocumentEdited:edited ? YES : NO];
}

void btn_app_close_window(void) {
    [g_window close];
}
