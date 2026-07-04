/*
 * Duenner ObjC-Shim: nur Fenster, Menue und Event-Weiterleitung.
 * Kein NSTextView, kein NSButton, kein NSTabView - der eigentliche
 * Editor-Inhalt wird komplett in C ueber Core Graphics/Core Text gezeichnet
 * (siehe render.c). Kompiliert ohne ARC (-fno-objc-arc), manuelles
 * Retain/Release nur dort noetig, wo Objekte nicht ohnehin von einem
 * AppKit-Container (NSMenu, NSWindow) gehalten werden.
 */
#import <Cocoa/Cocoa.h>
#include "shim.h"

static btn_draw_callback g_draw_cb = NULL;
static btn_key_callback g_key_cb = NULL;
static btn_resize_callback g_resize_cb = NULL;
static btn_menu_callback g_menu_cb = NULL;

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
        [g_window setTitle:@"BTNEdit"];
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
