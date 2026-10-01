/* Tabs in Such-/Ersetzen-Feldern und \t im Regex-Modus (Muster und
 * Ersetzungstext). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include "editor.h"
static Editor g_search_editor, g_replace_editor;
static int g_search_regex = 1, g_search_case_sensitive = 1, g_search_whole_word = 0;
#include "regex_extracted.h"
static int fails = 0;
#define CHECK(c, m) do { printf("%s %s\n", (c) ? "ok  " : "FAIL", m); if (!(c)) fails++; } while (0)
static void set(Editor *ed, const char *s) { editor_set_text(ed, s, strlen(s)); }
static int find_all(const char *doc, const char *pat, size_t *s0, size_t *e0) {
    set(&g_search_editor, pat);
    size_t s[16], e[16];
    size_t n = collect_all_matches(doc, strlen(doc), s, e, 16);
    if (n && s0) { *s0 = s[0]; *e0 = e[0]; }
    return (int)n;
}
static char *replace_all(const char *doc, const char *pat, const char *repl) {
    set(&g_search_editor, pat); set(&g_replace_editor, repl);
    size_t len = strlen(doc), *st, *en;
    size_t n = collect_all_matches_unbounded(doc, len, &st, &en);
    char *out = malloc(len * 4 + 256); size_t o = 0, pos = 0;
    for (size_t i = 0; i < n; i++) {
        memcpy(out + o, doc + pos, st[i] - pos); o += st[i] - pos;
        size_t rl; char *r = expand_replacement(doc, len, st[i], en[i], regexec_flags_for(doc, st[i]), &rl);
        memcpy(out + o, r, rl); o += rl; free(r); pos = en[i];
    }
    memcpy(out + o, doc + pos, len - pos); o += len - pos; out[o] = 0;
    free(st); free(en);
    return out;
}
int main(void) {
    editor_init(&g_search_editor); editor_init(&g_replace_editor);
    editor_set_single_line(&g_search_editor, 1); editor_set_single_line(&g_replace_editor, 1);
    size_t s, e; char *r;

    /* Einzeiliges Feld behaelt Tabs, ersetzt Zeilenumbrueche */
    set(&g_search_editor, "a\tb\nc\r");
    { size_t l; char *t = editor_copy_all(&g_search_editor, &l); CHECK(l == 6 && memcmp(t, "a\tb c ", 6) == 0, "single-line keeps tab, maps \\n/\\r to space"); free(t); }
    editor_set_cursor(&g_search_editor, 0, 0);
    editor_insert_text(&g_search_editor, "\t", 1);
    { size_t l; char *t = editor_copy_all(&g_search_editor, &l); CHECK(l == 7 && t[0] == '\t', "pasted tab kept"); free(t); }

    g_search_regex = 1;
    CHECK(find_all("x a\tb y atb", "a\\tb", &s, &e) == 1 && s == 2 && e == 5, "regex a\\tb finds the tab, not 'atb'");
    CHECK(find_all("a\\tb", "a\\\\tb", &s, &e) == 1 && s == 0 && e == 4, "regex a\\\\tb finds backslash-t");
    CHECK(find_all("a\tb", "a[\\t]b", NULL, NULL) == 1, "\\t inside brackets = tab");
    CHECK(find_all("x.y xzy", "x\\.y", &s, &e) == 1 && s == 0, "other escapes untouched (\\.)");
    CHECK(find_all("a\t\tb", "\\t+", &s, &e) == 1 && e - s == 2, "\\t+ quantifier");
    g_search_regex = 0;
    CHECK(find_all("a\tb", "a\tb", &s, &e) == 1, "literal mode: real tab (prefill) matches");
    CHECK(find_all("a\\tb a\tb", "a\\tb", &s, &e) == 1 && s == 0 && e == 4, "literal mode: \\t stays literal");

    g_search_regex = 1;
    r = replace_all("a,b", ",", "\\t"); CHECK(strcmp(r, "a\tb") == 0, "replacement \\t inserts tab"); free(r);
    r = replace_all("a,b", ",", "\\\\t"); CHECK(strcmp(r, "a\\tb") == 0, "replacement \\\\t gives backslash-t"); free(r);
    r = replace_all("k=v", "(.)=(.)", "$2\\t$1"); CHECK(strcmp(r, "v\tk") == 0, "tab mixed with backrefs"); free(r);
    r = replace_all("k=v", "=", "\\n"); CHECK(strcmp(r, "k\\nv") == 0, "unknown escape \\n unchanged"); free(r);
    r = replace_all("a,b", ",", "x\\"); CHECK(strcmp(r, "ax\\b") == 0, "trailing backslash kept"); free(r);
    g_search_regex = 0;
    r = replace_all("a,b", ",", "\\t"); CHECK(strcmp(r, "a\\tb") == 0, "literal mode replacement unchanged"); free(r);
    /* Regressions: bisherige Rueckreferenzen */
    g_search_regex = 1;
    r = replace_all("ab ab", "(a)(b)", "$2$1"); CHECK(strcmp(r, "ba ba") == 0, "backrefs unchanged"); free(r);
    r = replace_all("ab", "(a)", "\\1\\1"); CHECK(strcmp(r, "aab") == 0, "\\1 backrefs unchanged"); free(r);
    printf("%s\n", fails ? "FAILED" : "ALL TESTS PASSED");
    return fails != 0;
}
