/* Laeuft nur auf macOS (make test-objc, CI): erzeugt die echte
 * BTNContentView aus shim.m in einem Fenster und prueft die
 * NSTextInputClient-Uebersetzung - direkte Protokollaufrufe wie von einer
 * Eingabemethode und kuenstliche Tasten-Events durch keyDown:. Die
 * Callbacks sind Stubs, die mitschreiben, was bei main.c ankaeme. */
#import <Cocoa/Cocoa.h>
#include "shim.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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

static int ticks, ups, timer_calls;
static void timer_cb(void) { timer_calls++; }
static double tick_x, tick_y;
static void mouse_cb(btn_mouse_phase phase, double x, double y, int clicks, unsigned long mods) {
    (void)clicks; (void)mods;
    if (phase == BTN_MOUSE_UP) {
        ups++;
    }
    if (phase == BTN_MOUSE_AUTOSCROLL) {
        ticks++;
        tick_x = x;
        tick_y = y;
    }
}

/* ---- Kleiner HTTP-Server fuer btn_http_post_json (KI-Vervollstaendigung) ---- */
static int g_srv_fd = -1, g_srv_port = 0;
static char g_srv_request[8192];
static volatile int g_srv_delay_ms = 0;

static void *server_thread(void *arg) {
    (void)arg;
    for (;;) {
        int c = accept(g_srv_fd, NULL, NULL);
        if (c < 0) {
            return NULL;
        }
        char buf[8192];
        size_t n = 0;
        while (n < sizeof buf - 1) {
            ssize_t r = read(c, buf + n, sizeof buf - 1 - n);
            if (r <= 0) {
                break;
            }
            n += (size_t)r;
            buf[n] = 0;
            char *end = strstr(buf, "\r\n\r\n");
            char *cl = strcasestr(buf, "Content-Length:");
            if (end && (!cl || n >= (size_t)(end + 4 - buf) + strtoul(cl + 15, NULL, 10))) {
                break;
            }
        }
        buf[n] = 0;
        memcpy(g_srv_request, buf, n + 1);
        if (g_srv_delay_ms) {
            usleep((useconds_t)g_srv_delay_ms * 1000);
        }
        const char *body = "{\"response\":\"hi \\u00e4\"}";
        char resp[512];
        snprintf(resp, sizeof resp,
                 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                 strlen(body), body);
        ssize_t w = write(c, resp, strlen(resp));
        (void)w;
        close(c);
    }
}

static int start_server(void) {
    signal(SIGPIPE, SIG_IGN); /* Antwort auf eine abgebrochene Verbindung */
    g_srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    socklen_t l = sizeof a;
    if (g_srv_fd < 0 || bind(g_srv_fd, (struct sockaddr *)&a, sizeof a) != 0 || listen(g_srv_fd, 4) != 0 ||
        getsockname(g_srv_fd, (struct sockaddr *)&a, &l) != 0) {
        return 0;
    }
    g_srv_port = ntohs(a.sin_port);
    pthread_t t;
    return pthread_create(&t, NULL, server_thread, NULL) == 0;
}

static int http_calls = 0, http_status = -1, http_main_thread = 0;
static unsigned long http_id = 0;
static char http_body[256];
static void http_cb(unsigned long id, int status, const char *body, size_t len) {
    http_calls++;
    http_id = id;
    http_status = status;
    http_main_thread = [NSThread isMainThread];
    snprintf(http_body, sizeof http_body, "%.*s", (int)len, body ? body : "");
}

