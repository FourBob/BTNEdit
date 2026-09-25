/* Laeuft nur auf macOS (make test-objc, CI): erzeugt die echte
 * BTNContentView aus shim.m in einem Fenster und prueft die
 * NSTextInputClient-Uebersetzung - direkte Protokollaufrufe wie von einer
 * Eingabemethode und kuenstliche Tasten-Events durch keyDown:. Die
 * Callbacks sind Stubs, die mitschreiben, was bei main.c ankaeme. */
#import <Cocoa/Cocoa.h>
#include "shim.h"
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } else { printf("ok    " __VA_ARGS__); printf("\n"); } } while (0)

static char last_insert[64];
static long last_repl_loc = -2;
static int inserts, keys, marks, unmarks;
static unsigned short last_keycode;
static char last_key_chars[16];
static unsigned long last_mods;
static char last_marked[64];
static long last_sel_loc;
static int have_marked;

static void ti_insert(const char *utf8, long repl_loc, long repl_len) {
    (void)repl_len;
    snprintf(last_insert, sizeof last_insert, "%s", utf8);
    last_repl_loc = repl_loc;
    inserts++;
    have_marked = 0;
}
static void ti_mark(const char *utf8, long sel_loc, long sel_len, long repl_loc, long repl_len) {
    (void)sel_len; (void)repl_loc; (void)repl_len;
    snprintf(last_marked, sizeof last_marked, "%s", utf8);
    last_sel_loc = sel_loc;
    have_marked = utf8[0] != 0;
    marks++;
}
static void ti_unmark(void) { unmarks++; have_marked = 0; }
static void ti_query(long *sel_loc, long *sel_len, long *mk_loc, long *mk_len) {
    *sel_loc = 5; *sel_len = 0;
    *mk_loc = have_marked ? 5 : -1;
    *mk_len = have_marked ? (long)strlen(last_marked) : 0;
}
static uint16_t *ti_substring(long loc, long len, long *actual_loc, size_t *n) {
    (void)len;
    uint16_t *u = malloc(3 * sizeof(uint16_t));
    u[0] = 'a'; u[1] = 0xE4; u[2] = 'c';
    *actual_loc = loc;
    *n = 3;
    return u;
}
static long last_rect_loc = -2;
static CGRect ti_caret(long loc) { last_rect_loc = loc; return CGRectMake(10, 20, 8, 18); }
static void key_cb(const char *chars, unsigned short keycode, unsigned long mods) {
    snprintf(last_key_chars, sizeof last_key_chars, "%s", chars);
    last_keycode = keycode;
    last_mods = mods;
    keys++;
}

/* Sucht rekursiv einen Menueeintrag mit Tastenkuerzel key und genau diesen
 * Modifiern. */
static NSMenuItem *find_item(NSMenu *menu, NSString *key, NSEventModifierFlags mods) {
    for (NSMenuItem *item in [menu itemArray]) {
        if ([[item keyEquivalent] isEqualToString:key] && [item keyEquivalentModifierMask] == mods) {
            return item;
        }
        if ([item hasSubmenu]) {
            NSMenuItem *found = find_item([item submenu], key, mods);
            if (found) {
                return found;
            }
        }
    }
    return nil;
}

/* Ablegen im Fenster: der Shim liest nur draggingPasteboard. */
@interface FakeDrag : NSObject {
    NSPasteboard *_pb;
}
- (instancetype)initWithPasteboard:(NSPasteboard *)pb;
- (NSPasteboard *)draggingPasteboard;
@end

@implementation FakeDrag
- (instancetype)initWithPasteboard:(NSPasteboard *)pb {
    self = [super init];
    if (self) {
        _pb = [pb retain];
    }
    return self;
}
- (void)dealloc {
    [_pb release];
    [super dealloc];
}
- (NSPasteboard *)draggingPasteboard {
    return _pb;
}
@end

static int opened;
static char opened_paths[4][64];
static void open_cb(const char *path) {
    if (opened < 4) {
        snprintf(opened_paths[opened], sizeof opened_paths[0], "%s", path);
    }
    opened++;
}

static int ticks;
static double tick_x, tick_y;
static void mouse_cb(btn_mouse_phase phase, double x, double y, int clicks, unsigned long mods) {
    (void)clicks; (void)mods;
    if (phase == BTN_MOUSE_AUTOSCROLL) {
        ticks++;
        tick_x = x;
        tick_y = y;
    }
}

static void spin(double seconds) {
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
}

static NSEvent *mouse_event(NSWindow *w, NSEventType type, NSPoint p) {
    return [NSEvent mouseEventWithType:type location:p modifierFlags:0 timestamp:0 windowNumber:[w windowNumber]
                               context:nil eventNumber:0 clickCount:1 pressure:1.0f];
}

