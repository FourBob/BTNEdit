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
static btn_void_callback g_launch_cb = NULL;
static btn_void_callback g_activate_cb = NULL;
static BtnTextInputCallbacks g_ti;
static BOOL g_ti_set = NO;

static NSWindow *g_window = nil;
static NSMenu *g_recentMenu = nil;
/* Die drei Eintraege von Ablage > Zeilenenden (Index wie BtnEol), fuer das
 * Haekchen per btn_app_set_line_ending_menu(). */
static NSMenuItem *g_eolItems[3];
static BOOL g_eolMenuEnabled = YES;

/* Maustaste gedrueckt und ihre letzte Position (View-Koordinaten) - der
 * Autoscroll-Takt schickt sie erneut, solange die Maus still steht. */
static BOOL g_mouse_down = NO;
static NSPoint g_last_mouse;
static NSTimer *g_autoscroll_timer = nil; /* gehalten von der Run Loop */
static int g_test_button_state = -1;       /* btn_shim_test_set_mouse_button() */

/* I-Beam-Flaechen, siehe btn_app_set_text_cursor_rects() */
#define BTN_MAX_CURSOR_RECTS 4
static CGRect g_cursor_rects[BTN_MAX_CURSOR_RECTS];
static int g_cursor_rect_count = 0;

static void stop_autoscroll(void) {
    [g_autoscroll_timer invalidate];
    g_autoscroll_timer = nil;
}

/* "Oeffnen mit", Dock-Icon und Ablegen im Fenster: jede Datei einzeln an
 * main.c (dieselbe Logik wie Ablage > Oeffnen...). */
static void open_file_urls(NSArray<NSURL *> *urls) {
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
}

/* Nur Datei-URLs (keine Web-Links, kein Text) */
static NSArray<NSURL *> *dropped_file_urls(id<NSDraggingInfo> info) {
    return [[info draggingPasteboard] readObjectsForClasses:@[ [NSURL class] ]
                                                    options:@{ NSPasteboardURLReadingFileURLsOnlyKey : @YES }];
}

/* Gibt ein Tasten-Event wie frueher an main.c weiter (Keycode, Modifier und
 * characters) - fuer alles, was keinen Text erzeugt. */
static void forward_key_event(NSEvent *event) {
    if (!g_key_cb) {
        return;
    }
    NSString *characters = [event characters];
    const char *chars = [characters UTF8String];
    /* Funktions-/Navigationstasten (F1-F12, Bild auf/ab, Hilfe, Pfeile,
     * ...) liefern in [event characters] Codepunkte aus dem von AppKit
     * reservierten Bereich U+F700-U+F7FF (NSUpArrowFunctionKey bis
     * NSModeSwitchFunctionKey = U+F747). Bewusst NICHT bis U+F8FF: das
     * Apple-Logo (Wahl+Umschalt+K) ist U+F8FF und ein echtes, tippbares
     * Zeichen. Ohne Filter wuerde main.c alles, was es nicht per Keycode
     * kennt, wie normalen Text einfuegen: ein unsichtbares 3-Byte-Zeichen,
     * und das Dokument gilt als geaendert. Leeren String statt das Event zu
     * schlucken - Keycode/Modifier gehen unveraendert weiter, damit
     * Pfeile/Pos1/Ende usw. in main.c ueber den Keycode funktionieren. */
    if ([characters length] > 0) {
        unichar first = [characters characterAtIndex:0];
        if (first >= 0xF700 && first <= 0xF7FF) {
            chars = "";
        }
    }
    g_key_cb(chars ? chars : "", [event keyCode], (unsigned long)[event modifierFlags]);
}

static long ns_loc(NSRange r) {
    return r.location == NSNotFound ? -1 : (long)r.location;
}

/* Eigene View statt NSTextView (siehe Kopfkommentar), aber mit
 * NSTextInputClient: nur so funktionieren Tottasten (^ ´ ` auf der
 * deutschen Tastatur), Eingabemethoden (Pinyin, Kana), die Emoji-Palette
 * und das Akzent-Menue beim Gedrueckthalten einer Taste. Die Logik dahinter
 * liegt in main.c/textinput.c; hier wird nur uebersetzt. */