static int idle_calls = 0;
static void idle_cb(void) { idle_calls++; }

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
        it = find_item(menubar, @"/", cmd);
        CHECK(it && [it tag] == BTN_MENU_TOGGLE_COMMENT, "Cmd+/ = Toggle Comment");
        it = find_item(menubar, @"D", cmd);
        CHECK(it && [it tag] == BTN_MENU_DUPLICATE_LINES, "Shift+Cmd+D = Duplicate Lines");
        NSEventModifierFlags optcmd = NSEventModifierFlagOption | cmd;
        it = find_item(menubar, @"[", optcmd);
        CHECK(it && [it tag] == BTN_MENU_MOVE_LINES_UP, "Option+Cmd+[ = Move Lines Up");
        it = find_item(menubar, @"]", optcmd);
        CHECK(it && [it tag] == BTN_MENU_MOVE_LINES_DOWN, "Option+Cmd+] = Move Lines Down");
        it = find_item(menubar, @"i", optcmd);
        CHECK(it && [it tag] == BTN_MENU_SHOW_INVISIBLES && [it state] == NSControlStateValueOff, "Option+Cmd+I = Show Invisibles (off)");
        btn_app_set_show_invisibles_menu(1);
        CHECK(it && [it state] == NSControlStateValueOn, "Show Invisibles checkmark on");
        btn_app_set_show_invisibles_menu(0);
        CHECK(it && [it state] == NSControlStateValueOff, "and off again");
        NSMenuItem *aiItem = nil;
        for (NSMenuItem *m in [[[menubar itemAtIndex:2] submenu] itemArray]) {
            if ([m tag] == BTN_MENU_AI_COMPLETION) {
                aiItem = m;
            }
        }
        CHECK(aiItem && [aiItem state] == NSControlStateValueOff, "Edit menu: AI completion item (off)");
        btn_app_set_ai_menu(1);
        CHECK(aiItem && [aiItem state] == NSControlStateValueOn, "AI completion checkmark on");
        btn_app_set_ai_menu(0);
        NSMenu *modelMenu = nil;
        for (NSMenuItem *m in [[[menubar itemAtIndex:2] submenu] itemArray]) {
            if ([m submenu]) {
                modelMenu = [m submenu];
            }
        }
        CHECK(modelMenu && [modelMenu numberOfItems] == 4 && [[modelMenu itemAtIndex:2] tag] == BTN_MENU_AI_MODELS_REFRESH &&
                  [[modelMenu itemAtIndex:3] tag] == BTN_MENU_AI_TEST,
              "AI model submenu: status, separator, refresh, test (%ld items)", (long)[modelMenu numberOfItems]);
        const char *models[] = {"qwen2.5-coder:7b", "qwen3.6-coder:latest", "qwen3.6:35b-a3b"};
        btn_app_set_ai_model_menu("3 Modelle", models, 3, 1);
        [modelMenu update];
        CHECK([modelMenu numberOfItems] == 8 && [[[modelMenu itemAtIndex:0] title] isEqualToString:@"3 Modelle"] &&
                  ![[modelMenu itemAtIndex:0] isEnabled],
              "status line shown, not clickable");
        NSMenuItem *mi0 = [modelMenu itemAtIndex:2], *mi1 = [modelMenu itemAtIndex:3], *mi2 = [modelMenu itemAtIndex:4];
        CHECK([mi0 tag] == BTN_MENU_AI_MODEL_BASE && [mi1 tag] == BTN_MENU_AI_MODEL_BASE + 1 && [mi2 tag] == BTN_MENU_AI_MODEL_BASE + 2,
              "model items tagged by index (%ld %ld %ld)", (long)[mi0 tag], (long)[mi1 tag], (long)[mi2 tag]);
        CHECK([mi1 state] == NSControlStateValueOn && [mi0 state] == NSControlStateValueOff && [mi2 state] == NSControlStateValueOff,
              "the chosen model checked (%ld %ld %ld)", (long)[mi0 state], (long)[mi1 state], (long)[mi2 state]);
        CHECK([[mi0 title] isEqualToString:@"qwen2.5-coder:7b"] && [[mi2 title] isEqualToString:@"qwen3.6:35b-a3b"],
              "model titles (%s, %s)", [[mi0 title] UTF8String], [[mi2 title] UTF8String]);
        CHECK([mi1 target] != nil && [mi1 action] == NSSelectorFromString(@"menuAction:"), "model items reach the menu handler");
        btn_app_set_ai_model_menu(NULL, NULL, 0, -1);
        CHECK([modelMenu numberOfItems] == 2, "no status, no models: only refresh and test");

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

        /* ---- Autoscroll-Takt beim Markieren ----
         * Kuenstliche Events druecken keine echte Taste: der Hook sagt dem
         * Takt, die Taste sei gedrueckt. Kein App Nap, damit der 50-ms-Timer
         * auf einer ausgelasteten CI-Maschine nicht gedrosselt wird. */
        id activity = [[NSProcessInfo processInfo]
            beginActivityWithOptions:NSActivityUserInitiated | NSActivityLatencyCritical reason:@"timer test"];
        btn_shim_test_set_mouse_button(1);
        btn_app_set_mouse_callback(mouse_cb);
        btn_app_set_autoscroll(1);
        spin(0.2);
        CHECK(ticks == 0, "no autoscroll without a pressed mouse button");
        [view mouseDown:mouse_event(win, NSEventTypeLeftMouseDown, NSMakePoint(50, 200))];
        [view mouseDragged:mouse_event(win, NSEventTypeLeftMouseDragged, NSMakePoint(60, 350))];
        btn_app_set_autoscroll(1);
        spin(0.4);
        CHECK(ticks >= 2 && tick_x == 60 && tick_y == 350, "ticks with the last mouse position (%d, %.0f/%.0f)", ticks, tick_x, tick_y);
        int t0 = ticks;
        for (int q = 0; q < 15; q++) {
            btn_app_set_autoscroll(1); /* wie bei jeder Mausbewegung */
            spin(0.02);
        }
        CHECK(ticks - t0 >= 2, "switching on again keeps the running timer (%d ticks)", ticks - t0);
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

        /* mouseUp verloren (Loslassen waehrend eines modalen Dialogs): der
         * Takt merkt die losgelassene Taste, meldet Mouse-up und endet */
        [view mouseDown:mouse_event(win, NSEventTypeLeftMouseDown, NSMakePoint(50, 200))];
        btn_app_set_autoscroll(1);
        spin(0.2);
        int u0 = ups, t3 = ticks;
        CHECK(t3 > t2, "ticking again");
        btn_shim_test_set_mouse_button(0);
        spin(0.3);
        int t4 = ticks;
        CHECK(ups == u0 + 1, "released button noticed: one mouse-up (%d)", ups - u0);
        spin(0.2);
        CHECK(ticks == t4, "and the autoscroll stops");
        btn_shim_test_set_mouse_button(-1);

        /* Wiederholungs-Timer (Wiederherstellung, Dateien pruefen) */
        btn_app_start_repeating_timer(0.1, timer_cb);
        spin(0.55);
        CHECK(timer_calls >= 2, "repeating timer fires (%d)", timer_calls);

        /* ---- HTTP-POST an einen lokalen Server ---- */
        CHECK(start_server(), "local test server started (port %d)", g_srv_port);
        char url[128];
        snprintf(url, sizeof url, "http://127.0.0.1:%d/api/generate", g_srv_port);
        unsigned long rid = btn_http_post_json(url, "{\"a\":1}", 7, 5.0, http_cb);
        for (int q = 0; q < 100 && http_calls == 0; q++) {
            spin(0.05);
        }
        CHECK(rid != 0 && http_calls == 1 && http_id == rid && http_status == 200, "POST answered (%d calls, status %d)", http_calls, http_status);
        CHECK(http_main_thread, "callback on the main thread");
        CHECK(strstr(http_body, "\"response\"") != NULL, "response body delivered (%s)", http_body);
        CHECK(strncmp(g_srv_request, "POST /api/generate ", 19) == 0 && strcasestr(g_srv_request, "Content-Type: application/json") &&
                  strstr(g_srv_request, "{\"a\":1}"),
              "server saw POST, JSON content type and the body");
        g_srv_delay_ms = 600;
        unsigned long rid2 = btn_http_post_json(url, "{}", 2, 5.0, http_cb);
        spin(0.1);
        btn_http_cancel(rid2);
        spin(1.2);
        CHECK(rid2 != 0 && http_calls == 1, "cancelled request: no callback");
        g_srv_delay_ms = 0;
        snprintf(url, sizeof url, "http://127.0.0.1:1/x");
        unsigned long rid3 = btn_http_post_json(url, "{}", 2, 2.0, http_cb);
        for (int q = 0; q < 60 && http_calls == 1; q++) {
            spin(0.05);
        }
        CHECK(http_calls == 2 && http_id == rid3 && http_status == 0, "connection refused: callback with status 0");
        CHECK(btn_http_post_json("", "{}", 2, 1.0, http_cb) == 0, "empty URL rejected");

        /* ---- Tipp-Pause-Timer ---- */
        btn_app_restart_idle_timer(0.4, idle_cb);
        spin(0.1);
        btn_app_restart_idle_timer(0.4, idle_cb); /* neu gestartet */
        spin(0.15);
        CHECK(idle_calls == 0, "restarted timer has not fired yet");
        for (int q = 0; q < 40 && idle_calls == 0; q++) {
            spin(0.05);
        }
        spin(0.5);
        CHECK(idle_calls == 1, "fires once after the pause (%d)", idle_calls);
        btn_app_restart_idle_timer(0.1, idle_cb);
        btn_app_restart_idle_timer(-1, NULL);
        spin(0.4);
        CHECK(idle_calls == 1, "stopped timer does not fire");

        [[NSProcessInfo processInfo] endActivity:activity];

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
