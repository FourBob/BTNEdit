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
static btn_open_file_callback g_open_file_cb = NULL;

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

/* Gemeinsamer Aktivierungs-/Vordergrund-Code fuer alle drei Aufrufstellen
 * (applicationDidFinishLaunching:, application:openURLs: und frueher auch
 * btn_app_run() selbst - siehe Kommentar bei applicationDidFinishLaunching:
 * unten, warum Ersteres allein inzwischen genuegt). */
static void btn_activate_and_focus_window(void) {
    [NSApp activateIgnoringOtherApps:YES];
    [g_window makeKeyAndOrderFront:nil];
}

@interface BTNAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation BTNAppDelegate

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

/* Bringt die App zuverlaessig in den Vordergrund, wenn sie per Doppelklick/
 * "Oeffnen mit" auf eine Datei kaltgestartet wurde. Dies ist der von Apple
 * dafuer vorgesehene Zeitpunkt (im Gegensatz zu einem Aufruf VOR [NSApp run]
 * in btn_app_run(), der bei diesem Startweg manchmal zu frueh kommt, um noch
 * zu wirken, weil die App zu dem Zeitpunkt aus Sicht des Systems ihren Start
 * noch nicht abgeschlossen hat - btn_app_run() verlaesst sich deshalb
 * ausschliesslich auf diese Methode statt zusaetzlich selbst zu aktivieren);
 * application:openURLs: (siehe unten) aktiviert zusaetzlich noch einmal fuer
 * den Fall, dass die Datei erst nach dem Start-Ereignis eintrifft. */
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    btn_activate_and_focus_window();
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

/* Wird von "Oeffnen mit"/Doppelklick auf eine registrierte Dateiendung/
 * Drag&Drop aufs Dock-Icon ausgeloest - sowohl beim Programmstart (macOS
 * sammelt die Datei(en) aus dem Start-Kontext und liefert sie hierueber statt
 * ueber argv) als auch waehrend die App bereits laeuft. Reicht jede Datei
 * einzeln an main.c durch (g_open_file_cb), das dieselbe open_path_in_tab()-
 * Logik wie Datei > Oeffnen... nutzt. */
