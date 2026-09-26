/* KI-Vervollstaendigung in main.c (Funktionen verbatim, Netzwerk/Timer als
 * Stubs): wann gefragt wird und mit welchem Kontext, veraltete und
 * fehlerhafte Antworten, Geistertext, Tab (ein Undo-Schritt),
 * Weitertippen, Abbrechen, Einstellungsdatei. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ai.h"
#include "editor.h"
#include "eol.h"
#include "gapbuffer.h"
#include "textinput.h"
#include "filestamp.h"
#include "doc_extracted.h"
#include "focus_extracted.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 40) { printf("FAIL: " __VA_ARGS__); printf("\n"); } fails++; } } while (0)

/* ---- Zustand aus main.c ---- */
static Document g_doc;
static BtnFocus g_focus = BTN_FOCUS_DOCUMENT;
static BtnMarkedText g_marked;
static BtnAiConfig g_ai;
static unsigned long g_ai_request = 0;
static size_t g_ai_request_seq, g_ai_request_cursor;
static size_t g_ai_last_seq = (size_t)-1;
static struct {
    char *text;
    size_t len;
    size_t seq, cursor;
} g_ghost;
static Document *active_doc(void) { return &g_doc; }

/* ---- Stubs: Netzwerk, Timer, Menue ---- */
typedef void (*btn_http_callback)(unsigned long id, int status, const char *body, size_t len);
typedef void (*btn_void_callback)(void);
static unsigned long g_next_id = 1, g_cancelled = 0;
static int g_posts = 0, g_timer_starts = 0, g_timer_stops = 0, g_menu_state = -1, g_redraws = 0;
static char g_post_url[256];
static char *g_post_body = NULL;
static size_t g_post_len = 0;
static double g_timer_seconds = 0;
static btn_void_callback g_timer_cb = NULL;
static btn_http_callback g_post_cb = NULL;

static unsigned long btn_http_post_json(const char *url, const char *body, size_t len, double timeout, btn_http_callback cb) {
    (void)timeout;
    snprintf(g_post_url, sizeof g_post_url, "%s", url);
    free(g_post_body);
    g_post_body = malloc(len + 1);
    memcpy(g_post_body, body, len);
    g_post_body[len] = 0;
    g_post_len = len;
    g_post_cb = cb;
    g_posts++;
    return g_next_id++;
}
static void btn_http_cancel(unsigned long id) { g_cancelled = id; }
static void btn_app_restart_idle_timer(double seconds, btn_void_callback cb) {
    if (seconds < 0) {
        g_timer_stops++;
        return;
    }
    g_timer_seconds = seconds;
    g_timer_cb = cb;
    g_timer_starts++;
}
static void btn_app_set_ai_menu(int on) { g_menu_state = on; }
static void btn_app_request_redraw(void) { g_redraws++; }

#include "ai_glue_extracted.h"

/* ---- Hilfen ---- */
static Editor *ed = &g_doc.editor;

static void set_doc(const char *text, size_t cursor) {
    editor_set_text(ed, text, strlen(text));
    editor_set_cursor(ed, cursor, 0);
}

static void respond(int status, const char *body) {
    ai_on_response(g_next_id - 1, status, body, body ? strlen(body) : 0);
}

static int prompt_is(const char *key, const char *want, size_t want_len) {
    char *v;
    size_t n;
    if (!btn_ai_json_get_string(g_post_body, g_post_len, key, &v, &n)) {
        return 0;
    }
    int ok = n == want_len && memcmp(v, want, n) == 0;
    free(v);
    return ok;
}

static int text_is(const char *want) {
    size_t len;
    char *t = editor_copy_all(ed, &len);
    int ok = len == strlen(want) && memcmp(t, want, len) == 0;
    free(t);
    return ok;
}