@interface BTNContentView : NSView <NSTextInputClient> {
    NSEvent *_keyEvent; /* das Event, das gerade interpretKeyEvents: durchlaeuft */
    BOOL _keyForwarded; /* schon an main.c weitergereicht (hoechstens einmal pro Event) */
}
@end

@implementation BTNContentView

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    }
    return self;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)resetCursorRects {
    for (int i = 0; i < g_cursor_rect_count; i++) {
        [self addCursorRect:NSRectFromCGRect(g_cursor_rects[i]) cursor:[NSCursor IBeamCursor]];
    }
}

/* Dateien aus dem Finder ins Fenster ziehen: jede wird in einem Tab
 * geoeffnet. Text (z.B. aus einem Browser) wird nicht angenommen. */
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return (g_open_file_cb && [dropped_file_urls(sender) count] > 0) ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray<NSURL *> *urls = dropped_file_urls(sender);
    if (!g_open_file_cb || [urls count] == 0) {
        return NO;
    }
    /* Erst nach dem Ende der Drag-Sitzung oeffnen: eine Fehlermeldung
     * (Datei zu gross, Ordner, ...) waere sonst ein modaler Dialog mitten
     * im Ablegen, und der Finder wartete so lange auf das Ende. */
    dispatch_async(dispatch_get_main_queue(), ^{
        open_file_urls(urls);
        [NSApp activateIgnoringOtherApps:YES];
        [[self window] makeKeyAndOrderFront:nil];
    });
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
    [NSCursor setHiddenUntilMouseMoves:YES]; /* wie in TextEdit beim Tippen */
    if (!g_ti_set) {
        forward_key_event(event); /* ohne Eingabemethoden-Callbacks wie frueher */
        return;
    }
    /* Cmd+Taste ohne Menuepunkt ist ein Befehl, kein Text - direkt wie
     * frueher. Nur waehrend einer Eingabe geht sie durch die Eingabemethode,
     * damit die ihren Text erst festschreiben kann. */
    if (([event modifierFlags] & NSEventModifierFlagCommand) && ![self hasMarkedText]) {
        forward_key_event(event);
        return;
    }
    /* macOS entscheidet: Text (insertText:/setMarkedText:) oder Befehl
     * (doCommandBySelector:). Verschachtelt moeglich, daher sichern. */
    NSEvent *previous = _keyEvent;
    BOOL previous_forwarded = _keyForwarded;
    _keyEvent = event;
    _keyForwarded = NO;
    [self interpretKeyEvents:@[ event ]];
    _keyEvent = previous;
    _keyForwarded = previous_forwarded;
}

/* Taste ohne Text: das Original-Event an main.c, das Pfeile, Return, Tab,
 * Backspace, Escape usw. per Keycode behandelt wie bisher. Hoechstens
 * einmal pro Event: manche Tasten sind in macOS an zwei Befehle gebunden
 * (Wahl+Pfeil hoch = moveBackward: + moveToBeginningOfParagraph:) und
 * wuerden sonst doppelt ausgefuehrt. */
- (void)doCommandBySelector:(SEL)selector {
    (void)selector;
    if (_keyEvent && !_keyForwarded) {
        _keyForwarded = YES;
        forward_key_event(_keyEvent);
    }
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    NSString *s = [string isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString *)string string] : string;
    /* Tasten ohne Belegung (F1-F19, Hilfe, Loeschen auf dem Ziffernblock)
     * kommen als ein Zeichen aus dem AppKit-Bereich U+F700-U+F7FF - das ist
     * kein Text (siehe forward_key_event()). */
    if ([s length] == 1 && ![self hasMarkedText]) {
        unichar c = [s characterAtIndex:0];
        if (c >= 0xF700 && c <= 0xF7FF) {
            if (_keyEvent && !_keyForwarded) {
                _keyForwarded = YES;
                forward_key_event(_keyEvent);
            }
            return;
        }
    }
    const char *utf8 = [s UTF8String];
    if (utf8 && g_ti.insert_text) {
        g_ti.insert_text(utf8, ns_loc(replacementRange), (long)replacementRange.length);
    }
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {
    NSString *s = [string isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString *)string string] : string;
    const char *utf8 = [s UTF8String];
    if (g_ti.set_marked_text) {
        g_ti.set_marked_text(utf8 ? utf8 : "", ns_loc(selectedRange), (long)selectedRange.length,
                             ns_loc(replacementRange), (long)replacementRange.length);
    }
}