- (void)application:(NSApplication *)app openURLs:(NSArray<NSURL *> *)urls {
    (void)app;
    if (!g_open_file_cb) {
        return;
    }
    for (NSURL *url in urls) {
        if (![url isFileURL]) {
            continue;
        }
        const char *path = [[url path] UTF8String];
        if (path) {
            g_open_file_cb(path);
        }
    }
    btn_activate_and_focus_window();
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

void btn_app_set_open_file_callback(btn_open_file_callback cb) {
    g_open_file_cb = cb;
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
        /* Breite 800 statt z.B. 400: die Suchen/Ersetzen-Leiste (render.c)
         * braucht bei sichtbarem "Alle ersetzen"-Knopf ueber 750pt, bevor
         * ueberhaupt der Statustext anfaengt - bei einer kleineren Mindest-
         * breite waeren Knopf und/oder Statustext im schmalsten Fenster
         * abgeschnitten. shim.m kennt render.h's Layout-Konstanten bewusst
         * nicht (reine Chrome), daher hier als grosszuegig bemessener,
         * eigener Wert statt eines Verweises auf sie. */
        [g_window setMinSize:NSMakeSize(800, 300)];

        g_view = [[BTNContentView alloc] initWithFrame:frame];
        [g_window setContentView:g_view];
        [g_window makeFirstResponder:g_view];

        BTNAppDelegate *delegate = [BTNAppDelegate new];
        [NSApp setDelegate:delegate];
        [g_window setDelegate:delegate];

        /* Kein eigener makeKeyAndOrderFront:/activateIgnoringOtherApps:-Aufruf
         * hier noetig - delegate's applicationDidFinishLaunching: (oben)
         * erledigt genau das zuverlaessig, sobald [NSApp run] unten den
         * Start-Vorgang abschliesst. */
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

int btn_show_binary_file_warning(const char *display_name) {
    @autoreleasepool {
        char msg[512];
        snprintf(msg, sizeof(msg), btn_tr(BTN_STR_BINARY_WARNING_TITLE_FMT),
                 display_name ? display_name : btn_tr(BTN_STR_UNTITLED));
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:msg]];
        [alert setInformativeText:trs(BTN_STR_BINARY_WARNING_INFO)];
        [alert addButtonWithTitle:trs(BTN_STR_BTN_OPEN_ANYWAY)];
        [alert addButtonWithTitle:trs(BTN_STR_BTN_CANCEL)];
        NSModalResponse resp = [alert runModal];
        return resp == NSAlertFirstButtonReturn ? 1 : 0;
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

/* Bedruckbare Flaeche (Papierformat minus Systemraender) von info - eigene
 * Funktion statt Duplikat, weil sowohl BTNPrintView (live, pro Aufruf von
 * -knowsPageRange:) als auch eine etwaige spaetere Vorschau dieselbe
 * Umrechnung brauchen. */
static CGSize page_size_from_print_info(NSPrintInfo *info) {
    NSSize paper = [info paperSize];
    double width = paper.width - [info leftMargin] - [info rightMargin];
    double height = paper.height - [info topMargin] - [info bottomMargin];
    return CGSizeMake(width > 1.0 ? width : 1.0, height > 1.0 ? height : 1.0);
}

/* Reiner Druck-View: kennt weder Editor noch Zeilenumbruch, reicht beim
 * Zeichnen nur CGContext + Seiten-Rect an den C-Callback durch - genau wie
 * BTNContentView oben es fuer den Bildschirm mit g_draw_cb tut. Nicht
 * geflippt (wie BTNContentView), Seite 1 liegt oben im (langen) Gesamtview,
 * die letzte Seite unten.
 *
 * knowsPageRange: fragt bewusst [[NSPrintOperation currentOperation]
 * printInfo] ab (statt eine beim Aufbau des Views fest uebergebene
 * Seitengroesse zu benutzen) und ruft layoutCb JEDES Mal frisch auf: AppKit
 * ruft diese Methode nicht nur einmal auf, sondern wiederholt waehrend der
 * Nutzer im Systemdruckdialog Papierformat/Ausrichtung/Raender aendert (fuer
 * dessen Live-Vorschau) und ein letztes Mal nach der Bestaetigung - eine vor
 * dem Dialog fest berechnete Seitengroesse/Seitenanzahl wuerde sonst bei
 * einer im Dialog geaenderten Einstellung nicht mehr zum tatsaechlich
 * bedruckten Papier passen. */
@interface BTNPrintView : NSView {
@public
    btn_print_page_callback drawCb;
    btn_print_layout_callback layoutCb;
    int pageCount;
    CGSize pageSize;
}
@end

@implementation BTNPrintView

- (BOOL)knowsPageRange:(NSRangePointer)range {
    NSPrintInfo *info = [[NSPrintOperation currentOperation] printInfo];
    pageSize = page_size_from_print_info(info);
    pageCount = layoutCb ? layoutCb(pageSize) : 1;
    if (pageCount < 1) {
        pageCount = 1;
    }
    [self setFrameSize:NSMakeSize(pageSize.width, (double)pageCount * pageSize.height)];
    range->location = 1;
    range->length = pageCount;
    return YES;
}

- (NSRect)rectForPage:(NSInteger)page {
    double totalHeight = (double)pageCount * pageSize.height;
    double top = totalHeight - (double)page * pageSize.height;
    return NSMakeRect(0, top, pageSize.width, pageSize.height);
}

- (void)drawRect:(NSRect)dirtyRect {
    if (!drawCb) {
        return;
    }
    /* dirtyRect ist hier stets exakt das von rectForPage: gelieferte Rect
     * (eine Seite) - CTM so verschieben, dass der Callback wie gewohnt bei
     * (0,0) beginnende Seitenkoordinaten sieht (siehe btn_print_page_callback
     * in shim.h). */
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, dirtyRect.origin.x, dirtyRect.origin.y);
    double totalHeight = (double)pageCount * pageSize.height;
    int page_index = (int)(((totalHeight - dirtyRect.origin.y) / pageSize.height) - 1.0 + 0.5);
    drawCb(ctx, CGRectMake(0, 0, pageSize.width, pageSize.height), page_index);
    CGContextRestoreGState(ctx);
}

@end

void btn_print_pages(btn_print_layout_callback layout_cb, btn_print_page_callback draw_cb) {
    @autoreleasepool {
        /* Startgroesse ist nur ein Platzhalter - knowsPageRange: setzt Frame/
         * Seitengroesse/-anzahl neu, sobald AppKit sie braucht (siehe oben). */
        BTNPrintView *view = [[BTNPrintView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
        view->drawCb = draw_cb;
        view->layoutCb = layout_cb;
        view->pageCount = 1;
        view->pageSize = CGSizeMake(1, 1);

        NSPrintOperation *op = [NSPrintOperation printOperationWithView:view];
        [op setShowsPrintPanel:YES];
        [op runOperation];
        [view release];
    }
}
