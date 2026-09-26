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
    char *fmt = btn_ai_config_format(&c);
    BtnAiConfig d;
    btn_ai_config_defaults(&d);
    btn_ai_config_parse(&d, fmt, strlen(fmt));
    CHECK(memcmp(&c, &d, sizeof c) == 0, "format -> parse round trip");
    free(fmt);
    char *ep = btn_ai_endpoint(&c);
    CHECK(strcmp(ep, "http://box:8080/infill") == 0, "llama-server endpoint (%s)", ep);
    free(ep);
    btn_ai_config_defaults(&c);
    ep = btn_ai_endpoint(&c);
    CHECK(strcmp(ep, "http://127.0.0.1:11434/api/generate") == 0, "Ollama endpoint");
    free(ep);
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

int main(void) {
    test_config();
    test_request();
    test_response();
    test_clean();
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
