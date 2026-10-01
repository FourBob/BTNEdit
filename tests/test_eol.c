/* Zeilenenden (eol.c): Erkennung, Vereinheitlichung, Zurueckwandeln beim
 * Sichern - bytegenauer Round-Trip fuer LF/CRLF/CR, Umwandeln gemischter
 * Eingaben, Aufteilung in zwei Gap-Buffer-Haelften, exakte Einfuegelaenge
 * im Editor, Laufzeit bei 100 MB. Das Zusammenspiel in main.c (Laden,
 * Sichern, Menue) prueft test_eol_glue.c. */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "eol.h"
#include "editor.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 25) { printf("FAIL: " __VA_ARGS__); printf("\n"); } fails++; } } while (0)

static unsigned long rng = 2024;
static unsigned rnd(unsigned n) { rng = rng * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)((rng >> 33) % n); }

static void test_detect(void) {
    struct { const char *s; BtnEol want; int mixed; } c[] = {
        { "", BTN_EOL_LF, 0 },            { "abc", BTN_EOL_LF, 0 },
        { "a\n", BTN_EOL_LF, 0 },         { "a\r\n", BTN_EOL_CRLF, 0 },
        { "a\r", BTN_EOL_CR, 0 },         { "a\rb\rc", BTN_EOL_CR, 0 },
        { "a\r\nb\n", BTN_EOL_LF, 1 },    /* Gleichstand: LF */
        { "a\r\nb\r\nc\n", BTN_EOL_CRLF, 1 },
        { "a\r\r\n", BTN_EOL_CRLF, 1 },   /* CR + CRLF, Gleichstand: CRLF vor CR */
        { "\n\r", BTN_EOL_LF, 1 },        { "a\rb\rc\r\n", BTN_EOL_CR, 1 },
        { "\r\n\r\n\r\n", BTN_EOL_CRLF, 0 },
        /* '\r' als Nutzdaten - muss als gemischt erkannt werden, damit main.c
         * die Datei unveraendert laesst */
        { "10%\r20%\r30%\rdone\nnext\n", BTN_EOL_CR, 1 },   /* Fortschrittszeilen */
        { "a\nb\n-x\r\n+y\r\nc\n", BTN_EOL_LF, 1 },       /* Patch mit CRLF-Zeilen */
        { "a\nb\n\r", BTN_EOL_LF, 1 },                      /* '\r' am Dateiende */
    };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        int mixed = -1;
        BtnEol got = btn_eol_detect(c[i].s, strlen(c[i].s), &mixed);
        CHECK(got == c[i].want && mixed == c[i].mixed, "detect case %zu: got %s/%d want %s/%d", i,
              btn_eol_name(got), mixed, btn_eol_name(c[i].want), c[i].mixed);
    }
    CHECK(btn_eol_detect("a\r\n", 3, NULL) == BTN_EOL_CRLF, "detect with NULL mixed");
}

static void test_normalize_encode(void) {
    struct { const char *in, *out; } n[] = {
        { "a\r\nb\rc\n", "a\nb\nc\n" }, { "\r\r\n", "\n\n" }, { "\r", "\n" }, { "abc", "abc" },
        { "x\r\n", "x\n" }, { "", "" }, { "\n\r\n\r", "\n\n\n" },
    };
    for (size_t i = 0; i < sizeof n / sizeof n[0]; i++) {
        char buf[64];
        size_t len = strlen(n[i].in);
        memcpy(buf, n[i].in, len);
        size_t got = btn_eol_normalize(buf, len);
        CHECK(got == strlen(n[i].out) && memcmp(buf, n[i].out, got) == 0, "normalize case %zu", i);
    }
    size_t ol;
    char *e = btn_eol_encode("a\nb", 3, BTN_EOL_LF, &ol);
    CHECK(e && ol == 3 && memcmp(e, "a\nb", 3) == 0 && e[3] == 0, "encode LF copies");
    free(e);
    e = btn_eol_encode("a\r\nb\rc\n\r", 8, BTN_EOL_LF, &ol);
    CHECK(e && ol == 7 && memcmp(e, "a\nb\nc\n\n", 7) == 0, "encode LF also converts CRLF/CR (explicit choice)");
    free(e);
    e = btn_eol_encode("a\r\r\nb", 6, BTN_EOL_CRLF, &ol);
    CHECK(e && ol == 7 && memcmp(e, "a\r\n\r\nb", 7) == 0, "encode CR + CRLF -> two CRLF");
    free(e);
    e = btn_eol_encode("a\nb\n", 4, BTN_EOL_CRLF, &ol);
    CHECK(e && ol == 6 && memcmp(e, "a\r\nb\r\n", 6) == 0 && e[6] == 0, "encode CRLF");
    free(e);
    e = btn_eol_encode("a\nb\n", 4, BTN_EOL_CR, &ol);
    CHECK(e && ol == 4 && memcmp(e, "a\rb\r", 4) == 0, "encode CR");
    free(e);
    e = btn_eol_encode("", 0, BTN_EOL_CRLF, &ol);
    CHECK(e && ol == 0, "encode empty");
    free(e);
    CHECK(strcmp(btn_eol_name(BTN_EOL_LF), "LF") == 0 && strcmp(btn_eol_name(BTN_EOL_CRLF), "CRLF") == 0 &&
          strcmp(btn_eol_name(BTN_EOL_CR), "CR") == 0, "names");
}

