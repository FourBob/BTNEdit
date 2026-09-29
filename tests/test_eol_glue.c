/* Zeilenenden im Zusammenspiel von main.c: open_file_path(), perform_save_doc()
 * und set_doc_line_ending() verbatim aus main.c, mit echten Dateien. Prueft:
 * einheitliche Dateien bytegleich zurueck, gemischte und Binaerdateien
 * unangetastet, Aenderungen im richtigen Format, Umstellen per Menue
 * (ungesichert, ein Undo-Schritt, Cursor bleibt an seiner Textstelle). */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "editor.h"
#include "eol.h"
#include "gapbuffer.h"
#include "strings.h"
#include "filestamp.h"
#include "doc_extracted.h"

/* ---- Stubs fuer das, was die extrahierten Funktionen sonst anfassen ---- */
static int g_open_anyway = 1;      /* Antwort auf die Binaerdatei-Warnung */
static int g_binary_warnings = 0;
static int g_error_alerts = 0;
static const char *g_save_as_path = NULL;

const char *btn_tr(BtnStringId id) { return id == BTN_STR_FILE_TOO_LARGE_INFO_FMT ? "%d" : "%s"; }
static int btn_show_binary_file_warning(const char *name) { (void)name; g_binary_warnings++; return g_open_anyway; }
static void btn_show_error_alert(const char *t, const char *i) { (void)t; (void)i; g_error_alerts++; }
static char *btn_show_save_panel(const char *p) { (void)p; return g_save_as_path ? strdup(g_save_as_path) : NULL; }
static void set_doc_path(Document *d, const char *path) {
    char *copy = path ? strdup(path) : NULL;
    free(d->path);
    d->path = copy;
}
static void add_recent_file(const char *path) { (void)path; }
static const char *doc_display_name(Document *d) { (void)d; return "doc"; }
/* Konflikt beim Sichern: trotzdem sichern (eigener Test: test_protect);
 * Kodierung: g_choice_answer (1 = erster Knopf), gezaehlt */
static int g_choice_answer = 1, g_choices = 0;
static int btn_show_choice_alert(const char *t, const char *i, const char *a, const char *b, int esc) {
    (void)t; (void)i; (void)a; (void)b; (void)esc;
    g_choices++;
    return g_choice_answer;
}

#include "eol_glue_extracted.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 25) { printf("FAIL: " __VA_ARGS__); printf("\n"); } fails++; } } while (0)

static char g_dir[] = "/tmp/btn_eolglue_XXXXXX";

static void write_bytes(const char *path, const char *b, size_t n) {
    FILE *f = fopen(path, "wb");
    fwrite(b, 1, n, f);
    fclose(f);
}
static char *read_bytes(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) { *n = 0; return NULL; }
    char *b = malloc(1 << 20);
    *n = fread(b, 1, 1 << 20, f);
    fclose(f);
    return b;
}
static int file_is(const char *path, const char *b, size_t n) {
    size_t got;
    char *d = read_bytes(path, &got);
    int ok = d && got == n && memcmp(d, b, n) == 0;
    free(d);
    return ok;
}
static int buffer_is(Document *d, const char *b, size_t n) {
    size_t got;
    char *t = editor_copy_all(&d->editor, &got);
    int ok = got == n && memcmp(t, b, n) == 0;
    free(t);
    return ok;
}

/* wie add_tab() in main.c */
static void doc_init(Document *d) {
    memset(d, 0, sizeof(*d));
    editor_init(&d->editor);
    d->eol = BTN_EOL_LF;
    mark_doc_saved(d);
}
static void doc_done(Document *d) {
    editor_free(&d->editor);
    free(d->path);
}
static void path_for(char *out, const char *name) { snprintf(out, 512, "%s/%s", g_dir, name); }