- (void)unmarkText {
    if (g_ti.unmark_text) {
        g_ti.unmark_text();
    }
}

- (NSRange)selectedRange {
    long sel_loc = 0, sel_len = 0, mk_loc = -1, mk_len = 0;
    if (g_ti.query) {
        g_ti.query(&sel_loc, &sel_len, &mk_loc, &mk_len);
    }
    return NSMakeRange((NSUInteger)sel_loc, (NSUInteger)sel_len);
}

- (NSRange)markedRange {
    long sel_loc = 0, sel_len = 0, mk_loc = -1, mk_len = 0;
    if (g_ti.query) {
        g_ti.query(&sel_loc, &sel_len, &mk_loc, &mk_len);
    }
    return mk_loc < 0 ? NSMakeRange(NSNotFound, 0) : NSMakeRange((NSUInteger)mk_loc, (NSUInteger)mk_len);
}

- (BOOL)hasMarkedText {
    return [self markedRange].location != NSNotFound;
}

- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    if (!g_ti.substring || range.location == NSNotFound) {
        return nil;
    }
    long actual_loc = 0;
    size_t n = 0;
    uint16_t *u16 = g_ti.substring((long)range.location, (long)range.length, &actual_loc, &n);
    if (!u16) {
        return nil;
    }
    NSString *str = [[[NSString alloc] initWithCharacters:(const unichar *)u16 length:n] autorelease];
    free(u16);
    if (actualRange) {
        *actualRange = NSMakeRange((NSUInteger)actual_loc, [str length]);
    }
    return [[[NSAttributedString alloc] initWithString:str] autorelease];
}

- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText {
    return @[];
}

/* Bildschirm-Rechteck des Cursors - dort oeffnet macOS das
 * Kandidatenfenster bzw. das Akzent-Menue. */
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    if (actualRange) {
        *actualRange = range;
    }
    NSRect r = g_ti.caret_rect ? NSRectFromCGRect(g_ti.caret_rect(ns_loc(range))) : NSZeroRect;
    NSWindow *window = [self window];
    if (!window) {
        return r;
    }
    r = [self convertRect:r toView:nil];
    return [window convertRectToScreen:r];
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    (void)point;
    return NSNotFound;
}

- (void)mouseDown:(NSEvent *)event {
    g_mouse_down = YES;
    g_last_mouse = [self convertPoint:[event locationInWindow] fromView:nil];
    if (g_mouse_cb) {
        g_mouse_cb(BTN_MOUSE_DOWN, g_last_mouse.x, g_last_mouse.y, (int)[event clickCount],
                   (unsigned long)[event modifierFlags]);
    }
}

- (void)mouseDragged:(NSEvent *)event {
    g_last_mouse = [self convertPoint:[event locationInWindow] fromView:nil];
    if (g_mouse_cb) {
        g_mouse_cb(BTN_MOUSE_DRAGGED, g_last_mouse.x, g_last_mouse.y, (int)[event clickCount],
                   (unsigned long)[event modifierFlags]);
    }
}

