/* KI-Vervollstaendigung, reiner Teil (ai.c): Konfiguration, JSON der
 * Anfrage (Rundlauf ueber den eigenen Parser, auch mit kaputtem UTF-8),
 * Auswerten der Antwort und Aufraeumen des Vorschlags. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 40) { printf("FAIL: " __VA_ARGS__); printf("\n"); } fails++; } } while (0)

static int get(const char *json, const char *key, const char *want, size_t want_len) {
    char *v;
    size_t n;
    if (!btn_ai_json_get_string(json, strlen(json), key, &v, &n)) {
        return 0;
    }
    int ok = n == want_len && memcmp(v, want, n) == 0 && v[n] == 0;
    free(v);
    return ok;
}

static void test_config(void) {
    BtnAiConfig c;
    btn_ai_config_defaults(&c);
    CHECK(!c.enabled && c.api == BTN_AI_API_OLLAMA && strcmp(c.url, "http://127.0.0.1:11434") == 0 &&
              strcmp(c.model, "qwen2.5-coder:1.5b") == 0 && c.delay_ms == 300 && c.max_tokens == 48,
          "defaults: off, Ollama on 127.0.0.1");
    const char *text = "# comment\n enabled = yes \napi=llama\nurl=http://box:8080//\nmodel = my model  # note\n"
                       "delay_ms=10\nmax_tokens=99999\nunknown=1\nnoequals\n";
    btn_ai_config_parse(&c, text, strlen(text));
    CHECK(c.enabled && c.api == BTN_AI_API_LLAMA && strcmp(c.url, "http://box:8080") == 0 &&
              strcmp(c.model, "my model") == 0 && c.delay_ms == 50 && c.max_tokens == 512,
          "parse: trimming, comments, trailing slash, clamping (%s|%s|%d|%d)", c.url, c.model, c.delay_ms, c.max_tokens);
    const char *more = "api=bogus\nenabled=0\ndelay_ms=abc\nmax_tokens=0\nurl=\n";
    btn_ai_config_parse(&c, more, strlen(more));
    CHECK(!c.enabled && c.api == BTN_AI_API_LLAMA && c.delay_ms == 50 && c.max_tokens == 1 && strcmp(c.url, "http://box:8080") == 0,
          "unknown api and empty url keep the old value, numbers clamp");
    const char *tm = "text_model = qwen3.6:35b-a3b # prose\n";
    btn_ai_config_parse(&c, tm, strlen(tm));
    CHECK(strcmp(c.text_model, "qwen3.6:35b-a3b") == 0 && strcmp(btn_ai_text_model(&c), "qwen3.6:35b-a3b") == 0,
          "text_model parsed (%s)", c.text_model);
    char *fmt = btn_ai_config_format(&c);
    BtnAiConfig d;
    btn_ai_config_defaults(&d);
    btn_ai_config_parse(&d, fmt, strlen(fmt));
    CHECK(memcmp(&c, &d, sizeof c) == 0, "format -> parse round trip");
    free(fmt);
    char *ep = btn_ai_endpoint(&c, 0);
    CHECK(strcmp(ep, "http://box:8080/infill") == 0, "llama-server endpoint (%s)", ep);
    free(ep);
    ep = btn_ai_endpoint(&c, 1);
    CHECK(strcmp(ep, "http://box:8080/completion") == 0, "llama-server prose endpoint (%s)", ep);
    free(ep);
    btn_ai_config_parse(&c, "text_model=\n", 12);
    CHECK(c.text_model[0] == 0 && strcmp(btn_ai_text_model(&c), "my model") == 0, "empty text_model: same as model");
    btn_ai_config_defaults(&c);
    CHECK(c.text_model[0] == 0, "default: no text model");
    ep = btn_ai_endpoint(&c, 0);
    CHECK(strcmp(ep, "http://127.0.0.1:11434/api/generate") == 0, "Ollama endpoint");
    free(ep);
    ep = btn_ai_endpoint(&c, 1);
    CHECK(strcmp(ep, "http://127.0.0.1:11434/api/generate") == 0, "Ollama prose endpoint is the same");
    free(ep);
}

static void test_text(void) {
    CHECK(btn_ai_path_is_text(NULL) && btn_ai_path_is_text("/a/b.txt") && btn_ai_path_is_text("README.MD") &&
              btn_ai_path_is_text("x.markdown") && btn_ai_path_is_text("n.rst") && btn_ai_path_is_text("a.text"),
          "prose: untitled, .txt, .md (any case), .markdown, .rst, .text");
    CHECK(!btn_ai_path_is_text("/d.txt/main.c") && !btn_ai_path_is_text("Makefile") && !btn_ai_path_is_text("/x/.md") &&
              !btn_ai_path_is_text("a.txt.c") && !btn_ai_path_is_text("a.mdx") && !btn_ai_path_is_text("a.tx"),
          "code: other extensions, no extension, dot files, extension of the directory");

    BtnAiConfig c;
    btn_ai_config_defaults(&c);
    size_t n;
    char *body = btn_ai_request_body_text(&c, "qwen3.6:35b-a3b", "Es war \"einmal\"\n", 16, &n);
    CHECK(n == strlen(body) && get(body, "model", "qwen3.6:35b-a3b", 15) && get(body, "prompt", "Es war \"einmal\"\n", 16) &&
              strstr(body, "\"raw\":true") && !strstr(body, "suffix") && strstr(body, "\"num_predict\":48") &&
              strstr(body, "\"stop\":[\"\\n\"]"),
          "Ollama prose body: raw, no suffix (%s)", body);
    free(body);
    c.api = BTN_AI_API_LLAMA;
    body = btn_ai_request_body_text(&c, "ignored", "abc", 3, &n);
    CHECK(get(body, "prompt", "abc", 3) && !strstr(body, "input_") && !strstr(body, "ignored") &&
              strstr(body, "\"n_predict\":48"),
          "llama-server prose body: prompt only (%s)", body);
    free(body);

    static const char e1[] = "{\"error\":\"registry.ollama.ai/library/qwen3:8b does not support insert\"}";
    CHECK(btn_ai_is_no_insert_error(e1, sizeof e1 - 1), "no-insert error recognised");
    CHECK(!btn_ai_is_no_insert_error(e1, sizeof e1 - 4) && !btn_ai_is_no_insert_error(e1, 40) && !btn_ai_is_no_insert_error("{\"error\":\"not found\"}", 21) &&
              !btn_ai_is_no_insert_error(NULL, 0),
          "other errors / cut body / NULL are not");

    static const char *const cases[][2] = {
        { "öner Tag. Morgen", "öner Tag." }, { "Ja! Nein", "Ja!" },       { "Wie? So", "Wie?" },
        { "3.5 Meter weit", "3.5 Meter weit" }, { "Ende.", "Ende." },    { "kein Satzende", "kein Satzende" },
        { "你好。再见", "你好。" },                 { "对！好", "对！" },       { "吗？是", "吗？" },
        { "Nun\xE2\x80\xA6 gut", "Nun\xE2\x80\xA6" }, { "Nun\xE2\x80\xA6", "Nun\xE2\x80\xA6" },
        { "a.b. c", "a.b." },                     { "a\xE2\x80\xA6" "b", "a\xE2\x80\xA6" "b" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char buf[64];
        snprintf(buf, sizeof buf, "%s", cases[i][0]);
        size_t k = btn_ai_cut_sentence(buf, strlen(buf));
        CHECK(k == strlen(cases[i][1]) && strcmp(buf, cases[i][1]) == 0, "cut_sentence(%s) = %s", cases[i][0], buf);
    }
}

static void test_request(void) {
    BtnAiConfig c;
    btn_ai_config_defaults(&c);
    size_t n;
    char *body = btn_ai_request_body(&c, "int x = \"a\\b\";\n\tfoo(", 20, ");\n}", 4, &n);
    CHECK(n == strlen(body), "length matches");
    CHECK(get(body, "model", "qwen2.5-coder:1.5b", 18), "Ollama: model");
    CHECK(get(body, "prompt", "int x = \"a\\b\";\n\tfoo(", 20), "Ollama: prompt round trip with quotes, backslash, newline, tab");
    CHECK(get(body, "suffix", ");\n}", 4), "Ollama: suffix");
    CHECK(strstr(body, "\"stream\":false") && strstr(body, "\"num_predict\":48") && strstr(body, "\"stop\":[\"\\n\"]") &&
              strstr(body, "\"keep_alive\":\"30m\""),
          "Ollama: no streaming, token limit, one line, model stays loaded");
    free(body);
    body = btn_ai_request_body(&c, "x", 1, "", 0, &n);
    CHECK(get(body, "suffix", "\n", 1), "Ollama: empty suffix sent as newline (else Ollama uses the chat template)");
    free(body);
    c.api = BTN_AI_API_LLAMA;
    body = btn_ai_request_body(&c, "a", 1, "", 0, &n);
    CHECK(get(body, "input_prefix", "a", 1) && get(body, "input_suffix", "", 0) && strstr(body, "\"n_predict\":48"),
          "llama-server: input_prefix/input_suffix/n_predict");
    free(body);
    c.api = BTN_AI_API_OLLAMA;

    /* Kaputtes UTF-8 und Steuerzeichen */
    const char bad[] = "\xFF|\xC3|\xED\xA0\x80|\x01|\xE2\x82\xAC|\xF0\x9F\x98\x80|\x7F|\xC3";
    body = btn_ai_request_body(&c, bad, sizeof bad - 1, "", 0, &n);
    const char want[] = "\xEF\xBF\xBD|\xEF\xBF\xBD|\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD|\x01|\xE2\x82\xAC|\xF0\x9F\x98\x80|\x7F|\xEF\xBF\xBD";
    CHECK(get(body, "prompt", want, sizeof want - 1), "invalid bytes -> U+FFFD, valid multibyte kept, control chars escaped");
    CHECK(strstr(body, "\\u0001") != NULL, "control char as \\u0001");
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)body[i] < 0x20) {
            CHECK(0, "raw control character in JSON at %zu", i);
            break;
        }
    }
    free(body);

    /* Fuzz: gueltiges UTF-8 kommt unveraendert zurueck */
    srand(5);
    const char *pieces[] = { "a", "\"", "\\", "\n", "\r", "\t", "\x02", "\xC3\xA4", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "/", "}" };
    long bad_rt = 0;
    for (int it = 0; it < 3000; it++) {
        char p[200], s[200];
        size_t pl = 0, sl = 0;
        for (int k = rand() % 20; k > 0; k--) {
            const char *x = pieces[rand() % 12];
            memcpy(p + pl, x, strlen(x));
            pl += strlen(x);
        }
        for (int k = rand() % 10; k > 0; k--) {
            const char *x = pieces[rand() % 12];
            memcpy(s + sl, x, strlen(x));
            sl += strlen(x);
        }
        c.api = rand() % 2;
        body = btn_ai_request_body(&c, p, pl, s, sl, &n);
        int nl = !c.api && sl == 0; /* Ollama: leerer suffix wird "\n" */
        if (!get(body, c.api ? "input_prefix" : "prompt", p, pl) ||
            !get(body, c.api ? "input_suffix" : "suffix", nl ? "\n" : s, nl ? 1 : sl)) {
            bad_rt++;
        }
        free(body);
    }
    CHECK(bad_rt == 0, "fuzz: prefix/suffix round trip (%ld bad)", bad_rt);
}