/* Zufaellige Datei mit einheitlichem eol: Laden (erkennen + vereinheitlichen)
 * und Sichern (zurueckwandeln) muss exakt dieselben Bytes ergeben. */
static const char *eol_str(BtnEol e) { return e == BTN_EOL_CRLF ? "\r\n" : e == BTN_EOL_CR ? "\r" : "\n"; }

static size_t random_line(char *out) {
    static const char *pieces[] = { "a", "Z", " ", "\t", "\xC3\xA4", "\xE4", "\xF0\x9F\x98\x80", "{", "\\", "x\xA9" };
    size_t n = 0, k = rnd(8);
    for (size_t i = 0; i < k; i++) {
        const char *p = pieces[rnd(10)];
        memcpy(out + n, p, strlen(p));
        n += strlen(p);
    }
    return n;
}

static void test_roundtrip(void) {
    static char file[8192], work[8192];
    for (int iter = 0; iter < 20000; iter++) {
        BtnEol eol = (BtnEol)rnd(3);
        size_t len = 0, lines = 1 + rnd(12), endings = 0;
        for (size_t l = 0; l < lines; l++) {
            len += random_line(file + len);
            if (l + 1 < lines || rnd(2)) {
                memcpy(file + len, eol_str(eol), strlen(eol_str(eol)));
                len += strlen(eol_str(eol));
                endings++;
            }
        }
        int mixed;
        BtnEol det = btn_eol_detect(file, len, &mixed);
        CHECK(!mixed, "uniform file reported mixed (iter %d)", iter);
        CHECK(det == (endings ? eol : BTN_EOL_LF), "detect %s want %s (iter %d)", btn_eol_name(det), btn_eol_name(eol), iter);
        memcpy(work, file, len);
        size_t nlen = btn_eol_normalize(work, len);
        CHECK(memchr(work, '\r', nlen) == NULL, "no CR after normalize");
        size_t olen;
        char *enc = btn_eol_encode(work, nlen, det, &olen);
        CHECK(olen == len && memcmp(enc, file, len) == 0, "round trip %s not byte-identical (iter %d)", btn_eol_name(eol), iter);
        free(enc);
        /* dasselbe aus zwei Haelften (Gap-Buffer), an jeder Trennstelle */
        size_t split = rnd((unsigned)nlen + 1);
        enc = btn_eol_encode_segments(work, split, work + split, nlen - split, det, &olen);
        CHECK(olen == len && memcmp(enc, file, len) == 0, "segmented round trip (iter %d, split %zu)", iter, split);
        free(enc);
    }
}