- (void)mouseUp:(NSEvent *)event {
    g_mouse_down = NO;
    stop_autoscroll();
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

/* Standard-Editieraktionen (Undo/Redo/Cut/Copy/Paste/SelectAll) - werden
 * ueber die Menue-Eintraege mit target:nil aufgerufen (siehe
 * btn_app_build_menu() weiter unten), NICHT ueber den eigenen menuAction:/
 * Tag-Mechanismus wie der Rest des Menues. Grund: bei target:nil schickt
 * AppKit die Aktion die Responder-Chain hoch und liefert sie an das jeweils
 * TATSAECHLICH fokussierte Objekt aus - ist das diese View (der normale
 * Fall, waehrend ein Dokument/die Suchleiste editiert wird), landen wir
 * hier und reichen es wie gewohnt an main.c weiter; ist stattdessen z.B.
 * das Dateinamensfeld eines nativen NSSavePanel fokussiert, greift dessen
 * eigene eingebaute Editier-Logik, OHNE dass diese View ueberhaupt beteiligt
 * ist. Mit dem alten festen target/@selector(menuAction:) haette das Menue
 * Cmd+C/V/X/Z/A IMMER zuerst abgefangen (AppKit prueft Menue-Tastenkuerzel
 * vor der eigentlichen keyDown:-Zustellung) und dabei stets auf main.c's
 * eigenem Editor-Zustand gearbeitet, selbst wenn currently ein natives
 * Cocoa-Textfeld wie das Speichern-Dialog-Feld den echten Tastaturfokus
 * hatte - das native Feld haette Cmd+C/V/X nie zu sehen bekommen. */
- (void)cut:(id)sender {
    (void)sender;
    if (g_menu_cb) {
        g_menu_cb(BTN_MENU_CUT);
    }
}

- (void)copy:(id)sender {
    (void)sender;
    if (g_menu_cb) {
        g_menu_cb(BTN_MENU_COPY);
    }
}

- (void)paste:(id)sender {
    (void)sender;
    if (g_menu_cb) {
        g_menu_cb(BTN_MENU_PASTE);
    }
}

- (void)selectAll:(id)sender {
    (void)sender;
    if (g_menu_cb) {
        g_menu_cb(BTN_MENU_SELECT_ALL);
    }
}

- (void)undo:(id)sender {
    (void)sender;
    if (g_menu_cb) {
        g_menu_cb(BTN_MENU_UNDO);
    }
}

- (void)redo:(id)sender {
    (void)sender;
    if (g_menu_cb) {
        g_menu_cb(BTN_MENU_REDO);
    }
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    if (g_resize_cb) {
        g_resize_cb(NSSizeToCGSize(newSize));
    }
    [self setNeedsDisplay:YES];
}

/* Wird von AppKit gerufen, wenn sich das Erscheinungsbild AUCH OHNE
 * Nutzeraktion in dieser App aendert (z.B. automatischer Hell/Dunkel-Wechsel
 * bei Sonnenuntergang, waehrend die App im Hintergrund ist) - main.c fragt
 * btn_app_is_dark_mode() sonst nur bei jedem Redraw ab, der durch Tippen/
 * Groessenaenderung ausgeloest wird; ohne diesen Hook wuerde die Farbe erst
 * beim naechsten Tastendruck nachziehen. */
- (void)viewDidChangeEffectiveAppearance {
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
    if (g_launch_cb) {
        g_launch_cb();
    }
}

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    (void)notification;
    if (g_activate_cb) {
        g_activate_cb();
    }
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
    open_file_urls(urls);
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

/* NSMenu aktiviert Eintraege automatisch - hier nur die Zeilenenden sperren,
 * solange das aktive Dokument eine Binaerdatei ist. */
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    NSInteger tag = [item tag];
    if (tag >= BTN_MENU_EOL_LF && tag <= BTN_MENU_EOL_CR) {
        return g_eolMenuEnabled;
    }
    return YES;
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
/* C-String -> NSString fuer Text, der nicht garantiert gueltiges UTF-8 ist
 * (Dateinamen/Pfade auf SMB/NFS/FAT-Volumes, per snprintf gekuerzte
 * Meldungen): erst UTF-8, sonst ISO-8859-1 (nimmt jedes Byte). Nie nil -
 * nil in setTitle:/addItemWithTitle:/setMessageText: wirft eine Exception
 * bzw. zeigt nichts an. */
static NSString *ns_from_c(const char *s) {
    if (!s) {
        return @"";
    }
    NSString *r = [NSString stringWithUTF8String:s];
    if (!r) {
        r = [[[NSString alloc] initWithBytes:s length:strlen(s) encoding:NSISOLatin1StringEncoding] autorelease];
    }
    return r ? r : @"";
}

static NSString *trs(BtnStringId id) {
    return ns_from_c(btn_tr(id));
}

BtnUiLang btn_app_detect_system_language(void) {
    @autoreleasepool {
        NSArray<NSString *> *preferred = [NSLocale preferredLanguages];
        NSString *code = preferred.count > 0 ? preferred[0] : @"en";
        return btn_strings_lang_from_code([code UTF8String]);
    }
}

int btn_app_is_dark_mode(void) {
    @autoreleasepool {
        NSAppearance *appearance = [NSApp effectiveAppearance];
        NSAppearanceName match = [appearance bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
        return [match isEqualToString:NSAppearanceNameDarkAqua] ? 1 : 0;
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

void btn_app_set_launch_callback(btn_void_callback cb) {
    g_launch_cb = cb;
}

void btn_app_set_activate_callback(btn_void_callback cb) {
    g_activate_cb = cb;
}

void btn_app_start_repeating_timer(double seconds, btn_void_callback cb) {
    NSTimer *timer = [NSTimer timerWithTimeInterval:seconds repeats:YES block:^(NSTimer *t) {
        (void)t;
        cb();
    }];
    /* Nur Standardmodus: hinter einem Dialog (Sichern?, Datei geaendert?)
     * soll nichts neu geladen oder geschrieben werden. */
    [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSDefaultRunLoopMode];
}

void btn_app_set_autoscroll(int on) {
    if (!on || !g_mouse_down) {
        stop_autoscroll();
        return;
    }
    if (g_autoscroll_timer) {
        return; /* laeuft schon - bei jeder Mausbewegung neu starten hiesse nie ticken */
    }
    g_autoscroll_timer = [NSTimer timerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *timer) {
        (void)timer;
        /* Das mouseUp kann verloren gehen (Loslassen waehrend eines modalen
         * Dialogs, z.B. nach Cmd+W mitten im Markieren) - ohne diese Pruefung
         * liefe der Autoscroll danach bis zum Dokumentende weiter. */
        BOOL pressed = g_test_button_state >= 0 ? g_test_button_state : ([NSEvent pressedMouseButtons] & 1) != 0;
        if (!pressed) {
            g_mouse_down = NO;
            stop_autoscroll();
            if (g_mouse_cb) {
                g_mouse_cb(BTN_MOUSE_UP, g_last_mouse.x, g_last_mouse.y, 1, (unsigned long)[NSEvent modifierFlags]);
            }
            return;
        }
        if (g_mouse_cb) {
            g_mouse_cb(BTN_MOUSE_AUTOSCROLL, g_last_mouse.x, g_last_mouse.y, 1, (unsigned long)[NSEvent modifierFlags]);
        }
    }];
    /* Nicht NSRunLoopCommonModes: die enthalten auch den Modus modaler
     * Dialoge, und hinter einem Sichern-Dialog soll nichts scrollen. */
    [[NSRunLoop currentRunLoop] addTimer:g_autoscroll_timer forMode:NSDefaultRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer:g_autoscroll_timer forMode:NSEventTrackingRunLoopMode];
}

void btn_shim_test_set_mouse_button(int state) {
    g_test_button_state = state;
}

void btn_app_set_text_cursor_rects(const CGRect *rects, int count) {
    if (count < 0) {
        count = 0;
    }
    if (count > BTN_MAX_CURSOR_RECTS) {
        count = BTN_MAX_CURSOR_RECTS;
    }
    if (count == g_cursor_rect_count &&
        (count == 0 || memcmp(rects, g_cursor_rects, (size_t)count * sizeof(CGRect)) == 0)) {
        return;
    }
    if (count > 0) {
        memcpy(g_cursor_rects, rects, (size_t)count * sizeof(CGRect));
    }
    g_cursor_rect_count = count;
    [[g_view window] invalidateCursorRectsForView:g_view];
}

void btn_app_set_should_close_callback(btn_should_close_callback cb) {
    g_should_close_cb = cb;
}

void btn_app_set_open_file_callback(btn_open_file_callback cb) {
    g_open_file_cb = cb;
}

void btn_app_set_text_input_callbacks(const BtnTextInputCallbacks *cb) {
    if (cb) {
        g_ti = *cb;
        g_ti_set = YES;
    } else {
        memset(&g_ti, 0, sizeof(g_ti));
        g_ti_set = NO;
    }
}

void btn_text_input_discard(void) {
    [[g_view inputContext] discardMarkedText];
}

void btn_text_input_invalidate(void) {
    [[g_view inputContext] invalidateCharacterCoordinates];
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
        /* Zeilenenden, mit denen das aktive Dokument gesichert wird. */
        NSMenu *eolMenu = [[NSMenu alloc] initWithTitle:trs(BTN_STR_LINE_ENDINGS)];
        const BtnStringId eolTitles[3] = { BTN_STR_EOL_LF, BTN_STR_EOL_CRLF, BTN_STR_EOL_CR };
        for (int i = 0; i < 3; i++) {
            g_eolItems[i] = [eolMenu addItemWithTitle:trs(eolTitles[i]) action:@selector(menuAction:) keyEquivalent:@""];
            [g_eolItems[i] setTarget:g_menuTarget];
            [g_eolItems[i] setTag:BTN_MENU_EOL_LF + i];
        }
        NSMenuItem *eolItem = [fileMenu addItemWithTitle:trs(BTN_STR_LINE_ENDINGS) action:nil keyEquivalent:@""];
        [eolItem setSubmenu:eolMenu];
        [fileMenu addItem:[NSMenuItem separatorItem]];
        add_item(fileMenu, trs(BTN_STR_CLOSE), @"w", BTN_MENU_CLOSE);
        [fileMenu addItem:[NSMenuItem separatorItem]];
        add_item(fileMenu, trs(BTN_STR_PRINT), @"p", BTN_MENU_PRINT);
        [fileMenuItem setSubmenu:fileMenu];

        NSMenuItem *editMenuItem = [NSMenuItem new];
        [menubar addItem:editMenuItem];
        NSMenu *editMenu = [[NSMenu alloc] initWithTitle:trs(BTN_STR_EDIT_MENU)];
        /* target:nil (nicht add_item()) fuer die Standard-Editieraktionen -
         * siehe der ausfuehrliche Kommentar bei BTNContentViews cut:/copy:/
         * paste:/selectAll:/undo:/redo: oben, warum das noetig ist, damit
         * Cmd+C/V/X/Z/A auch in nativen Cocoa-Textfeldern (z.B. dem
         * Speichern-Dialog) funktionieren statt immer von diesem Menue
         * abgefangen zu werden. */
        [editMenu addItemWithTitle:trs(BTN_STR_UNDO) action:@selector(undo:) keyEquivalent:@"z"];
        [editMenu addItemWithTitle:trs(BTN_STR_REDO) action:@selector(redo:) keyEquivalent:@"Z"];
        [editMenu addItem:[NSMenuItem separatorItem]];
        [editMenu addItemWithTitle:trs(BTN_STR_CUT) action:@selector(cut:) keyEquivalent:@"x"];
        [editMenu addItemWithTitle:trs(BTN_STR_COPY) action:@selector(copy:) keyEquivalent:@"c"];
        [editMenu addItemWithTitle:trs(BTN_STR_PASTE) action:@selector(paste:) keyEquivalent:@"v"];
        [editMenu addItemWithTitle:trs(BTN_STR_SELECT_ALL) action:@selector(selectAll:) keyEquivalent:@"a"];
        [editMenu addItem:[NSMenuItem separatorItem]];
        add_item(editMenu, trs(BTN_STR_FIND), @"f", BTN_MENU_FIND);
        /* Grossbuchstabe = mit Shift (wie "S" bei Sichern unter) */
        add_item(editMenu, trs(BTN_STR_FIND_NEXT), @"g", BTN_MENU_FIND_NEXT);
        add_item(editMenu, trs(BTN_STR_FIND_PREVIOUS), @"G", BTN_MENU_FIND_PREVIOUS);
        add_item(editMenu, trs(BTN_STR_USE_SELECTION_FOR_FIND), @"e", BTN_MENU_USE_SELECTION_FOR_FIND);
        add_item(editMenu, trs(BTN_STR_GOTO_LINE), @"l", BTN_MENU_GOTO_LINE);
        [editMenuItem setSubmenu:editMenu];

        NSMenuItem *viewMenuItem = [NSMenuItem new];
        [menubar addItem:viewMenuItem];
        NSMenu *viewMenu = [[NSMenu alloc] initWithTitle:trs(BTN_STR_VIEW_MENU)];
        add_item(viewMenu, trs(BTN_STR_ZOOM_IN), @"=", BTN_MENU_ZOOM_IN);
        add_item(viewMenu, trs(BTN_STR_ZOOM_OUT), @"-", BTN_MENU_ZOOM_OUT);
        add_item(viewMenu, trs(BTN_STR_ZOOM_RESET), @"0", BTN_MENU_ZOOM_RESET);
        [viewMenuItem setSubmenu:viewMenu];

        /* Fenster: die AppKit-Standardaktionen (target nil -> Responder-Kette
         * bis zum NSWindow) plus Tab-Wechsel. setWindowsMenu: laesst macOS
         * die Fensterliste selbst anhaengen. */
        NSMenuItem *windowMenuItem = [NSMenuItem new];
        [menubar addItem:windowMenuItem];
        NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:trs(BTN_STR_WINDOW_MENU)];
        [windowMenu addItemWithTitle:trs(BTN_STR_MINIMIZE) action:@selector(performMiniaturize:) keyEquivalent:@"m"];
        [windowMenu addItemWithTitle:trs(BTN_STR_ZOOM) action:@selector(performZoom:) keyEquivalent:@""];
        NSMenuItem *fullScreen = [windowMenu addItemWithTitle:trs(BTN_STR_FULL_SCREEN)
                                                       action:@selector(toggleFullScreen:)
                                                keyEquivalent:@"f"];
        [fullScreen setKeyEquivalentModifierMask:NSEventModifierFlagControl | NSEventModifierFlagCommand];
        [windowMenu addItem:[NSMenuItem separatorItem]];
        /* "}"/"{" = Shift+"]"/"[" (US-Layout, wie in Safari) */
        add_item(windowMenu, trs(BTN_STR_NEXT_TAB), @"}", BTN_MENU_NEXT_TAB);
        add_item(windowMenu, trs(BTN_STR_PREVIOUS_TAB), @"{", BTN_MENU_PREVIOUS_TAB);
        [windowMenuItem setSubmenu:windowMenu];
        [NSApp setWindowsMenu:windowMenu];

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
            NSString *full = ns_from_c(paths[i]);
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

void btn_app_set_line_ending_menu(int index, int enabled) {
    g_eolMenuEnabled = enabled ? YES : NO;
    for (int i = 0; i < 3; i++) {
        [g_eolItems[i] setState:(i == index) ? NSControlStateValueOn : NSControlStateValueOff];
    }
}

void btn_beep(void) {
    NSBeep();
}

void btn_app_request_redraw(void) {
    [g_view setNeedsDisplay:YES];
}

void btn_app_run(void) {
    @autoreleasepool {
        /* Breite 1080 passend zur untenstehenden setMinSize: (AppKit wuerde
         * ein kleiner angefordertes Fenster ohnehin sofort auf die
         * Mindestbreite hochziehen, aber ein Startwert UNTER der eigenen
         * Mindestgroesse waere irrefuehrend zu lesen). */
        NSRect frame = NSMakeRect(200, 200, 1080, 600);
        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        g_window = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:style
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
        [g_window setTitle:trs(BTN_STR_UNTITLED)];
        /* Breite 1080 statt z.B. 400: Felder, die drei Umschalter
         * (".*"/"Aa"/"\b") und der "Alle ersetzen"-Knopf der Suchen/Ersetzen-
         * Leiste (render.c) reichen bis x = 840pt, dahinter beginnt der
         * Statustext. Bei 1080pt bleiben ihm gut 230pt; laengere Meldungen
         * kuerzt render.c mit "..." (btn_render_find_bar()). Bei einer
         * kleineren Mindestbreite waere der Knopf selbst abgeschnitten.
         * shim.m kennt render.h's Layout-Konstanten bewusst nicht (reine
         * Chrome), daher hier als eigener Wert statt eines Verweises. */
        [g_window setMinSize:NSMakeSize(1080, 300)];
        /* Fenster > Vollbild (toggleFullScreen:) */
        [g_window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];

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

int btn_pasteboard_set_string(const char *bytes, size_t len) {
    @autoreleasepool {
        NSString *s = @"";
        if (bytes && len > 0) {
            /* initWithBytes:length: statt stringWithUTF8String: - Letzteres
             * bricht am ersten NUL-Byte ab und liefert bei ungueltigem UTF-8
             * nil; dann landete nil in setString: und der Text war nach dem
             * anschliessenden Loeschen (Ausschneiden) nirgends mehr.
             * ISO-8859-1 bildet jeden Bytewert ab und schlaegt nie fehl -
             * fuer Latin-1-/Binaerdateien, die render.c bewusst anzeigt. */
            s = [[[NSString alloc] initWithBytes:bytes length:len encoding:NSUTF8StringEncoding] autorelease];
            if (!s) {
                s = [[[NSString alloc] initWithBytes:bytes length:len encoding:NSISOLatin1StringEncoding] autorelease];
            }
            if (!s) {
                return 0;
            }
        }
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        return [pb setString:s forType:NSPasteboardTypeString] ? 1 : 0;
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
            NSString *s = ns_from_c(suggested_path);
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
        /* autorelease: ohne ARC leakte jeder Dialog sein NSAlert samt Panel. */
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:ns_from_c(msg)];
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

int btn_show_choice_alert(const char *title, const char *info, const char *first, const char *second) {
    @autoreleasepool {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:ns_from_c(title)];
        [alert setInformativeText:ns_from_c(info)];
        [alert addButtonWithTitle:ns_from_c(first)];
        NSButton *other = [alert addButtonWithTitle:ns_from_c(second)];
        [other setKeyEquivalent:@"\033"];
        return [alert runModal] == NSAlertFirstButtonReturn ? 1 : 0;
    }
}

int btn_show_binary_file_warning(const char *display_name) {
    @autoreleasepool {
        char msg[512];
        snprintf(msg, sizeof(msg), btn_tr(BTN_STR_BINARY_WARNING_TITLE_FMT),
                 display_name ? display_name : btn_tr(BTN_STR_UNTITLED));
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:ns_from_c(msg)];
        [alert setInformativeText:trs(BTN_STR_BINARY_WARNING_INFO)];
        [alert addButtonWithTitle:trs(BTN_STR_BTN_OPEN_ANYWAY)];
        [alert addButtonWithTitle:trs(BTN_STR_BTN_CANCEL)];
        NSModalResponse resp = [alert runModal];
        return resp == NSAlertFirstButtonReturn ? 1 : 0;
    }
}

int btn_show_goto_line_dialog(long max_line, long *out_line) {
    @autoreleasepool {
        char info[128];
        snprintf(info, sizeof(info), btn_tr(BTN_STR_GOTO_LINE_INFO_FMT), (int)max_line);

        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:trs(BTN_STR_GOTO_LINE)];
        [alert setInformativeText:ns_from_c(info)];
        [alert addButtonWithTitle:trs(BTN_STR_BTN_OK)];
        [alert addButtonWithTitle:trs(BTN_STR_BTN_CANCEL)];

        /* Zahlen-Eingabefeld als Accessory View - wie die anderen Alerts
         * hier ein reiner Systemdialog, kein eigenstaendiges Content-Fenster
         * (siehe Architektur-Kommentar oben in dieser Datei). */
        NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 24)];
        [[field cell] setPlaceholderString:@"1"];
        [alert setAccessoryView:field];
        /* initialFirstResponder gehoert zu NSWindow, nicht zu NSAlert selbst -
         * [alert window] baut/liefert das dahinterliegende Panel (inklusive
         * layoutetem Accessory View) erst bei diesem Zugriff. */
        [[alert window] setInitialFirstResponder:field];

        NSModalResponse resp = [alert runModal];
        int ok = (resp == NSAlertFirstButtonReturn);
        if (ok) {
            long line = [[field stringValue] integerValue];
            if (line < 1) {
                line = 1;
            }
            if (max_line > 0 && line > max_line) {
                line = max_line;
            }
            *out_line = line;
        }
        [field release];
        return ok;
    }
}

void btn_show_help_alert(void) {
    @autoreleasepool {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:trs(BTN_STR_HELP_TITLE)];
        [alert setInformativeText:trs(BTN_STR_HELP_BODY)];
        [alert runModal];
    }
}

void btn_show_error_alert(const char *title, const char *info) {
    @autoreleasepool {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setAlertStyle:NSAlertStyleWarning];
        [alert setMessageText:ns_from_c(title)];
        [alert setInformativeText:ns_from_c(info)];
        [alert addButtonWithTitle:trs(BTN_STR_BTN_OK)];
        [alert runModal];
    }
}

void btn_set_window_title(const char *title) {
    @autoreleasepool {
        [g_window setTitle:ns_from_c(title ? title : btn_tr(BTN_STR_UNTITLED))];
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
