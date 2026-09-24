/* Zeichengrenzen: btn_utf8_seq_start (roh), Regex-Treffer auf Zeichen-
 * grenzen, btn_row_offset_for_column. Alles gegen den echten, verbatim
 * extrahierten Code aus main.c/render.c und die echte editor.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <regex.h>
#include "editor.h"
#include "highlight.h"

typedef uint16_t UniChar;
typedef struct {
    size_t start;
    size_t len;
    size_t logical_line;
    int is_continuation;
} BtnRow;
static long chars_per_row_for(double w) { long n = (long)w; return n > 0 ? n : 1; }
#include "render_pure_extracted.h"

static Editor g_search_editor;
static Editor g_replace_editor;
static int g_search_regex = 1;
static int g_search_case_sensitive = 1;
static int g_search_whole_word = 0;
#include "regex_extracted.h"

#define BTN_MAX_SEARCH_MATCHES_TEST 4096
static long failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { if (failures < 25) { printf("FAIL: " __VA_ARGS__); printf("\n"); } failures++; } } while (0)

static unsigned long rng = 4242;
static unsigned rnd(unsigned n) { rng = rng * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)((rng >> 33) % n); }

static void set_text(Editor *ed, const char *s, size_t n) {
    editor_free(ed);
    editor_init(ed);
    editor_insert_text(ed, s, n);
}

/* Vorwaerts-Zerlegung = Definition der Zeichengrenzen. */
static void boundaries(const unsigned char *s, size_t len, char *is_b) {
    memset(is_b, 0, len + 1);
    size_t i = 0;
    while (i < len) {
        is_b[i] = 1;
        i += btn_utf8_char_len(s + i, len - i);
    }
    is_b[len] = 1;
}

static const char *pieces[] = { "a", "b", "\xC3\xA4", "\xE2\x82\xAC", "\xE4", "\xA4", "\n", " ",
                                "\xF0\x9F\x98\x80", "\xC3", "x", "\xED\xA0\x80", "\xC0\xAF" };
#define NPIECES (sizeof(pieces) / sizeof(pieces[0]))

static size_t random_doc(unsigned char *buf, size_t max_pieces) {
    size_t n = 0, k = rnd((unsigned)max_pieces + 1);
    for (size_t i = 0; i < k; i++) {
        const char *p = pieces[rnd(NPIECES)];
        size_t pl = strlen(p);
        memcpy(buf + n, p, pl);
        n += pl;
    }
    return n;
}

/* ---- A: btn_utf8_seq_start == editor_utf8_seq_start, und korrekt ---- */
static void test_seq_start(void) {
    static const unsigned char edge[] = { 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC2, 0xDF,
                                          0xE0, 0xED, 0xEF, 0xF0, 0xF4, 0xF5, 0xFF, 'A', '\t' };
    unsigned char s[8];
    char is_b[9];
    Editor ed;
    editor_init(&ed);
    for (int iter = 0; iter < 400000; iter++) {
        size_t len = 1 + rnd(7);
        for (size_t i = 0; i < len; i++) s[i] = edge[rnd(sizeof(edge))];
        set_text(&ed, (const char *)s, len);
        boundaries(s, len, is_b);
        for (size_t pos = 0; pos <= len; pos++) {
            size_t raw = btn_utf8_seq_start(s, len, pos);
            CHECK(raw == editor_utf8_seq_start(&ed, pos), "seq_start raw vs editor pos %zu", pos);
            /* Referenz: groesste Grenze <= pos */
            size_t ref = pos;
            while (!is_b[ref]) ref--;
            CHECK(raw == ref, "seq_start ref pos %zu got %zu want %zu", pos, raw, ref);
        }
    }
    editor_free(&ed);
}

/* ---- B: Regex-Treffer liegen immer auf Zeichengrenzen ---- */
static const char *patterns[] = { ".", "..", "[^a]", "a*", "x*", "b.", ".$", "^.", "\xC3\xA4", "[^\n]+",
                                  "(.)", "(.)(.)", "\xE2\x82\xAC|b", "[[:alpha:]]", "a|" };
#define NPATTERNS (sizeof(patterns) / sizeof(patterns[0]))

