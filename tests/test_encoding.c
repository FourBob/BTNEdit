/* Zeichenkodierungen (encoding.c): Erkennung, Lesen nach UTF-8, Schreiben
 * zurueck - Rundlauf aller 256 Bytes fuer Latin-1/Windows-1252, UTF-16 mit
 * Surrogatpaaren und kaputten Einheiten, nicht darstellbare Zeichen, BOM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "encoding.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 30) { printf("FAIL: " __VA_ARGS__); printf("\n"); } fails++; } } while (0)

static int dec_is(const char *in, size_t n, BtnEncoding enc, const char *want, size_t wn, int want_lossy) {
    size_t ol;
    int lossy;
    char *o = btn_enc_decode(in, n, enc, &ol, &lossy);
    int ok = o && ol == wn && memcmp(o, want, wn) == 0 && o[ol] == 0 && lossy == want_lossy;
    free(o);
    return ok;
}

static int enc_is(const char *in, size_t n, BtnEncoding enc, const char *want, size_t wn) {
    size_t ol, bad;
    char *o = btn_enc_encode(in, n, enc, 1, &ol, &bad);
    int ok = o && ol == wn && memcmp(o, want, wn) == 0 && bad == (size_t)-1;
    free(o);
    return ok;
}

static size_t enc_bad(const char *in, size_t n, BtnEncoding enc) {
    size_t ol, bad;
    char *o = btn_enc_encode(in, n, enc, 1, &ol, &bad);
    free(o);
    return o ? (size_t)-2 : bad;
}

static void test_detect(void) {
    CHECK(btn_enc_detect("plain ascii", 11) == BTN_ENC_UTF8, "ASCII: UTF-8");
    CHECK(btn_enc_detect("gr\xC3\xBC\xC3\x9F", 6) == BTN_ENC_UTF8, "valid UTF-8");
    CHECK(btn_enc_detect("gr\xFC\xDF und \x80", 10) == BTN_ENC_WIN1252, "Latin-1 bytes, no UTF-8: Windows-1252");
    CHECK(btn_enc_detect("\xC3\xA4 log \xFF", 8) == BTN_ENC_UTF8, "mixed: UTF-8, bytes kept raw");
    CHECK(btn_enc_detect("\xEF\xBB\xBFhi", 5) == BTN_ENC_UTF8_BOM, "UTF-8 BOM");
    CHECK(btn_enc_detect("\xFF\xFEh\0", 4) == BTN_ENC_UTF16LE, "UTF-16 LE BOM");
    CHECK(btn_enc_detect("\xFE\xFF\0h", 4) == BTN_ENC_UTF16BE, "UTF-16 BE BOM");
    CHECK(btn_enc_detect("", 0) == BTN_ENC_UTF8, "empty: UTF-8");
    CHECK(btn_enc_detect("\xFF\xFEtre", 5) == BTN_ENC_WIN1252 && btn_enc_detect("\xFE\xFFtre", 5) == BTN_ENC_WIN1252,
          "\"ÿþ\" at the start of a Latin-1 file without NUL: not UTF-16");
    CHECK(btn_enc_detect("\xFF\xFE", 2) == BTN_ENC_UTF16LE && btn_enc_detect("\xFE\xFF", 2) == BTN_ENC_UTF16BE,
          "just the BOM: empty UTF-16 file");
    CHECK(btn_enc_detect("\xEF\xBB", 2) == BTN_ENC_WIN1252, "truncated BOM is no BOM");
    CHECK(btn_enc_detect("\xC0\xAF", 2) == BTN_ENC_WIN1252 && btn_enc_detect("\xED\xA0\x80", 3) == BTN_ENC_WIN1252,
          "overlong / surrogate is not UTF-8");
    CHECK(btn_enc_bom_len("\xEF\xBB\xBFx", 4, BTN_ENC_UTF8_BOM) == 3 && btn_enc_bom_len("\xEF\xBB\xBFx", 4, BTN_ENC_UTF8) == 0 &&
              btn_enc_bom_len("\xFF\xFE", 2, BTN_ENC_UTF16BE) == 0,
          "BOM length only for the matching encoding");
}

static void test_single_byte(void) {
    /* alle 256 Bytes: lesen und unveraendert zurueckschreiben */
    char all[256];
    for (int i = 0; i < 256; i++) {
        all[i] = (char)i;
    }
    for (int e = BTN_ENC_LATIN1; e <= BTN_ENC_WIN1252; e++) {
        size_t ul, bl, bad;
        int lossy;
        char *u = btn_enc_decode(all, 256, (BtnEncoding)e, &ul, &lossy);
        char *b = btn_enc_encode(u, ul, (BtnEncoding)e, 1, &bl, &bad);
        CHECK(u && b && !lossy && bl == 256 && memcmp(b, all, 256) == 0, "%s: all 256 bytes round-trip", btn_enc_name((BtnEncoding)e));
        free(u);
        free(b);
    }
    CHECK(dec_is("\xE4\x80", 2, BTN_ENC_LATIN1, "\xC3\xA4\xC2\x80", 4, 0), "Latin-1: 0xE4 = ä, 0x80 = U+0080");
    CHECK(dec_is("\xE4\x80\x93\x81", 4, BTN_ENC_WIN1252, "\xC3\xA4\xE2\x82\xAC\xE2\x80\x9C\xC2\x81", 10, 0),
          "Windows-1252: € and “, unassigned 0x81 = U+0081");
    CHECK(enc_is("\xE2\x82\xAC \xC3\xA4", 6, BTN_ENC_WIN1252, "\x80 \xE4", 3), "€ -> 0x80 in Windows-1252");
    CHECK(enc_bad("ok \xE2\x82\xAC", 6, BTN_ENC_LATIN1) == 3, "€ not in Latin-1: offset of the character");
    CHECK(enc_bad("ab\xF0\x9F\x98\x80", 6, BTN_ENC_WIN1252) == 2, "emoji not in Windows-1252");
    CHECK(enc_bad("\xC2\x80", 2, BTN_ENC_WIN1252) == 0, "U+0080 in Windows-1252 would read back as €");
    CHECK(enc_is("\xC2\x81", 2, BTN_ENC_WIN1252, "\x81", 1), "unassigned U+0081 stays 0x81");
    CHECK(enc_is("a\xFF" "b", 3, BTN_ENC_LATIN1, "a\xFF" "b", 3), "raw invalid byte: kept as that Latin-1 byte");
}