static void test_response(void) {
    char *s;
    size_t n;
    const char *r1 = "{\"model\":\"m\",\"created_at\":\"t\",\"response\":\"a\\n\\\"b\\u00e4\\ud83d\\ude00\\/\",\"done\":true}";
    CHECK(btn_ai_parse_response(BTN_AI_API_OLLAMA, r1, strlen(r1), &s, &n) && n == 11 &&
              memcmp(s, "a\n\"b\xC3\xA4\xF0\x9F\x98\x80/", 11) == 0,
          "Ollama response with escapes and a surrogate pair");
    free(s);
    const char *r2 = " { \"context\" : [1, 2, {\"x\": \"}\"}], \"n\": -1.5e3, \"ok\": true, \"z\": null, \"response\" : \"ok\" } ";
    CHECK(btn_ai_parse_response(BTN_AI_API_OLLAMA, r2, strlen(r2), &s, &n) && n == 2 && memcmp(s, "ok", 2) == 0,
          "skips arrays, nested objects, numbers, literals");
    free(s);
    const char *r3 = "{\"content\":\"x\\ud800y\"}";
    CHECK(btn_ai_parse_response(BTN_AI_API_LLAMA, r3, strlen(r3), &s, &n) && n == 5 && memcmp(s, "x\xEF\xBF\xBDy", 5) == 0,
          "llama-server content, lone surrogate -> U+FFFD");
    free(s);
    const char *bad[] = { "", "[]", "{\"response\":1}", "{\"response\":\"abc", "{\"other\":\"x\"}", "{\"response\":\"a\\q\"}",
                          "{\"response\" \"x\"}", "{\"a\":[1,2,\"response\":\"x\"}", "{\"response\":\"\\u12\"}", "not json" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int ok = btn_ai_parse_response(BTN_AI_API_OLLAMA, bad[i], strlen(bad[i]), &s, &n);
        CHECK(!ok, "bad response %zu rejected", i);
        if (ok) {
            free(s);
        }
    }
    CHECK(!btn_ai_parse_response(BTN_AI_API_LLAMA, r1, strlen(r1), &s, &n), "api decides the key");
}