static void test_regex_boundaries(void) {
    unsigned char doc[256];
    char is_b[257];
    size_t starts[BTN_MAX_SEARCH_MATCHES_TEST], ends[BTN_MAX_SEARCH_MATCHES_TEST];
    for (int iter = 0; iter < 20000; iter++) {
        size_t len = random_doc(doc, 20);
        doc[len] = '\0';
        boundaries(doc, len, is_b);
        const char *pat = patterns[rnd(NPATTERNS)];
        set_text(&g_search_editor, pat, strlen(pat));
        g_search_regex = 1;
        g_search_case_sensitive = (int)rnd(2);

        size_t n = collect_all_matches((const char *)doc, len, starts, ends, BTN_MAX_SEARCH_MATCHES_TEST);
        size_t *us, *ue;
        size_t un = collect_all_matches_unbounded((const char *)doc, len, &us, &ue);
        CHECK(un == n, "unbounded count %zu vs %zu pat '%s'", un, n, pat);
        size_t prev_end = 0;
        for (size_t i = 0; i < n; i++) {
            CHECK(starts[i] <= ends[i] && ends[i] <= len, "range pat '%s'", pat);
            CHECK(is_b[starts[i]] && is_b[ends[i]], "boundary pat '%s' match [%zu,%zu) len %zu", pat, starts[i], ends[i], len);
            CHECK(starts[i] >= prev_end, "overlap pat '%s' at %zu (prev end %zu)", pat, starts[i], prev_end);
            if (i < un) CHECK(us[i] == starts[i] && ue[i] == ends[i], "unbounded differs pat '%s'", pat);
            prev_end = ends[i];
        }
        free(us);
        free(ue);

        for (size_t from = 0; from <= len; from++) {
            if (!is_b[from]) continue;
            size_t fs, fe;
            if (find_match((const char *)doc, len, from, 1, 0, &fs, &fe)) {
                CHECK(fs >= from && is_b[fs] && is_b[fe] && fs <= fe, "find fwd pat '%s' from %zu -> [%zu,%zu)", pat, from, fs, fe);
            }
            if (find_match((const char *)doc, len, from, 0, 1, &fs, &fe)) {
                CHECK(is_b[fs] && is_b[fe] && fs <= fe, "find back pat '%s' from %zu -> [%zu,%zu)", pat, from, fs, fe);
            }
        }
    }
}

/* Ersetzen: "<$0>" bzw. "<$1>" um jeden Treffer - nach Entfernen der
 * Klammern muss das Original (bzw. bei $1 eine Teilmenge ganzer Zeichen)
 * herauskommen, und jeder Klammerinhalt ist eine Folge ganzer Zeichen. */
static char *replace_all_str(const unsigned char *text, size_t len, const char *repl, size_t *out_len) {
    set_text(&g_replace_editor, repl, strlen(repl));
    size_t *starts, *ends;
    size_t n = collect_all_matches_unbounded((const char *)text, len, &starts, &ends);
    char *out = malloc(len * 8 + 64 * (n + 1));
    size_t o = 0, pos = 0;
    for (size_t i = 0; i < n; i++) {
        memcpy(out + o, text + pos, starts[i] - pos);
        o += starts[i] - pos;
        size_t rlen;
        char *r = expand_replacement((const char *)text, len, starts[i], ends[i],
                                     regexec_flags_for((const char *)text, starts[i]), &rlen);
        memcpy(out + o, r, rlen);
        o += rlen;
        free(r);
        pos = ends[i];
    }
    memcpy(out + o, text + pos, len - pos);
    o += len - pos;
    free(starts);
    free(ends);
    *out_len = o;
    return out;
}

static void test_regex_replace(void) {
    unsigned char doc[256];
    for (int iter = 0; iter < 5000; iter++) {
        size_t len = random_doc(doc, 16);
        doc[len] = '\0';
        const char *pat = patterns[rnd(NPATTERNS)];
        set_text(&g_search_editor, pat, strlen(pat));
        g_search_case_sensitive = 1;
        size_t olen;
        char *out = replace_all_str(doc, len, "<$0>", &olen);
        /* Klammern entfernen -> Original */
        char *strip = malloc(olen + 1);
        size_t sl = 0;
        for (size_t i = 0; i < olen; i++) if (out[i] != '<' && out[i] != '>') strip[sl++] = out[i];
        CHECK(sl == len && memcmp(strip, doc, len) == 0, "$0 roundtrip pat '%s'", pat);
        /* Jedes <...> umschliesst ganze Zeichen: Klammerpositionen im
         * Original sind Grenzen. */
        char is_b[257];
        boundaries(doc, len, is_b);
        size_t orig = 0;
        for (size_t i = 0; i < olen; i++) {
            if (out[i] == '<' || out[i] == '>') CHECK(is_b[orig], "bracket mid-char pat '%s' orig %zu", pat, orig);
            else orig++;
        }
        free(strip);
        free(out);
    }
    /* Konkrete Faelle */
    struct { const char *doc, *pat, *repl, *want; } cases[] = {
        { "\xC3\xA4", ".", "<$0>", "<\xC3\xA4>" },
        { "\xC3\xA4", "(.)", "[$1]", "[]" },                 /* erweitert -> $1 leer, nie halbes Zeichen */
        { "ab", "(.)", "[$1]", "[a][b]" },                   /* normaler Fall unveraendert */
        { "x\xE2\x82\xACy", "x.", "<$0>", "<x\xE2\x82\xAC>y" },
        { "\xE4\xC3\xA4", ".", "-", "--" },                 /* Latin-1-Byte + UTF-8-ae: 2 Zeichen */
        { "\xC3\xA4\xC3\xA4", "\xC3\xA4", "o", "oo" },       /* Literal-UTF-8 unveraendert */
    };
    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        set_text(&g_search_editor, cases[k].pat, strlen(cases[k].pat));
        size_t olen;
        char *out = replace_all_str((const unsigned char *)cases[k].doc, strlen(cases[k].doc), cases[k].repl, &olen);
        CHECK(olen == strlen(cases[k].want) && memcmp(out, cases[k].want, olen) == 0,
              "case %zu: got '%.*s' want '%s'", k, (int)olen, out, cases[k].want);
        free(out);
    }
    /* Leertreffer in mehrbytigem Text: "x*" auf "aeae" - Scan springt
     * zeichenweise, jede Position eine Grenze, 3 Leertreffer (0, 2, 4). */
    {
        set_text(&g_search_editor, "x*", 2);
        size_t s2[16], e2[16];
        size_t n = collect_all_matches("\xC3\xA4\xC3\xA4", 4, s2, e2, 16);
        CHECK(n == 3 && s2[0] == 0 && s2[1] == 2 && s2[2] == 4, "empty matches step per char: n=%zu", n);
    }
}