static NSEvent *key_event(NSWindow *w, NSString *chars, NSString *ignoring, unsigned short code, NSEventModifierFlags mods) {
    return [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:mods timestamp:0
                        windowNumber:[w windowNumber] context:nil characters:chars charactersIgnoringModifiers:ignoring
                           isARepeat:NO keyCode:code];
}

int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        btn_app_set_key_callback(key_cb);
        static const BtnTextInputCallbacks cbs = { ti_insert, ti_mark, ti_unmark, ti_query, ti_substring, ti_caret };
        btn_app_set_text_input_callbacks(&cbs);

        Class cls = NSClassFromString(@"BTNContentView");
        CHECK(cls != Nil, "BTNContentView exists");
        CHECK([cls conformsToProtocol:@protocol(NSTextInputClient)], "BTNContentView conforms to NSTextInputClient");
        NSView<NSTextInputClient> *view = [[cls alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
        NSWindow *win = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 400, 300)
                                                    styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        [win setContentView:view];
        [win makeKeyAndOrderFront:nil];
        [win makeFirstResponder:view];
        [[view inputContext] activate];

        /* ---- Protokollaufrufe wie von einer Eingabemethode ---- */
        [view insertText:@"é" replacementRange:NSMakeRange(NSNotFound, 0)];
        CHECK(strcmp(last_insert, "\xC3\xA9") == 0 && last_repl_loc == -1, "insertText -> insert_text(UTF-8, -1)");
        [view insertText:[[[NSAttributedString alloc] initWithString:@"x"] autorelease] replacementRange:NSMakeRange(4, 1)];
        CHECK(strcmp(last_insert, "x") == 0 && last_repl_loc == 4, "attributed insertText with replacement range");
        [view setMarkedText:@"ni" selectedRange:NSMakeRange(2, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
        CHECK(strcmp(last_marked, "ni") == 0 && last_sel_loc == 2 && [view hasMarkedText], "setMarkedText -> set_marked_text, hasMarkedText");
        NSRange mr = [view markedRange];
        CHECK(mr.location == 5 && mr.length == 2, "markedRange from query");
        [view unmarkText];
        CHECK(unmarks == 1 && ![view hasMarkedText] && [view markedRange].location == NSNotFound, "unmarkText");
        CHECK([view selectedRange].location == 5, "selectedRange");
        NSRange actual;
        NSAttributedString *sub = [view attributedSubstringForProposedRange:NSMakeRange(1, 3) actualRange:&actual];
        CHECK(sub && [[sub string] isEqualToString:@"aäc"] && actual.location == 1, "attributedSubstringForProposedRange");
        NSRect r = [view firstRectForCharacterRange:NSMakeRange(5, 0) actualRange:NULL];
        CHECK(r.size.width == 8 && r.size.height == 18 && r.origin.x >= 100 && last_rect_loc == 5,
              "firstRectForCharacterRange passes the location, returns screen coordinates");
        CHECK([view characterIndexForPoint:NSZeroPoint] == NSNotFound, "characterIndexForPoint");

        /* ---- Tasten durch keyDown: (interpretKeyEvents:) ---- */
        int i0 = inserts, k0 = keys;
        [view keyDown:key_event(win, @"a", @"a", 0, 0)];
        CHECK(inserts == i0 + 1 && keys == k0 && strcmp(last_insert, "a") == 0, "letter -> insert_text, not key callback");
        k0 = keys; i0 = inserts;
        [view keyDown:key_event(win, @"\r", @"\r", 36, 0)];
        CHECK(keys == k0 + 1 && last_keycode == 36 && strcmp(last_key_chars, "\r") == 0 && inserts == i0, "Return -> key callback (keycode 36)");
        unichar left = NSLeftArrowFunctionKey;
        NSString *ls = [NSString stringWithCharacters:&left length:1];
        [view keyDown:key_event(win, ls, ls, 123, NSEventModifierFlagFunction | NSEventModifierFlagNumericPad)];
        CHECK(last_keycode == 123 && last_key_chars[0] == 0, "arrow -> key callback, private-use character filtered");
        [view keyDown:key_event(win, @"\x7f", @"\x7f", 51, 0)];
        CHECK(last_keycode == 51 && (unsigned char)last_key_chars[0] == 0x7F, "Backspace -> key callback");
        [view keyDown:key_event(win, @"\t", @"\t", 48, 0)];
        CHECK(last_keycode == 48, "Tab -> key callback");
        [view keyDown:key_event(win, @"\x19", @"\t", 48, NSEventModifierFlagShift)];
        CHECK(last_keycode == 48 && (last_mods & NSEventModifierFlagShift), "Shift+Tab -> key callback with shift");
        i0 = inserts;
        [view keyDown:key_event(win, @"j", @"j", 38, NSEventModifierFlagCommand)];
        CHECK(last_keycode == 38 && (last_mods & NSEventModifierFlagCommand) && inserts == i0, "Cmd+J -> key callback, no text");

        /* Tottaste (US-Layout: Option+E, dann E). Haengt vom Tastaturlayout
         * des Rechners ab - nur Hinweis, kein Fehler. */
        int m0 = marks;
        i0 = inserts;
        [view keyDown:key_event(win, @"", @"e", 14, NSEventModifierFlagOption)];
        [view keyDown:key_event(win, @"e", @"e", 14, 0)];
        printf("info  dead key Option+E, E: %d marked update(s), last insert \"%s\" (%s)\n", marks - m0, last_insert,
               (inserts > i0 && strcmp(last_insert, "\xC3\xA9") == 0) ? "composed" : "layout-dependent, not composed here");

        /* Jede Taste ohne Text genau EIN Key-Callback, kein Text */
        struct { NSString *chars; unsigned short code; NSEventModifierFlags mods; const char *name; } keys_once[] = {
            { [NSString stringWithFormat:@"%C", (unichar)NSF5FunctionKey], 96, NSEventModifierFlagFunction, "F5" },
            { [NSString stringWithFormat:@"%C", (unichar)NSUpArrowFunctionKey], 126,
              NSEventModifierFlagOption | NSEventModifierFlagFunction | NSEventModifierFlagNumericPad, "Option+Up" },
            { [NSString stringWithFormat:@"%C", (unichar)NSDownArrowFunctionKey], 125,
              NSEventModifierFlagOption | NSEventModifierFlagFunction | NSEventModifierFlagNumericPad, "Option+Down" },
            { @"\x1b", 53, 0, "Escape" },
            { [NSString stringWithFormat:@"%C", (unichar)NSDeleteFunctionKey], 117, NSEventModifierFlagFunction, "Forward Delete" },
            { [NSString stringWithFormat:@"%C", (unichar)NSHomeFunctionKey], 115, NSEventModifierFlagFunction, "Home" },
            { [NSString stringWithFormat:@"%C", (unichar)NSPageDownFunctionKey], 121, NSEventModifierFlagFunction, "Page Down" },
            { @"\r", 36, 0, "Return" },
            { @"\x7f", 51, 0, "Backspace" },
        };
        for (size_t q = 0; q < sizeof keys_once / sizeof keys_once[0]; q++) {
            int kb = keys, ib = inserts;
            [view keyDown:key_event(win, keys_once[q].chars, keys_once[q].chars, keys_once[q].code, keys_once[q].mods)];
            CHECK(keys == kb + 1 && inserts == ib && last_keycode == keys_once[q].code,
                  "%s -> exactly one key callback, no text (%d)", keys_once[q].name, keys - kb);
        }

        /* Cmd+Taste waehrend einer Eingabe: die Eingabemethode darf erst
         * festschreiben, der Befehl kommt trotzdem genau einmal an */
        [view setMarkedText:@"k" selectedRange:NSMakeRange(1, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
        int kb2 = keys;
        unichar lc = NSLeftArrowFunctionKey;
        NSString *cl = [NSString stringWithCharacters:&lc length:1];
        [view keyDown:key_event(win, cl, cl, 123, NSEventModifierFlagCommand | NSEventModifierFlagFunction)];
        CHECK(keys <= kb2 + 1, "Cmd+Left with marked text: at most one key callback (%d)", keys - kb2);
        [view unmarkText];

        /* Ctrl+Tab kommt mit Ctrl-Flag beim Key-Callback an (Tab-Wechsel) */
        [view keyDown:key_event(win, @"\t", @"\t", 48, NSEventModifierFlagControl)];
        CHECK(last_keycode == 48 && (last_mods & NSEventModifierFlagControl), "Ctrl+Tab -> key callback with control");

        /* ---- Menue: Standard-Tastenkuerzel ---- */
        btn_app_build_menu();
        NSMenu *menubar = [NSApp mainMenu];
        NSEventModifierFlags cmd = NSEventModifierFlagCommand;
        NSMenuItem *it = find_item(menubar, @"g", cmd);
        CHECK(it && [it tag] == BTN_MENU_FIND_NEXT, "Cmd+G = Find Next");
        it = find_item(menubar, @"G", cmd);
        CHECK(it && [it tag] == BTN_MENU_FIND_PREVIOUS, "Shift+Cmd+G = Find Previous");
        it = find_item(menubar, @"e", cmd);
        CHECK(it && [it tag] == BTN_MENU_USE_SELECTION_FOR_FIND, "Cmd+E = Use Selection for Find");
        it = find_item(menubar, @"m", cmd);
        CHECK(it && [it action] == @selector(performMiniaturize:), "Cmd+M = Minimize");
        it = find_item(menubar, @"f", NSEventModifierFlagControl | cmd);
        CHECK(it && [it action] == @selector(toggleFullScreen:), "Ctrl+Cmd+F = Full Screen");
        it = find_item(menubar, @"}", cmd);
        CHECK(it && [it tag] == BTN_MENU_NEXT_TAB, "Shift+Cmd+] = Next Tab");
        it = find_item(menubar, @"{", cmd);
        CHECK(it && [it tag] == BTN_MENU_PREVIOUS_TAB, "Shift+Cmd+[ = Previous Tab");
        CHECK([NSApp windowsMenu] != nil && [[NSApp windowsMenu] indexOfItemWithTarget:nil andAction:@selector(performZoom:)] >= 0,
              "Window menu registered with Zoom");
        it = find_item(menubar, @"f", cmd);
        CHECK(it && [it tag] == BTN_MENU_FIND, "Cmd+F still Find (no clash with full screen)");

        /* ---- Dateien ins Fenster ziehen ---- */
        btn_app_set_open_file_callback(open_cb);
        CHECK([[view registeredDraggedTypes] containsObject:NSPasteboardTypeFileURL], "view accepts dragged file URLs");
        NSPasteboard *pb = [NSPasteboard pasteboardWithUniqueName];
        [pb clearContents];
        [pb writeObjects:@[ [NSURL fileURLWithPath:@"/tmp/btn a.txt"], [NSURL fileURLWithPath:@"/tmp/b.c"] ]];
        FakeDrag *drag = [[FakeDrag alloc] initWithPasteboard:pb];
        id<NSDraggingInfo> info = (id<NSDraggingInfo>)drag;
        id dest = view;
        CHECK([dest draggingEntered:info] == NSDragOperationCopy, "file drag: copy cursor");
        CHECK([dest performDragOperation:info], "file drop accepted");
        CHECK(opened == 0, "files opened only after the drop has returned");
        spin(0.2);
        CHECK(opened == 2 && strcmp(opened_paths[0], "/tmp/btn a.txt") == 0 && strcmp(opened_paths[1], "/tmp/b.c") == 0,
              "both dropped files opened, in order (%d)", opened);
        [pb clearContents];
        [pb writeObjects:@[ @"just text" ]];
        CHECK([dest draggingEntered:info] == NSDragOperationNone && ![dest performDragOperation:info], "text drag refused");
        [pb clearContents];
        [pb writeObjects:@[ [NSURL URLWithString:@"https://example.com/a.txt"] ]];
        CHECK([dest draggingEntered:info] == NSDragOperationNone && ![dest performDragOperation:info], "web link refused");
        spin(0.1);
        CHECK(opened == 2, "nothing opened for refused drops");
        [drag release];
        [pb releaseGlobally];

        /* ---- Autoscroll-Takt beim Markieren ---- */
        btn_app_set_mouse_callback(mouse_cb);
        btn_app_set_autoscroll(1);
        spin(0.2);
        CHECK(ticks == 0, "no autoscroll without a pressed mouse button");
        [view mouseDown:mouse_event(win, NSEventTypeLeftMouseDown, NSMakePoint(50, 200))];
        [view mouseDragged:mouse_event(win, NSEventTypeLeftMouseDragged, NSMakePoint(60, 350))];
        btn_app_set_autoscroll(1);
        spin(0.3);
        CHECK(ticks >= 3 && tick_x == 60 && tick_y == 350, "ticks with the last mouse position (%d, %.0f/%.0f)", ticks, tick_x, tick_y);
        int t0 = ticks;
        for (int q = 0; q < 15; q++) {
            btn_app_set_autoscroll(1); /* wie bei jeder Mausbewegung */
            spin(0.02);
        }
        CHECK(ticks - t0 >= 3, "switching on again keeps the running timer (%d ticks)", ticks - t0);
        btn_app_set_autoscroll(0);
        spin(0.06);
        int t1 = ticks;
        spin(0.2);
        CHECK(ticks == t1, "switched off: no more ticks");
        btn_app_set_autoscroll(1);
        spin(0.2);
        CHECK(ticks > t1, "switched on again while the button is down");
        [view mouseUp:mouse_event(win, NSEventTypeLeftMouseUp, NSMakePoint(60, 350))];
        int t2 = ticks;
        spin(0.2);
        CHECK(ticks == t2, "mouse up stops the autoscroll");

        /* I-Beam-Flaechen: setzen, gleich setzen, leeren - ohne Absturz */
        CGRect rects[2] = { CGRectMake(44, 22, 300, 250), CGRectMake(78, 272, 200, 22) };
        btn_app_set_text_cursor_rects(rects, 2);
        btn_app_set_text_cursor_rects(rects, 2);
        btn_app_set_text_cursor_rects(NULL, 0);

        [win orderOut:nil];
    }
    printf("%s: %d checks, %d failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