static void test_flow(void) {
    btn_ai_config_defaults(&g_ai);
    set_doc("int f() {\n    retu", 18);

    /* aus: nichts */
    ai_note_typing();
    ai_on_idle();
    CHECK(g_timer_starts == 0 && g_posts == 0, "disabled: no timer, no request");

    g_ai.enabled = 1;
    ai_note_typing();
    CHECK(g_timer_starts == 1 && g_timer_seconds == 0.3 && g_timer_cb == ai_on_idle, "typing starts the 300 ms pause timer");
    ai_on_idle();
    CHECK(g_posts == 1 && strcmp(g_post_url, "http://127.0.0.1:11434/api/generate") == 0, "pause over: request to Ollama");
    CHECK(prompt_is("prompt", "int f() {\n    retu", 18) && prompt_is("suffix", "\n", 1),
          "context: text before the cursor; at the end of the file the suffix is a newline (Ollama needs one for FIM)");
    ai_on_idle();
    CHECK(g_posts == 1, "request pending: no second one");

    respond(200, "{\"response\":\"rn 0;\\n}\",\"done\":true}");
    CHECK(ghost_visible() && g_ghost.len == 5 && strcmp(g_ghost.text, "rn 0;") == 0 && g_ai_request == 0,
          "answer: one-line ghost text");
    ai_on_idle();
    CHECK(g_posts == 1, "same text, ghost showing: no new request");

    /* Tab: uebernehmen, ein Undo-Schritt, getrennt vom Tippen davor */
    editor_insert_text(ed, "", 0);
    ai_accept(ed);
    CHECK(text_is("int f() {\n    return 0;") && !ghost_visible() && g_ghost.text == NULL, "Tab inserts the suggestion");
    editor_undo(ed);
    CHECK(text_is("int f() {\n    retu"), "one undo step removes exactly the suggestion");

    /* Getipptes davor bleibt ein eigener Undo-Schritt */
    set_doc("", 0);
    editor_insert_text(ed, "r", 1);
    editor_insert_text(ed, "e", 1);
    ai_on_idle();
    respond(200, "{\"response\":\"turn\"}");
    ai_accept(ed);
    editor_insert_text(ed, "!", 1);
    CHECK(text_is("return!"), "accepted, typed on");
    editor_undo(ed);
    CHECK(text_is("return"), "undo: the typed '!' alone");
    editor_undo(ed);
    CHECK(text_is("re"), "undo: the suggestion alone, typing before it stays");

    /* Weitertippen des Vorschlags */
    set_doc("x = ", 4);
    ai_on_idle();
    respond(200, "{\"response\":\"foo(1)\"}");
    CHECK(ghost_visible(), "ghost for 'foo(1)'");
    size_t c0 = ed->cursor, l0 = editor_length(ed);
    editor_insert_text(ed, "fo", 2);
    ai_keep_ghost_after_typing(ed, c0, l0);
    CHECK(ghost_visible() && strcmp(g_ghost.text, "o(1)") == 0, "typing the suggestion keeps the rest (%s)", g_ghost.text ? g_ghost.text : "-");
    c0 = ed->cursor;
    l0 = editor_length(ed);
    editor_insert_text(ed, "x", 1);
    ai_keep_ghost_after_typing(ed, c0, l0);
    CHECK(!ghost_visible() && g_ghost.len == 0, "typing something else drops it");
    ai_on_idle();
    respond(200, "{\"response\":\"ab\"}");
    c0 = ed->cursor;
    l0 = editor_length(ed);
    editor_insert_text(ed, "ab", 2);
    ai_keep_ghost_after_typing(ed, c0, l0);
    CHECK(g_ghost.len == 0, "typing all of it: nothing left");

    /* Cursorbewegung / Aenderung: nicht mehr sichtbar */
    set_doc("y = ", 4);
    ai_on_idle();
    respond(200, "{\"response\":\"1\"}");
    CHECK(ghost_visible(), "ghost");
    editor_set_cursor(ed, 0, 0);
    CHECK(!ghost_visible(), "cursor moved: gone");
    editor_set_cursor(ed, 4, 0);
    editor_set_cursor(ed, 2, 1);
    CHECK(!ghost_visible(), "selection: gone");
    editor_set_cursor(ed, 4, 0);
    g_focus = BTN_FOCUS_SEARCH;
    CHECK(!ghost_visible(), "find field focused: gone");
    g_focus = BTN_FOCUS_DOCUMENT;
    btn_marked_set(&g_marked, "n", 1, 1, 0);
    CHECK(!ghost_visible(), "input method composing: gone");
    btn_marked_clear(&g_marked);
    CHECK(ghost_visible(), "all back: visible again");
    ghost_clear();

    /* Veraltete, falsche und fehlerhafte Antworten */
    set_doc("z = ", 4);
    ai_on_idle();
    unsigned long id = g_next_id - 1;
    editor_insert_text(ed, "q", 1);
    ai_on_response(id, 200, "{\"response\":\"1\"}", 16);
    CHECK(g_ghost.len == 0, "text changed meanwhile: answer ignored");
    ai_on_idle();
    ai_on_response(12345, 200, "{\"response\":\"1\"}", 16);
    CHECK(g_ghost.len == 0 && g_ai_request != 0, "unknown id ignored");
    respond(500, "{\"response\":\"1\"}");
    CHECK(g_ghost.len == 0 && g_ai_request == 0, "HTTP 500: nothing");
    editor_insert_text(ed, "r", 1);
    ai_on_idle();
    respond(0, NULL);
    CHECK(g_ghost.len == 0, "network error: nothing");
    editor_insert_text(ed, "s", 1);
    ai_on_idle();
    respond(200, "{\"error\":\"model not found\"}");
    CHECK(g_ghost.len == 0, "error object: nothing");
    editor_insert_text(ed, "t", 1);
    ai_on_idle();
    respond(200, "{\"response\":\"  \\n\"}");
    CHECK(g_ghost.len == 0, "empty suggestion: nothing");

    /* Tippen waehrend einer Anfrage bricht sie ab - ohne Antwort wird
     * derselbe Text danach erneut gefragt (z.B. nach einer Pfeiltaste) */
    editor_insert_text(ed, "u", 1);
    ai_on_idle();
    id = g_ai_request;
    int posts_before = g_posts;
    ai_note_typing();
    CHECK(g_cancelled == id && g_ai_request == 0, "typing cancels the pending request");
    ai_on_idle();
    CHECK(g_posts == posts_before + 1, "cancelled without answer: asked again for the same text");
    respond(0, NULL);

    /* Tab-Wechsel/Oeffnen: Pause und Anfrage verworfen */
    editor_insert_text(ed, "v", 1);
    ai_note_typing();
    ai_on_idle();
    id = g_ai_request;
    int stops = g_timer_stops;
    ai_cancel();
    CHECK(g_cancelled == id && g_ai_request == 0 && g_timer_stops == stops + 1, "ai_cancel: request cancelled, timer stopped");

    /* Wann nicht gefragt wird */
    int before = g_posts;
    set_doc("foo(bar)", 4);
    ai_on_idle();
    CHECK(g_posts == before, "text right of the cursor: no request");
    set_doc("foo(bar)", 7);
    ai_on_idle();
    CHECK(g_posts == before + 1 && prompt_is("suffix", ")", 1), "only ')' right of it: request with suffix");
    respond(200, "{\"response\":\"x)\"}");
    CHECK(g_ghost.len == 1 && g_ghost.text[0] == 'x', "overlap with ')' trimmed");
    ghost_clear();
    before = g_posts;
    set_doc("abc", 3);
    editor_set_cursor(ed, 0, 1);
    ai_on_idle();
    CHECK(g_posts == before, "selection: no request");
    set_doc("abc", 3);
    btn_marked_set(&g_marked, "n", 1, 1, 0);
    ai_on_idle();
    btn_marked_clear(&g_marked);
    CHECK(g_posts == before, "composing: no request");
    g_focus = BTN_FOCUS_REPLACE;
    ai_on_idle();
    g_focus = BTN_FOCUS_DOCUMENT;
    CHECK(g_posts == before, "find field: no request");
    g_doc.binary = 1;
    ai_on_idle();
    g_doc.binary = 0;
    CHECK(g_posts == before, "binary file: no request");
    ai_on_idle();
    CHECK(g_posts == before + 1, "otherwise: request");
    respond(0, NULL);
    ai_on_idle();
    CHECK(g_posts == before + 1, "same text again after a failed answer: no repeat");

    /* Kontext auf Zeichengrenzen, begrenzt */
    size_t big = BTN_AI_PREFIX_BYTES * 3 + 4; /* Kontextanfang faellt mitten in ein 'ä' */
    char *t = malloc(big + 1);
    for (size_t i = 0; i < big;) {
        const char *ch = (i % 5 == 0 && i + 2 <= big) ? "\xC3\xA4" : "a";
        size_t l = strlen(ch);
        memcpy(t + i, ch, l);
        i += l;
    }
    t[big] = 0;
    set_doc(t, big);
    ai_on_idle();
    char *v;
    size_t n;
    CHECK(btn_ai_json_get_string(g_post_body, g_post_len, "prompt", &v, &n) && n <= BTN_AI_PREFIX_BYTES && n > BTN_AI_PREFIX_BYTES - 4 &&
              !strstr(v, "\xEF\xBF\xBD") && memcmp(v, t + big - n, n) == 0,
          "prefix limited to %d bytes, starts on a character (%zu)", BTN_AI_PREFIX_BYTES, n);
    free(v);
    respond(0, NULL);
    set_doc(t, 0);
    memset(t, ')', big);
    set_doc(t, 0);
    ai_on_idle();
    CHECK(btn_ai_json_get_string(g_post_body, g_post_len, "suffix", &v, &n) && n == BTN_AI_SUFFIX_BYTES, "suffix limited to %d bytes", BTN_AI_SUFFIX_BYTES);
    free(v);
    respond(0, NULL);
    free(t);
}