static size_t clean(const char *in, const char *rest, char *out) {
    strcpy(out, in);
    return btn_ai_clean_suggestion(out, strlen(out), rest, strlen(rest));
}

static void test_clean(void) {
    char b[128];
    CHECK(clean("return 0;\n}", "", b) == 9 && strcmp(b, "return 0;") == 0, "only the first line");
    CHECK(clean("x);", ");", b) == 1 && strcmp(b, "x") == 0, "overlap with the rest of the line removed");
    CHECK(clean("a, b)", ")\nnext", b) == 4 && strcmp(b, "a, b") == 0, "rest only up to its line end");
    CHECK(clean("   \t", "", b) == 0, "whitespace only: nothing");
    CHECK(clean("\nfoo", "", b) == 0, "starts with a newline: nothing");
    CHECK(clean("foo  ", "", b) == 3, "trailing blanks trimmed");
    CHECK(clean("  foo", "", b) == 5, "leading blanks kept (indentation)");
    CHECK(clean("\xC3\xA4\xC3", "", b) == 2, "no cut UTF-8 character at the end");
    CHECK(clean("\xE2\x82\xAC", "\xAC", b) == 0 || strcmp(b, "\xE2\x82\xAC") == 0 || b[0] == 0,
          "byte overlap never leaves half a character");
    CHECK(clean("ab\rcd", "", b) == 2, "CR ends the line too");
    CHECK(clean("a\x1b[31mb", "", b) == 1, "escape sequence cut");
    CHECK(clean("ab\x7f", "", b) == 2 && clean("x\xC2\x85y", "", b) == 1, "DEL and C1 cut");
    CHECK(clean("if (a\xE2\x80\xAE) b", "", b) == 5, "bidi override cut (Trojan Source)");
    CHECK(clean("x\xE2\x80\x8By", "", b) == 1 && clean("\xEF\xBB\xBFx", "", b) == 0, "zero width space, BOM cut");
    CHECK(clean("x\x80\x80\x80", "", b) == 1 && clean("\xFF" "abc", "", b) == 0, "invalid UTF-8 cut");
    CHECK(clean("a\tb\xC3\xA4\xE2\x82\xAC", "", b) == 8, "tab and normal non-ASCII kept");
    char z[8] = { 'a', 0, 'b' };
    CHECK(btn_ai_clean_suggestion(z, 3, "", 0) == 1, "NUL cut");

    /* enabled umschalten, Rest der Datei bleibt */
    const char *cfg = "# x\n  enabled =  1  \nmodel=m\nenabledx=5";
    char *o = btn_ai_config_set_enabled(cfg, strlen(cfg), 0);
    CHECK(strcmp(o, "# x\nenabled=0\nmodel=m\nenabledx=5") == 0, "set_enabled: only that line (%s)", o);
    free(o);
    o = btn_ai_config_set_enabled("model=m", 7, 1);
    CHECK(strcmp(o, "model=m\nenabled=1\n") == 0, "set_enabled: appended when missing");
    free(o);
    o = btn_ai_config_set_enabled("", 0, 0);
    CHECK(strcmp(o, "enabled=0\n") == 0, "set_enabled: empty file");
    free(o);

    CHECK(btn_ai_rest_allows_request("", 0), "end of line");
    CHECK(btn_ai_rest_allows_request("  );\n more", 10), "closing characters, then the next line");
    CHECK(btn_ai_rest_allows_request("\"]}`',\t\r\n", 10), "quotes, brackets, CR");
    CHECK(!btn_ai_rest_allows_request("bar)", 4), "text after the cursor: no request");
    CHECK(!btn_ai_rest_allows_request(")\0", 2), "NUL is not closing");
}

