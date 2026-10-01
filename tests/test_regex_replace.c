/* Regex-Suchen/Ersetzen: REG_NEWLINE, Flags beim Sammeln/Expandieren,
 * Rueckreferenzen (Code aus main.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include "editor.h"

/* Globale, die der aus main.c extrahierte Code erwartet. */
static Editor g_search_editor;
static Editor g_replace_editor;
static int g_search_regex = 1;
static int g_search_case_sensitive = 1;
static int g_search_whole_word = 0;

/* Verbatim aus main.c (sed beim Kompilieren): BTN_MAX_REGEX_GROUPS,
 * BTN_MAX_EXPANDED_REPLACEMENT_LEN, regexec_flags_for, regex_escape_literal,
 * compile_search_regex, collect_all_matches_unbounded,
 * replacement_has_backreferences, expand_replacement. */
#include "regex_extracted.h"

static int failures = 0;

static void set_text(Editor *ed, const char *s) {
    editor_free(ed);
    editor_init(ed);
    editor_insert_text(ed, s, strlen(s));
}

static void check(int cond, const char *label) {
    printf("%s [%s]\n", cond ? "ok  " : "FAIL", label);
    if (!cond) failures++;
}

/* Nachbau der perform_replace_all()-Kernlogik ohne Editor: Treffer einmal
 * auf dem Original sammeln, jeden mit denselben Flags expandieren, Ergebnis
 * zusammensetzen. */
static char *replace_all_str(const char *text, const char *pattern, const char *repl) {
    set_text(&g_search_editor, pattern);
    set_text(&g_replace_editor, repl);
    size_t len = strlen(text);
    size_t *starts, *ends;
    size_t n = collect_all_matches_unbounded(text, len, &starts, &ends);
    size_t cap = len * 8 + 64;
    char *out = malloc(cap);
    size_t o = 0, pos = 0;
    for (size_t i = 0; i < n; i++) {
        memcpy(out + o, text + pos, starts[i] - pos);
        o += starts[i] - pos;
        size_t rlen;
        char *r = expand_replacement(text, len, starts[i], ends[i],
                                     regexec_flags_for(text, starts[i]), &rlen);
        memcpy(out + o, r, rlen);
        o += rlen;
        free(r);
        pos = ends[i];
    }
    memcpy(out + o, text + pos, len - pos);
    o += len - pos;
    out[o] = '\0';
    free(starts);
    free(ends);
    return out;
}

static size_t count_matches(const char *text, const char *pattern, size_t *first_start, size_t *first_end) {
    set_text(&g_search_editor, pattern);
    size_t *starts, *ends;
    size_t n = collect_all_matches_unbounded(text, strlen(text), &starts, &ends);
    if (n > 0 && first_start) { *first_start = starts[0]; *first_end = ends[0]; }
    free(starts);
    free(ends);
    return n;
}

int main(void) {
    editor_init(&g_search_editor);
    editor_init(&g_replace_editor);
    char *r;
    size_t s, e;

    /* ---- Fix 3: angrenzende Treffer + Rueckreferenzen ---- */
    r = replace_all_str("a\nb\nc\n", "^([a-z]+\n)", "[$1]");
    check(strcmp(r, "[a\n][b\n][c\n]") == 0, "R1 adjacent ^-matches expand groups (was: literal $1)");
    if (strcmp(r, "[a\n][b\n][c\n]") != 0) printf("     got: \"%s\"\n", r);
    free(r);

    r = replace_all_str("a\na\nb", "^(a\n)|(b)", "[$1$2]");
    check(strcmp(r, "[a\n][a\n][b]") == 0, "R2 adjacent alternation uses own groups (was: other match)");
    if (strcmp(r, "[a\n][a\n][b]") != 0) printf("     got: \"%s\"\n", r);
    free(r);

    r = replace_all_str("a", "(a)|(b)", "[$1$2]");
    check(strcmp(r, "[a]") == 0, "R8 unmatched group -> empty");
    free(r);

    r = replace_all_str("x1 x2", "x([0-9])", "$0=$1");
    check(strcmp(r, "x1=1 x2=2") == 0, "R9 $0 and $1 with gap between matches");
    free(r);

    /* ---- Fix 5: REG_NEWLINE ---- */
    check(count_matches("bar\nfoo\nfoo", "^foo", &s, &e) == 2 && s == 4 && e == 7, "R3 ^foo matches at line starts (was 0)");
    check(count_matches("a\nb", "a$", &s, &e) == 1 && s == 0 && e == 1, "R4 a$ before newline (was 0)");
    check(count_matches("ab\ncd", "b.c", NULL, NULL) == 0, "R5 '.' does not cross newline");
    check(count_matches("abc", "x*", NULL, NULL) == 4, "R6 empty matches terminate (4 positions)");
    check(count_matches("a\na\na", "^a", NULL, NULL) == 3, "R3b ^a on every line");
    check(count_matches("a\nb\n", "^", NULL, NULL) == 3, "R6b bare ^ matches at each line start incl. after trailing \\n");

    /* Literalmodus unveraendert */
    g_search_regex = 0;
    check(count_matches("a.b axb", "a.b", &s, &e) == 1 && s == 0, "R7 literal '.' is escaped");
    g_search_regex = 1;

    /* Case-insensitive + REG_NEWLINE */
    g_search_case_sensitive = 0;
    check(count_matches("Foo\nfoo", "^foo", NULL, NULL) == 2, "R10 icase with ^");
    g_search_case_sensitive = 1;

    editor_free(&g_search_editor);
    editor_free(&g_replace_editor);
    printf(failures ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