/* Zwei Haelften: "\r" am Ende von a und "\n" am Anfang von b sind EIN CRLF. */
static void test_segments(void) {
    size_t ol;
    char *e = btn_eol_encode_segments("x\r", 2, "\ny", 2, BTN_EOL_CRLF, &ol);
    CHECK(e && ol == 4 && memcmp(e, "x\r\ny", 4) == 0, "CRLF split across segments stays one line ending");
    free(e);
    e = btn_eol_encode_segments("x\r", 2, "", 0, BTN_EOL_LF, &ol);
    CHECK(e && ol == 2 && memcmp(e, "x\n", 2) == 0, "CR at end of last segment");
    free(e);
    e = btn_eol_encode_segments("", 0, "", 0, BTN_EOL_CRLF, &ol);
    CHECK(e && ol == 0 && e[0] == 0, "empty segments");
    free(e);
    /* zufaellige Eingaben: jede Trennstelle ergibt dasselbe wie am Stueck */
    static char in[512];
    for (int iter = 0; iter < 20000; iter++) {
        size_t len = rnd(40);
        for (size_t i = 0; i < len; i++) in[i] = "ab\r\n"[rnd(4)];
        BtnEol eol = (BtnEol)rnd(3);
        size_t l1, l2;
        char *whole = btn_eol_encode(in, len, eol, &l1);
        size_t split = rnd((unsigned)len + 1);
        char *parts = btn_eol_encode_segments(in, split, in + split, len - split, eol, &l2);
        CHECK(l1 == l2 && memcmp(whole, parts, l1) == 0, "segments == whole (iter %d)", iter);
        int mixed;
        BtnEol det = btn_eol_detect(whole, l1, &mixed);
        CHECK(!mixed, "encoded output is uniform (iter %d)", iter);
        CHECK(det == eol || (!memchr(in, '\n', len) && !memchr(in, '\r', len)), "encoded output detected as target (iter %d)", iter);
        free(whole);
        free(parts);
    }
}

/* editor_insert_text fuegt genau die uebergebenen Bytes ein - auch "\r\n"
 * (roh geladene gemischte/Binaerdatei). Vorher wurde dort vereinheitlicht:
 * Klammer-Umschliessen setzte den Cursor dann hinter das Pufferende
 * (Heap-Ueberlauf beim naechsten Tastendruck), "Alle ersetzen" verschob
 * alle folgenden Treffer. */
static void test_editor_exact_insert(void) {
    Editor ed;
    editor_init(&ed);
    editor_set_text(&ed, "\0A\r\nB\r\n", 7);
    editor_set_cursor(&ed, 1, 0);
    editor_set_cursor(&ed, 7, 1);
    editor_handle_bracket_key(&ed, '(');
    size_t len;
    char *t = editor_copy_all(&ed, &len);
    CHECK(len == 9 && memcmp(t, "\0(A\r\nB\r\n)", 9) == 0, "wrap keeps CRLF bytes in a raw buffer");
    CHECK(ed.anchor == 2 && ed.cursor == 8 && ed.cursor <= len, "wrap selection stays inside the buffer");
    free(t);
    editor_insert_text(&ed, "z", 1); /* tippt ueber die Selektion - vorher ASan-Fehler */
    t = editor_copy_all(&ed, &len);
    CHECK(len == 4 && memcmp(t, "\0(z)", 4) == 0, "typing over the wrapped selection");
    free(t);
    editor_set_text(&ed, "", 0);
    editor_insert_text(&ed, "a\r\nb\rc", 6);
    CHECK(editor_length(&ed) == 6 && ed.cursor == 6, "insert keeps CR bytes (normalizing is main.c's job)");
    editor_free(&ed);

    Editor field;
    editor_init(&field);
    editor_set_single_line(&field, 1);
    editor_insert_text(&field, "a\r\nb", 4);
    t = editor_copy_all(&field, &len);
    CHECK(len == 4 && memcmp(t, "a  b", 4) == 0, "single-line field still maps CR/LF to spaces");
    free(t);
    editor_free(&field);
}

static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }

static void test_large(void) {
    size_t target = 100u * 1024 * 1024, len = 0;
    char *buf = malloc(target + 128);
    const char *line = "Eine typische Zeile mit etwas Text und Zahlen 12345;\r\n";
    size_t ll = strlen(line);
    while (len + ll <= target) {
        memcpy(buf + len, line, ll);
        len += ll;
    }
    double t0 = now();
    int mixed;
    BtnEol e = btn_eol_detect(buf, len, &mixed);
    double t1 = now();
    size_t nlen = btn_eol_normalize(buf, len);
    double t2 = now();
    size_t olen;
    char *enc = btn_eol_encode(buf, nlen, e, &olen);
    double t3 = now();
    printf("100 MB CRLF: erkennen %.0f ms, vereinheitlichen %.0f ms, zurueckwandeln %.0f ms\n",
           (t1 - t0) * 1000, (t2 - t1) * 1000, (t3 - t2) * 1000);
    CHECK(e == BTN_EOL_CRLF && !mixed && olen == len, "large file round trip");
    CHECK(t3 - t0 < 10.0, "large file under 10 s even with sanitizers");
    free(enc);
    free(buf);
}

int main(void) {
    test_detect();
    test_normalize_encode();
    test_roundtrip();
    test_segments();
    test_editor_exact_insert();
    test_large();
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