static void test_utf16(void) {
    /* "Aä€😀" */
    static const char utf8[] = "A\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80";
    static const char le[] = "\xFF\xFE" "A\0" "\xE4\0" "\xAC\x20" "\x3D\xD8\x00\xDE";
    static const char be[] = "\xFE\xFF" "\0A" "\0\xE4" "\x20\xAC" "\xD8\x3D\xDE\x00";
    CHECK(dec_is(le, 12, BTN_ENC_UTF16LE, utf8, 10, 0), "UTF-16 LE with surrogate pair");
    CHECK(dec_is(be, 12, BTN_ENC_UTF16BE, utf8, 10, 0), "UTF-16 BE");
    CHECK(enc_is(utf8, 10, BTN_ENC_UTF16LE, le, 12) && enc_is(utf8, 10, BTN_ENC_UTF16BE, be, 12), "and back, with BOM");
    CHECK(dec_is("A\0", 2, BTN_ENC_UTF16LE, "A", 1, 0), "no BOM: decoded as chosen");
    CHECK(dec_is("\x00\xD8" "A\0", 4, BTN_ENC_UTF16LE, "\xEF\xBF\xBD" "A", 4, 1), "lone high surrogate: U+FFFD, lossy");
    CHECK(dec_is("\x00\xDC", 2, BTN_ENC_UTF16LE, "\xEF\xBF\xBD", 3, 1), "lone low surrogate: lossy");
    CHECK(dec_is("\x00\xD8", 2, BTN_ENC_UTF16LE, "\xEF\xBF\xBD", 3, 1), "high surrogate at the end: lossy");
    CHECK(dec_is("A\0B", 3, BTN_ENC_UTF16LE, "A\xEF\xBF\xBD", 4, 1), "odd length: lossy");
    CHECK(enc_is("\xFF", 1, BTN_ENC_UTF16BE, "\xFE\xFF\0\xFF", 4), "raw byte -> its Latin-1 character");
    CHECK(enc_is("", 0, BTN_ENC_UTF16LE, "\xFF\xFE", 2), "empty text: just the BOM");
    size_t nl, nb;
    char *nob = btn_enc_encode("A", 1, BTN_ENC_UTF16BE, 0, &nl, &nb);
    CHECK(nob && nl == 2 && memcmp(nob, "\0A", 2) == 0, "UTF-16 without BOM when the file had none");
    free(nob);
    nob = btn_enc_encode("A", 1, BTN_ENC_UTF8_BOM, 0, &nl, &nb);
    CHECK(nob && nl == 4 && memcmp(nob, "\xEF\xBB\xBF" "A", 4) == 0, "UTF-8 (BOM) always writes its BOM");
    free(nob);
}