static void test_models(void) {
    const char *tags = "{\"models\":[{\"name\":\"qwen2.5-coder:7b\",\"size\":4683087332,\"details\":{\"families\":[\"a\",\"b\"]}},"
                       "{\"name\":\"qwen3.6-coder:latest\",\"size\":23000000000},"
                       "{\"name\":\"qwen3.6:35b-a3b\",\"size\":23000000000},"
                       "{\"model\":\"no-name\"},{\"name\":\"\"},42,{\"name\":\"codellama:13b\",\"size\":7000000000}]}";
    BtnAiModel *m;
    size_t n = btn_ai_parse_models(tags, strlen(tags), &m);
    CHECK(n == 4 && strcmp(m[0].name, "qwen2.5-coder:7b") == 0 && m[0].size == 4683087332LL &&
              strcmp(m[3].name, "codellama:13b") == 0,
          "models parsed, entries without a name skipped (%zu)", n);
    CHECK(btn_ai_pick_model(m, n) == 0, "smallest code model picked");
    CHECK(btn_ai_find_model(m, n, "qwen3.6-coder") == 1 && btn_ai_find_model(m, n, "qwen2.5-coder") == -1 &&
              btn_ai_find_model(m, n, "qwen3.6:35b-a3b") == 2,
          "find: exact, and name -> name:latest only");
    CHECK(btn_ai_model_is_coder("codegemma:2b") && btn_ai_model_is_coder("starcoder2:3b") && !btn_ai_model_is_coder("llama3:8b"),
          "code models recognised by name");
    btn_ai_models_free(m, n);
    const char *chat = "{\"models\":[{\"name\":\"llama3:8b\",\"size\":1}]}";
    n = btn_ai_parse_models(chat, strlen(chat), &m);
    CHECK(n == 1 && btn_ai_pick_model(m, n) == -1, "no code model: nothing picked");
    btn_ai_models_free(m, n);
    CHECK(btn_ai_parse_models("{}", 2, &m) == 0 && m == NULL, "no list");
    CHECK(btn_ai_parse_models("{\"models\":[{\"name\":\"x\"", 23, &m) == 0, "truncated list");
    btn_ai_models_free(m, 0);

    char *o = btn_ai_config_set_value("a=1\nmodel = old  # c\nmodelx=2", 30, "model", "new:7b");
    CHECK(strcmp(o, "a=1\nmodel=new:7b  # c\nmodelx=2") == 0, "set_value replaces only that key, keeps the comment (%s)", o);
    free(o);
    /* doppelter Schluessel: die letzte Zeile gilt beim Lesen - die wird ersetzt */
    static const char dup[] = "model=a\r\nurl=x\r\nmodel=b # zweite\r\n";
    o = btn_ai_config_set_value(dup, sizeof dup - 1, "model", "d");
    CHECK(strcmp(o, "model=a\r\nurl=x\r\nmodel=d # zweite\n") == 0, "duplicate key: last line replaced (%s)", o);
    BtnAiConfig c;
    btn_ai_config_defaults(&c);
    btn_ai_config_parse(&c, o, strlen(o));
    CHECK(strcmp(c.model, "d") == 0, "and read back as the new value (%s)", c.model);
    free(o);
    CHECK(btn_ai_config_set_value("a=1\n", 4, "model", "x\nurl=http://evil") == NULL, "value with a line end refused");

    /* Namen, die nicht sauber in die Datei passen, fehlen; Cloud-Modelle */
    static const char odd[] = "{\"models\":[{\"name\":\"x\\nurl=http://evil:1\"},{\"name\":\"a #b\"},"
                              "{\"name\":\" lead\"},{\"name\":\"qwen3-coder:480b-cloud\",\"size\":384},"
                              "{\"name\":\"deepseek-coder:6.7b\",\"size\":3000,\"remote_host\":\"https://ollama.com:443\"},"
                              "{\"name\":\"hf.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF:Q8_0\",\"size\":1600},"
                              "{\"name\":\"Cloud:latest\",\"size\":1}]}";
    size_t k = btn_ai_parse_models(odd, sizeof odd - 1, &m);
    CHECK(k == 4 && strcmp(m[0].name, "qwen3-coder:480b-cloud") == 0, "control chars, '#', edge spaces dropped (%zu)", k);
    if (k == 4) {
        CHECK(m[0].remote && m[1].remote && !m[2].remote && !m[3].remote, "cloud: tag or remote_host");
        CHECK(btn_ai_model_is_coder(m[2].name), "Coder in capitals is a code model");
        CHECK(btn_ai_pick_model(m, k) == 2, "never auto-picks a cloud model");
        CHECK(btn_ai_find_model(m, k, "HF.CO/qwen/qwen2.5-coder-1.5b-instruct-gguf:q8_0") == 2, "find ignores case");
        CHECK(btn_ai_find_model(m, k, "cloud") == 3, "case-insensitive with :latest");
        CHECK(btn_ai_find_model(m, k, "clou") == -1 && btn_ai_find_model(m, k, "Cloud:lates") == -1, "no prefix matches");
    }
    btn_ai_models_free(m, k);
    char longname[200];
    int ln = snprintf(longname, sizeof longname, "{\"models\":[{\"name\":\"%0130d\"}]}", 0);
    CHECK(btn_ai_parse_models(longname, (size_t)ln, &m) == 0, "name too long for model= dropped");
    btn_ai_models_free(m, 0);
}

int main(void) {
    test_config();
    test_models();
    test_request();
    test_text();
    test_response();
    test_clean();
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