/* Laden und unveraendert sichern ergibt dieselben Bytes. */
static void check_untouched_roundtrip(const char *name, const char *bytes, size_t n, int want_raw) {
    char p[512];
    path_for(p, name);
    write_bytes(p, bytes, n);
    Document d;
    doc_init(&d);
    open_file_path(&d, p);
    CHECK(d.path && !doc_is_dirty(&d), "%s: opened clean", name);
    CHECK(d.eol_raw == want_raw, "%s: raw=%d want %d", name, d.eol_raw, want_raw);
    if (!want_raw) {
        size_t len;
        char *t = editor_copy_all(&d.editor, &len);
        CHECK(!memchr(t, '\r', len), "%s: buffer has no CR", name);
        free(t);
    } else {
        CHECK(buffer_is(&d, bytes, n), "%s: raw buffer equals file bytes", name);
    }
    CHECK(perform_save_doc(&d, 0) == 1, "%s: save ok", name);
    CHECK(file_is(p, bytes, n), "%s: untouched save is byte-identical", name);
    doc_done(&d);
}

static void test_untouched(void) {
    struct { const char *name, *b; int raw; } c[] = {
        { "lf.txt", "one\ntwo\nthree\n", 0 },          { "crlf.txt", "one\r\ntwo\r\nthree", 0 },
        { "cr.txt", "one\rtwo\rthree\r", 0 },          { "none.txt", "no line ending at all", 0 },
        { "empty.txt", "", 0 },                         { "umlaut.txt", "gr\xC3\xBC\xC3\x9F\r\nLatin-1 \xE4\r\n", 0 },
        { "progress.log", "10%\r20%\r30%\rdone\nnext\n", 1 },
        { "patch.diff", "a\nb\n-x\r\n+y\r\nc\n", 1 },
        { "crcrlf.txt", "a\r\r\nb\r\n", 1 },            { "trailing_cr.txt", "a\nb\n\r", 1 },
    };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        check_untouched_roundtrip(c[i].name, c[i].b, strlen(c[i].b), c[i].raw);
    }
}

/* Binaerdatei mit NUL erst nach 8 KiB (frueher nur die ersten 8 KiB
 * geprueft) und CRLF: Warnung, roh, bytegleich, Menue wirkungslos. */
static void test_binary(void) {
    static const char tail[] = "x\r\ny\0z\r\n\rq\n\r\n\r\n\r\n\r";
    size_t n = 9000 + sizeof tail - 1;
    char *b = malloc(n);
    memset(b, 'a', 9000);
    memcpy(b + 9000, tail, sizeof tail - 1);
    char p[512];
    path_for(p, "late_nul.bin");
    write_bytes(p, b, n);

    g_open_anyway = 0;
    g_binary_warnings = 0;
    Document d;
    doc_init(&d);
    open_file_path(&d, p);
    CHECK(g_binary_warnings == 1 && d.path == NULL && editor_length(&d.editor) == 0, "binary: 'Cancel' leaves doc untouched");
    doc_done(&d);

    g_open_anyway = 1;
    doc_init(&d);
    open_file_path(&d, p);
    CHECK(d.binary && d.eol_raw && buffer_is(&d, b, n), "binary: NUL after 8 KiB detected, raw buffer");
    set_doc_line_ending(&d, BTN_EOL_CRLF);
    CHECK(!doc_is_dirty(&d) && d.eol_raw && buffer_is(&d, b, n), "binary: line-ending menu has no effect");
    CHECK(perform_save_doc(&d, 0) == 1 && file_is(p, b, n), "binary: save byte-identical");
    doc_done(&d);
    free(b);
}

/* Aenderung in einer CRLF-Datei wird wieder als CRLF geschrieben. */
static void test_edit_keeps_format(void) {
    char p[512];
    path_for(p, "edit_crlf.txt");
    const char *orig = "a\r\nb\r\n";
    write_bytes(p, orig, strlen(orig));
    Document d;
    doc_init(&d);
    open_file_path(&d, p);
    editor_set_cursor(&d.editor, 0, 0);
    editor_insert_text(&d.editor, "new\n", 4);
    CHECK(doc_is_dirty(&d), "edit makes dirty");
    CHECK(perform_save_doc(&d, 0) == 1 && file_is(p, "new\r\na\r\nb\r\n", 11), "edit saved as CRLF");
    CHECK(!doc_is_dirty(&d), "clean after save");
    doc_done(&d);

    /* gemischte Datei: Aenderung bleibt, der Rest unangetastet */
    path_for(p, "edit_mixed.log");
    const char *mixed = "10%\r20%\rdone\n";
    write_bytes(p, mixed, strlen(mixed));
    doc_init(&d);
    open_file_path(&d, p);
    editor_set_cursor(&d.editor, 0, 0);
    editor_insert_text(&d.editor, "X", 1);
    CHECK(perform_save_doc(&d, 0) == 1 && file_is(p, "X10%\r20%\rdone\n", 14), "mixed file: only the edit changes");
    doc_done(&d);
}