static void test_utf8(void) {
    CHECK(dec_is("\xEF\xBB\xBFhi", 5, BTN_ENC_UTF8_BOM, "hi", 2, 0), "BOM stripped");
    CHECK(dec_is("\xEF\xBB\xBFhi", 5, BTN_ENC_UTF8, "\xEF\xBB\xBFhi", 5, 0), "UTF-8 without BOM keeps the bytes");
    CHECK(dec_is("a\xFF", 2, BTN_ENC_UTF8, "a\xFF", 2, 0), "invalid bytes pass through");
    CHECK(enc_is("hi", 2, BTN_ENC_UTF8_BOM, "\xEF\xBB\xBFhi", 5) && enc_is("a\xFF", 2, BTN_ENC_UTF8, "a\xFF", 2),
          "UTF-8 out: BOM added / bytes unchanged");
    CHECK(strcmp(btn_enc_name(BTN_ENC_WIN1252), "Windows-1252") == 0 && strcmp(btn_enc_name(BTN_ENC_COUNT), "?") == 0, "names");
}

/* Zufaellige Texte: UTF-8 -> jede Kodierung -> zurueck ergibt dasselbe
 * (soweit darstellbar). */
static void test_fuzz(void) {
    unsigned long rng = 7;
    static const char *const pieces[] = { "a", "\n", "\xC3\xA4", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xC2\x81", " ", "\xE2\x80\x9C" };
    for (int round = 0; round < 2000; round++) {
        char text[400];
        size_t n = 0;
        int count = (int)((rng = rng * 6364136223846793005UL + 1) >> 60);
        for (int k = 0; k < count * 3; k++) {
            const char *p = pieces[(rng = rng * 6364136223846793005UL + 1) >> 61];
            memcpy(text + n, p, strlen(p));
            n += strlen(p);
        }
        for (int e = 0; e < BTN_ENC_COUNT; e++) {
            size_t bl, bad, ul;
            int lossy;
            char *b = btn_enc_encode(text, n, (BtnEncoding)e, 1, &bl, &bad);
            if (!b) {
                CHECK(bad < n, "unrepresentable: offset inside the text");
                continue;
            }
            char *u = btn_enc_decode(b, bl, (BtnEncoding)e, &ul, &lossy);
            CHECK(u && !lossy && ul == n && memcmp(u, text, n) == 0, "round trip %s (round %d)", btn_enc_name((BtnEncoding)e), round);
            free(u);
            free(b);
        }
    }
}

int main(void) {
    test_detect();
    test_single_byte();
    test_utf16();
    test_utf8();
    test_fuzz();
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