/* ---- C: btn_row_offset_for_column ---- */
static void test_row_offset(void) {
    static const char *words[] = { "a", "bb", "\xC3\xA4\xC3\xA4", "x\xE2\x82\xAC", "\xE4\xE4\xE4", "abcdefghijklmnop",
                                   "\xF0\x9F\x98\x80\xF0\x9F\x98\x80", "\t", " ", "\n", "  " };
    Editor ed;
    editor_init(&ed);
    char doc[512];
    char is_b[513];
    for (int iter = 0; iter < 6000; iter++) {
        size_t len = 0;
        size_t k = rnd(30);
        for (size_t i = 0; i < k; i++) {
            const char *w = words[rnd(sizeof(words) / sizeof(words[0]))];
            size_t wl = strlen(w);
            memcpy(doc + len, w, wl);
            len += wl;
            if (rnd(2)) doc[len++] = ' ';
        }
        set_text(&ed, doc, len);
        boundaries((const unsigned char *)doc, len, is_b);
        long cpr = 3 + (long)rnd(10);
        size_t nrows, nwords;
        BtnRow *rows = layout_build(&ed, cpr, &nrows, &nwords, NULL);
        for (size_t r = 0; r < nrows; r++) {
            size_t row_end = rows[r].start + rows[r].len;
            int wrapped = r + 1 < nrows && rows[r + 1].is_continuation;
            size_t prev = rows[r].start;
            for (size_t col = 0; col <= (size_t)cpr + 6; col++) {
                size_t off = btn_row_offset_for_column(&ed, rows, nrows, r, col);
                CHECK(off >= rows[r].start && off <= row_end, "off in row");
                CHECK(is_b[off], "off boundary");
                CHECK(off >= prev, "monotone");
                /* Kernpunkt: der Offset gehoert zu DIESER Row (wird dort
                 * gezeichnet), nicht zum Anfang der naechsten. */
                CHECK(btn_layout_row_for_offset(rows, nrows, off) == r,
                      "row %zu col %zu -> off %zu lands in row %zu (cpr %ld)", r, col, off,
                      btn_layout_row_for_offset(rows, nrows, off), cpr);
                prev = off;
            }
            size_t end_off = btn_row_offset_for_column(&ed, rows, nrows, r, (size_t)-1);
            if (!wrapped) {
                CHECK(end_off == row_end, "unwrapped End = row end");
            } else if (rows[r].len > 0) {
                CHECK(end_off == editor_utf8_seq_start(&ed, row_end - 1), "wrapped End = before last char");
            }
            /* Klick/Ab mit Spalte innerhalb des Row-Texts: unveraendert wie
             * editor_offset_for_column_in_range. */
            size_t plain = editor_offset_for_column_in_range(&ed, rows[r].start, rows[r].len, 1);
            if (plain < row_end || !wrapped) {
                CHECK(btn_row_offset_for_column(&ed, rows, nrows, r, 1) == plain, "inside-row col unchanged");
            }
        }
        free(rows);
    }
    editor_free(&ed);
}

int main(void) {
    editor_init(&g_search_editor);
    editor_init(&g_replace_editor);
    editor_set_single_line(&g_search_editor, 1);
    editor_set_single_line(&g_replace_editor, 1);
    test_seq_start();
    printf("A seq_start: %ld checks, %ld failures\n", checks, failures);
    test_regex_boundaries();
    printf("B regex boundaries: %ld checks, %ld failures\n", checks, failures);
    test_regex_replace();
    printf("B2 regex replace: %ld checks, %ld failures\n", checks, failures);
    test_row_offset();
    printf("C row offset: %ld checks, %ld failures\n", checks, failures);
    editor_free(&g_search_editor);
    editor_free(&g_replace_editor);
    printf("%s: %ld checks, %ld failures\n", failures ? "FAILED" : "ALL PASSED", checks, failures);
    return failures ? 1 : 0;
}