/* Umstellen per Menue: ungesichert, zurueckstellen macht wieder sauber. */
static void test_switch_format(void) {
    char p[512];
    path_for(p, "switch.txt");
    write_bytes(p, "a\nb\n", 4);
    Document d;
    doc_init(&d);
    open_file_path(&d, p);
    set_doc_line_ending(&d, BTN_EOL_CRLF);
    CHECK(doc_is_dirty(&d), "switch LF->CRLF is unsaved");
    set_doc_line_ending(&d, BTN_EOL_LF);
    CHECK(!doc_is_dirty(&d), "switching back to the saved format is clean again");
    set_doc_line_ending(&d, BTN_EOL_CR);
    CHECK(perform_save_doc(&d, 0) == 1 && file_is(p, "a\rb\r", 4) && !doc_is_dirty(&d), "saved as CR");
    /* Sichern unter: Format bleibt */
    char q[512];
    path_for(q, "switch_copy.txt");
    g_save_as_path = q;
    CHECK(perform_save_doc(&d, 1) == 1 && file_is(q, "a\rb\r", 4), "save as keeps CR");
    g_save_as_path = NULL;
    doc_done(&d);
}

/* Gemischte Datei ausdruecklich vereinheitlichen. */
static void test_convert_mixed(void) {
    char p[512];
    path_for(p, "convert.txt");
    const char *orig = "a\r\nb\nc\r\nd";
    write_bytes(p, orig, strlen(orig));
    Document d;
    doc_init(&d);
    open_file_path(&d, p);
    CHECK(d.eol_raw && d.eol == BTN_EOL_CRLF, "mixed file raw, dominant CRLF");
    /* Selektion "c" (roh: Offset 5..6) */
    editor_set_cursor(&d.editor, 5, 0);
    editor_set_cursor(&d.editor, 6, 1);
    set_doc_line_ending(&d, BTN_EOL_CRLF);
    CHECK(doc_is_dirty(&d) && !d.eol_raw, "explicit choice on a mixed file is unsaved and no longer raw");
    CHECK(buffer_is(&d, "a\nb\nc\nd", 7), "buffer normalized");
    CHECK(d.editor.anchor == 4 && d.editor.cursor == 5, "selection still on 'c' (anchor %zu cursor %zu)",
          d.editor.anchor, d.editor.cursor);
    editor_undo(&d.editor);
    CHECK(buffer_is(&d, orig, strlen(orig)), "one undo restores the raw bytes");
    editor_redo(&d.editor);
    CHECK(buffer_is(&d, "a\nb\nc\nd", 7), "redo normalizes again");
    editor_undo(&d.editor);
    /* auch mit '\r' im Puffer (nach Undo) schreibt Sichern einheitlich */
    CHECK(perform_save_doc(&d, 0) == 1 && file_is(p, "a\r\nb\r\nc\r\nd", 10), "save after undo still uniform CRLF");
    doc_done(&d);
}