static void test_config_file(void) {
    char home[] = "/tmp/btn_ai_XXXXXX";
    if (!mkdtemp(home)) {
        return;
    }
    setenv("HOME", home, 1);
    g_ai.enabled = 1;
    load_ai_config();
    CHECK(!g_ai.enabled && g_menu_state == 0, "no file: defaults (off), menu unchecked");
    char path[300];
    snprintf(path, sizeof path, "%s/.btnedit_ai", home);
    FILE *f = fopen(path, "w");
    fputs("enabled=1\napi=llama\nurl=http://10.0.0.2:8080\n", f);
    fclose(f);
    load_ai_config();
    CHECK(g_ai.enabled && g_ai.api == BTN_AI_API_LLAMA && strcmp(g_ai.url, "http://10.0.0.2:8080") == 0 && g_menu_state == 1,
          "file read, menu checked");
    /* Umschalten liest die Datei neu und aendert nur enabled */
    f = fopen(path, "w");
    fputs("# meine Notiz\nenabled = 1\nmodel=custom:7b\nfuture_key=42\n", f);
    fclose(f);
    toggle_ai_config();
    CHECK(!g_ai.enabled && strcmp(g_ai.model, "custom:7b") == 0 && g_menu_state == 0, "toggle off: file re-read (model from the file)");
    f = fopen(path, "r");
    char buf[256];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    CHECK(strcmp(buf, "# meine Notiz\nenabled=0\nmodel=custom:7b\nfuture_key=42\n") == 0, "only the enabled line changed (%s)", buf);
    toggle_ai_config();
    CHECK(g_ai.enabled && g_menu_state == 1, "toggle on again");
    unlink(path);
    toggle_ai_config();
    CHECK(g_ai.enabled && g_menu_state == 1, "no file: toggled from defaults (on)");
    load_ai_config();
    CHECK(g_ai.enabled && strcmp(g_ai.model, "qwen2.5-coder:1.5b") == 0, "and a complete file was written");
    unlink(path);
    rmdir(home);
}

int main(void) {
    editor_init(ed);
    test_flow();
    test_config_file();
    ghost_clear();
    editor_free(ed);
    free(g_post_body);
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
