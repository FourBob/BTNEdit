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
static CGRect ti_caret(void) { return CGRectMake(10, 20, 8, 18); }
static void key_cb(const char *chars, unsigned short keycode, unsigned long mods) {
    snprintf(last_key_chars, sizeof last_key_chars, "%s", chars);
    last_keycode = keycode;
    last_mods = mods;
    keys++;
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
        CHECK(r.size.width == 8 && r.size.height == 18 && r.origin.x >= 100, "firstRectForCharacterRange in screen coordinates");
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

        [win orderOut:nil];
    }
    printf("%s: %d checks, %d failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