/* ---- Zeichenkodierung ---- */
static void test_encodings(void) {
    char p[512];
    Document d;
    /* Latin-1-Datei: als Windows-1252 erkannt, im Puffer UTF-8, bytegleich zurueck */
    path_for(p, "latin.txt");
    write_bytes(p, "gr\xFC\xDF\r\n\x80\r\n", 9);
    doc_init(&d);
    open_file_path(&d, p);
    CHECK(d.enc == BTN_ENC_WIN1252 && !d.binary && d.eol == BTN_EOL_CRLF && !doc_is_dirty(&d), "Latin-1 bytes: Windows-1252, CRLF");
    CHECK(buffer_is(&d, "gr\xC3\xBC\xC3\x9F\n\xE2\x82\xAC\n", 11), "buffer is UTF-8 (grüß, €)");
    CHECK(perform_save_doc(&d, 0) == 1 && file_is(p, "gr\xFC\xDF\r\n\x80\r\n", 9), "unchanged: byte-identical");
    /* getipptes ä landet als 0xE4, nicht als UTF-8 */
    editor_set_cursor(&d.editor, editor_length(&d.editor), 0);
    editor_insert_text(&d.editor, "\xC3\xA4", 2);
    CHECK(perform_save_doc(&d, 0) == 1 && file_is(p, "gr\xFC\xDF\r\n\x80\r\n\xE4", 10), "typed ä saved as one Windows-1252 byte");
    /* Kodierung umstellen: ungesichert; Latin-1 kennt € nicht -> Frage */
    set_doc_encoding(&d, BTN_ENC_LATIN1);
    CHECK(doc_is_dirty(&d), "switching the encoding is an unsaved change");
    g_choice_answer = 0;
    g_choices = 0;
    CHECK(perform_save_doc(&d, 0) == 0 && g_choices == 1 && file_is(p, "gr\xFC\xDF\r\n\x80\r\n\xE4", 10) && doc_is_dirty(&d),
          "€ not in Latin-1: asked, cancel keeps the file");
    g_choice_answer = 1;
    CHECK(perform_save_doc(&d, 0) == 1 && d.enc == BTN_ENC_UTF8 && !doc_is_dirty(&d) &&
              file_is(p, "gr\xC3\xBC\xC3\x9F\r\n\xE2\x82\xAC\r\n\xC3\xA4", 15),
          "'save as UTF-8': everything kept, encoding now UTF-8");
    doc_done(&d);

    /* UTF-16 LE mit BOM: kein Binaer-Alarm, Zeilenenden erkannt, bytegleich */
    static const char u16[] = "\xFF\xFE" "a\0\r\0\n\0\xE4\0";
    path_for(p, "u16.txt");
    write_bytes(p, u16, 10);
    int warnings = g_binary_warnings;
    doc_init(&d);
    open_file_path(&d, p);
    CHECK(d.enc == BTN_ENC_UTF16LE && !d.binary && g_binary_warnings == warnings && d.eol == BTN_EOL_CRLF &&
              buffer_is(&d, "a\n\xC3\xA4", 4),
          "UTF-16 LE: decoded, not binary, CRLF");
    CHECK(perform_save_doc(&d, 0) == 1 && file_is(p, u16, 10), "UTF-16 LE: byte-identical");
    editor_insert_text(&d.editor, "\xF0\x9F\x98\x80", 4);
    CHECK(perform_save_doc(&d, 0) == 1 && file_is(p, "\xFF\xFE\x3D\xD8\x00\xDE" "a\0\r\0\n\0\xE4\0", 14), "emoji as a surrogate pair");
    doc_done(&d);
    /* kaputtes UTF-16 mit BOM: roh wie eine Binaerdatei */
    path_for(p, "bad16.txt");
    write_bytes(p, "\xFF\xFE\x00\xD8", 4);
    doc_init(&d);
    open_file_path(&d, p);
    CHECK(d.binary && d.enc == BTN_ENC_UTF8 && buffer_is(&d, "\xFF\xFE\x00\xD8", 4), "broken UTF-16: raw bytes, binary");
    CHECK(perform_save_doc(&d, 0) == 1 && file_is(p, "\xFF\xFE\x00\xD8", 4), "and saved unchanged");
    set_doc_encoding(&d, BTN_ENC_LATIN1);
    CHECK(d.enc == BTN_ENC_UTF8 && !doc_is_dirty(&d), "binary: encoding cannot be switched");
    doc_done(&d);
    /* UTF-8 mit BOM, gemischtes UTF-8 */
    path_for(p, "bom.txt");
    write_bytes(p, "\xEF\xBB\xBFx\n", 5);
    doc_init(&d);
    open_file_path(&d, p);
    CHECK(d.enc == BTN_ENC_UTF8_BOM && buffer_is(&d, "x\n", 2), "UTF-8 BOM: BOM not in the buffer");
    CHECK(perform_save_doc(&d, 0) == 1 && file_is(p, "\xEF\xBB\xBFx\n", 5), "BOM written again");
    doc_done(&d);
    path_for(p, "mixed.txt");
    write_bytes(p, "\xC3\xA4 \xFF\n", 5);
    doc_init(&d);
    open_file_path(&d, p);
    CHECK(d.enc == BTN_ENC_UTF8 && buffer_is(&d, "\xC3\xA4 \xFF\n", 5), "mixed UTF-8: bytes as they are");
    CHECK(perform_save_doc(&d, 0) == 1 && file_is(p, "\xC3\xA4 \xFF\n", 5), "and saved unchanged");
    doc_done(&d);

    /* Neu oeffnen als: die Erkennung lag daneben */
    path_for(p, "reopen.txt");
    write_bytes(p, "\xC3\xA4\n", 3);
    doc_init(&d);
    open_file_path(&d, p);
    CHECK(d.enc == BTN_ENC_UTF8, "UTF-8 detected");
    g_choices = 0;
    reopen_doc_as(&d, BTN_ENC_LATIN1);
    CHECK(g_choices == 0 && d.enc == BTN_ENC_LATIN1 && d.enc_chosen && buffer_is(&d, "\xC3\x83\xC2\xA4\n", 5) && !doc_is_dirty(&d),
          "reopened as Latin-1: no question without edits, clean");
    write_bytes(p, "\xC3\xA4\xC3\xA4\n", 5);
    reload_doc(&d);
    CHECK(d.enc == BTN_ENC_LATIN1 && buffer_is(&d, "\xC3\x83\xC2\xA4\xC3\x83\xC2\xA4\n", 9), "reload keeps the chosen encoding");
    write_bytes(p, "\xFF\xFE" "a\0", 4);
    d.enc = BTN_ENC_UTF16LE; /* gewaehlt, Datei passt */
    reload_doc(&d);
    CHECK(d.enc == BTN_ENC_UTF16LE && d.enc_chosen && buffer_is(&d, "a", 1), "chosen UTF-16 still fits: kept");
    write_bytes(p, "\xC3\xA4\xC3\xA4\n", 5);
    reload_doc(&d);
    CHECK(d.enc == BTN_ENC_UTF8 && !d.enc_chosen && buffer_is(&d, "\xC3\xA4\xC3\xA4\n", 5),
          "chosen encoding no longer fits (odd length for UTF-16): detected again");
    reopen_doc_as(&d, BTN_ENC_LATIN1);
    editor_insert_text(&d.editor, "x", 1);
    g_choice_answer = 0;
    reopen_doc_as(&d, BTN_ENC_UTF8);
    CHECK(g_choices == 1 && d.enc == BTN_ENC_LATIN1 && doc_is_dirty(&d), "unsaved edits: asked, cancel changes nothing");
    g_choice_answer = 1;
    reopen_doc_as(&d, BTN_ENC_UTF8);
    CHECK(g_choices == 2 && d.enc == BTN_ENC_UTF8 && buffer_is(&d, "\xC3\xA4\xC3\xA4\n", 5) && !doc_is_dirty(&d),
          "confirmed: reopened as UTF-8");
    int errors = g_error_alerts;
    reopen_doc_as(&d, BTN_ENC_UTF16BE); /* 5 Bytes: ungerade Laenge */
    CHECK(g_error_alerts == errors + 1 && d.enc == BTN_ENC_UTF8 && buffer_is(&d, "\xC3\xA4\xC3\xA4\n", 5),
          "does not fit UTF-16: message, document unchanged");
    g_error_alerts = errors;
    free(d.path);
    d.path = NULL;
    reopen_doc_as(&d, BTN_ENC_LATIN1);
    CHECK(d.enc == BTN_ENC_UTF8, "untitled: nothing to reopen");
    doc_done(&d);
    g_choice_answer = 1;
}

int main(void) {
    if (!mkdtemp(g_dir)) {
        return 2;
    }
    test_encodings();
    test_untouched();
    test_binary();
    test_edit_keeps_format();
    test_switch_format();
    test_convert_mixed();
    CHECK(g_error_alerts == 0, "no error alerts");
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf %s", g_dir);
    if (system(cmd) != 0) {
        printf("cleanup failed\n");
    }
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
