#include "ai.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gapbuffer.h" /* btn_xmalloc/btn_xrealloc */

/* ---- wachsender Puffer ---- */

typedef struct {
    char *d;
    size_t len, cap;
} Buf;

static void buf_reserve(Buf *b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + extra + 1) {
            cap = btn_xmul(cap, 2);
        }
        b->d = btn_xrealloc(b->d, cap);
        b->cap = cap;
    }
}

static void buf_add(Buf *b, const char *s, size_t n) {
    buf_reserve(b, n);
    memcpy(b->d + b->len, s, n);
    b->len += n;
    b->d[b->len] = '\0';
}

static void buf_str(Buf *b, const char *s) {
    buf_add(b, s, strlen(s));
}

/* ---- UTF-8 und JSON-Strings ---- */

/* Laenge einer gueltigen UTF-8-Sequenz bei s (1-4), 0 = ungueltig (streng:
 * keine ueberlangen Formen, keine Surrogate, nichts ueber U+10FFFF). */
static size_t utf8_valid_len(const unsigned char *s, size_t avail) {
    unsigned char c = s[0];
    if (c < 0x80) {
        return 1;
    }
    size_t n;
    unsigned char lo = 0x80, hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) {
        n = 2;
    } else if (c >= 0xE0 && c <= 0xEF) {
        n = 3;
        if (c == 0xE0) {
            lo = 0xA0;
        } else if (c == 0xED) {
            hi = 0x9F;
        }
    } else if (c >= 0xF0 && c <= 0xF4) {
        n = 4;
        if (c == 0xF0) {
            lo = 0x90;
        } else if (c == 0xF4) {
            hi = 0x8F;
        }
    } else {
        return 0;
    }
    if (n > avail || s[1] < lo || s[1] > hi) {
        return 0;
    }
    for (size_t i = 2; i < n; i++) {
        if (s[i] < 0x80 || s[i] > 0xBF) {
            return 0;
        }
    }
    return n;
}

static void json_add_string(Buf *b, const char *s, size_t len) {
    buf_add(b, "\"", 1);
    for (size_t i = 0; i < len;) {
        unsigned char c = (unsigned char)s[i];
        char esc[8];
        switch (c) {
            case '"':
                buf_add(b, "\\\"", 2);
                i++;
                continue;
            case '\\':
                buf_add(b, "\\\\", 2);
                i++;
                continue;
            case '\n':
                buf_add(b, "\\n", 2);
                i++;
                continue;
            case '\r':
                buf_add(b, "\\r", 2);
                i++;
                continue;
            case '\t':
                buf_add(b, "\\t", 2);
                i++;
                continue;
            default:
                break;
        }
        if (c < 0x20) {
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            buf_add(b, esc, 6);
            i++;
            continue;
        }
        size_t n = utf8_valid_len((const unsigned char *)s + i, len - i);
        if (n == 0) {
            buf_add(b, "\\ufffd", 6); /* JSON muss gueltiges UTF-8 sein */
            i++;
        } else {
            buf_add(b, s + i, n);
            i += n;
        }
    }
    buf_add(b, "\"", 1);
}

static void add_utf8(Buf *b, unsigned long cp) {
    char u[4];
    size_t n;
    if (cp < 0x80) {
        u[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        u[0] = (char)(0xC0 | (cp >> 6));
        u[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        u[0] = (char)(0xE0 | (cp >> 12));
        u[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        u[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        u[0] = (char)(0xF0 | (cp >> 18));
        u[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        u[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        u[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    buf_add(b, u, n);
}

static int hex4(const char *s, size_t avail, unsigned long *out) {
    if (avail < 4) {
        return 0;
    }
    unsigned long v = 0;
    for (int i = 0; i < 4; i++) {
        int c = (unsigned char)s[i], d;
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            d = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            d = c - 'A' + 10;
        } else {
            return 0;
        }
        v = v * 16 + (unsigned long)d;
    }
    *out = v;
    return 1;
}

/* JSON-String ab j[*i] (auf dem oeffnenden '"'); dekodiert nach out (darf
 * NULL sein = nur ueberspringen). 1 = gueltig, *i hinter dem '"'. */
static int json_read_string(const char *j, size_t len, size_t *i, Buf *out) {
    size_t p = *i + 1;
    while (p < len) {
        char c = j[p];
        if (c == '"') {
            *i = p + 1;
            return 1;
        }
        if ((unsigned char)c < 0x20) {
            return 0;
        }
        if (c != '\\') {
            if (out) {
                buf_add(out, &c, 1);
            }
            p++;
            continue;
        }
        if (p + 1 >= len) {
            return 0;
        }
        char e = j[p + 1];
        const char *simple = NULL;
        switch (e) {
            case '"': simple = "\""; break;
            case '\\': simple = "\\"; break;
            case '/': simple = "/"; break;
            case 'b': simple = "\b"; break;
            case 'f': simple = "\f"; break;
            case 'n': simple = "\n"; break;
            case 'r': simple = "\r"; break;
            case 't': simple = "\t"; break;
            default: break;
        }
        if (simple) {
            if (out) {
                buf_add(out, simple, 1);
            }
            p += 2;
            continue;
        }
        if (e != 'u') {
            return 0;
        }
        unsigned long cp;
        if (!hex4(j + p + 2, len - (p + 2), &cp)) {
            return 0;
        }
        p += 6;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            unsigned long lo;
            if (p + 1 < len && j[p] == '\\' && j[p + 1] == 'u' && hex4(j + p + 2, len - (p + 2), &lo) &&
                lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                p += 6;
            } else {
                cp = 0xFFFD; /* einzelnes Surrogat */
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        if (out) {
            add_utf8(out, cp);
        }
    }
    return 0;
}

static size_t skip_ws(const char *j, size_t len, size_t i) {
    while (i < len && (j[i] == ' ' || j[i] == '\t' || j[i] == '\n' || j[i] == '\r')) {
        i++;
    }
    return i;
}

/* Ueberspringt einen beliebigen JSON-Wert. 1 = gueltig. */
static int json_skip_value(const char *j, size_t len, size_t *i) {
    size_t p = skip_ws(j, len, *i);
    if (p >= len) {
        return 0;
    }
    if (j[p] == '"') {
        *i = p;
        return json_read_string(j, len, i, NULL);
    }
    if (j[p] == '{' || j[p] == '[') {
        int depth = 0;
        while (p < len) {
            char c = j[p];
            if (c == '"') {
                size_t q = p;
                if (!json_read_string(j, len, &q, NULL)) {
                    return 0;
                }
                p = q;
                continue;
            }
            if (c == '{' || c == '[') {
                depth++;
            } else if (c == '}' || c == ']') {
                if (--depth == 0) {
                    *i = p + 1;
                    return 1;
                }
            }
            p++;
        }
        return 0;
    }
    /* Zahl, true, false, null */
    size_t start = p;
    while (p < len && j[p] != ',' && j[p] != '}' && j[p] != ']' && j[p] != ' ' && j[p] != '\n' && j[p] != '\r' &&
           j[p] != '\t') {
        p++;
    }
    *i = p;
    return p > start;
}

/* Position des Werts von key auf oberster Ebene eines JSON-Objekts. */
static int json_find_key(const char *json, size_t len, const char *key, size_t *value_pos) {
    size_t i = skip_ws(json, len, 0);
    if (i >= len || json[i] != '{') {
        return 0;
    }
    i++;
    size_t key_len = strlen(key);
    for (;;) {
        i = skip_ws(json, len, i);
        if (i >= len || json[i] != '"') {
            return 0;
        }
        Buf k = { 0 };
        if (!json_read_string(json, len, &i, &k)) {
            free(k.d);
            return 0;
        }
        int match = k.len == key_len && (key_len == 0 || memcmp(k.d, key, key_len) == 0);
        free(k.d);
        i = skip_ws(json, len, i);
        if (i >= len || json[i] != ':') {
            return 0;
        }
        i = skip_ws(json, len, i + 1);
        if (match) {
            *value_pos = i;
            return i < len;
        }
        if (!json_skip_value(json, len, &i)) {
            return 0;
        }
        i = skip_ws(json, len, i);
        if (i < len && json[i] == ',') {
            i++;
            continue;
        }
        return 0;
    }
}

int btn_ai_json_get_string(const char *json, size_t len, const char *key, char **out, size_t *out_len) {
    size_t i;
    if (!json_find_key(json, len, key, &i) || json[i] != '"') {
        return 0;
    }
    Buf v = { 0 };
    if (!json_read_string(json, len, &i, &v)) {
        free(v.d);
        return 0;
    }
    buf_reserve(&v, 0); /* auch ein leerer String kommt als "" */
    v.d[v.len] = '\0';
    *out = v.d;
    *out_len = v.len;
    return 1;
}

/* ---- Konfiguration ---- */

void btn_ai_config_defaults(BtnAiConfig *c) {
    memset(c, 0, sizeof(*c));
    c->enabled = 0;
    c->api = BTN_AI_API_OLLAMA;
    /* IP statt "localhost": App Transport Security laesst unverschluesseltes
     * HTTP zu IP-Adressen zu. */
    snprintf(c->url, sizeof(c->url), "%s", "http://127.0.0.1:11434");
    snprintf(c->model, sizeof(c->model), "%s", "qwen2.5-coder:1.5b");
    c->delay_ms = 300;
    c->max_tokens = 48;
}

static int clamp_int(long v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : (int)v;
}

static void trim(const char **s, size_t *n) {
    while (*n && isspace((unsigned char)**s)) {
        (*s)++;
        (*n)--;
    }
    while (*n && isspace((unsigned char)(*s)[*n - 1])) {
        (*n)--;
    }
}

void btn_ai_config_parse(BtnAiConfig *c, const char *text, size_t len) {
    size_t i = 0;
    while (i < len) {
        size_t e = i;
        while (e < len && text[e] != '\n') {
            e++;
        }
        const char *line = text + i;
        size_t n = e - i;
        i = e + 1;
        trim(&line, &n);
        if (n == 0 || line[0] == '#') {
            continue;
        }
        const char *eq = memchr(line, '=', n);
        if (!eq) {
            continue;
        }
        const char *key = line, *val = eq + 1;
        size_t kn = (size_t)(eq - line), vn = n - kn - 1;
        /* " # Kommentar" hinter dem Wert */
        for (size_t k = 1; k < vn; k++) {
            if (val[k] == '#' && isspace((unsigned char)val[k - 1])) {
                vn = k;
                break;
            }
        }
        trim(&key, &kn);
        trim(&val, &vn);
        char v[256];
        size_t vl = vn < sizeof(v) - 1 ? vn : sizeof(v) - 1;
        memcpy(v, val, vl);
        v[vl] = '\0';
#define KEY_IS(s) (kn == strlen(s) && memcmp(key, s, kn) == 0)
        if (KEY_IS("enabled")) {
            c->enabled = strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "yes") == 0 || strcmp(v, "on") == 0;
        } else if (KEY_IS("api")) {
            if (strcmp(v, "ollama") == 0) {
                c->api = BTN_AI_API_OLLAMA;
            } else if (strcmp(v, "llama") == 0 || strcmp(v, "llama-server") == 0 || strcmp(v, "llama.cpp") == 0) {
                c->api = BTN_AI_API_LLAMA;
            }
        } else if (KEY_IS("url") && vl > 0 && vl < sizeof(c->url)) {
            while (vl > 0 && v[vl - 1] == '/') {
                v[--vl] = '\0';
            }
            memcpy(c->url, v, vl + 1);
        } else if (KEY_IS("model") && vl > 0 && vl < sizeof(c->model)) {
            memcpy(c->model, v, vl + 1);
        } else if (KEY_IS("delay_ms")) {
            c->delay_ms = clamp_int(strtol(v, NULL, 10), 50, 5000);
        } else if (KEY_IS("max_tokens")) {
            c->max_tokens = clamp_int(strtol(v, NULL, 10), 1, 512);
        }
#undef KEY_IS
    }
}

char *btn_ai_config_format(const BtnAiConfig *c) {
    Buf b = { 0 };
    char num[80];
    buf_str(&b, "# BTNEdit: KI-Vervollstaendigung ueber einen lokalen Server\n"
                "# api: ollama oder llama (llama.cpp llama-server); der Text um den\n"
                "# Cursor geht an url - nur einen Server eintragen, dem du vertraust.\n");
    snprintf(num, sizeof(num), "enabled=%d\napi=%s\nurl=", c->enabled, c->api == BTN_AI_API_LLAMA ? "llama" : "ollama");
    buf_str(&b, num);
    buf_str(&b, c->url);
    buf_str(&b, "\nmodel=");
    buf_str(&b, c->model);
    snprintf(num, sizeof(num), "\ndelay_ms=%d\nmax_tokens=%d\n", c->delay_ms, c->max_tokens);
    buf_str(&b, num);
    return b.d;
}

char *btn_ai_endpoint(const BtnAiConfig *c) {
    Buf b = { 0 };
    buf_str(&b, c->url);
    buf_str(&b, c->api == BTN_AI_API_LLAMA ? "/infill" : "/api/generate");
    return b.d;
}

/* ---- Anfrage und Antwort ---- */

char *btn_ai_request_body(const BtnAiConfig *c, const char *prefix, size_t prefix_len, const char *suffix,
                          size_t suffix_len, size_t *out_len) {
    Buf b = { 0 };
    char num[64];
    if (c->api == BTN_AI_API_LLAMA) {
        buf_str(&b, "{\"input_prefix\":");
        json_add_string(&b, prefix, prefix_len);
        buf_str(&b, ",\"input_suffix\":");
        json_add_string(&b, suffix, suffix_len);
        snprintf(num, sizeof(num), ",\"n_predict\":%d", c->max_tokens);
        buf_str(&b, num);
        buf_str(&b, ",\"temperature\":0.2,\"stop\":[\"\\n\"],\"cache_prompt\":true,\"stream\":false}");
    } else {
        buf_str(&b, "{\"model\":");
        json_add_string(&b, c->model, strlen(c->model));
        buf_str(&b, ",\"prompt\":");
        json_add_string(&b, prefix, prefix_len);
        /* Ollama nimmt die Fill-in-the-Middle-Vorlage nur mit nicht leerem
         * suffix - leer faellt es auf die Chat-Vorlage zurueck, und ein
         * Instruct-Modell antwortet mit Prosa statt mit Code (z.B. am
         * Dateiende). Ein Zeilenende steht dort ohnehin. */
        if (suffix_len == 0) {
            suffix = "\n";
            suffix_len = 1;
        }
        buf_str(&b, ",\"suffix\":");
        json_add_string(&b, suffix, suffix_len);
        /* keep_alive: das Modell bleibt 30 min geladen (Standard 5 min) -
         * sonst dauert die erste Anfrage nach einer Pause sekundenlang. */
        snprintf(num, sizeof(num), ",\"stream\":false,\"keep_alive\":\"30m\",\"options\":{\"num_predict\":%d",
                 c->max_tokens);
        buf_str(&b, num);
        buf_str(&b, ",\"temperature\":0.2,\"stop\":[\"\\n\"]}}");
    }
    *out_len = b.len;
    return b.d;
}

int btn_ai_parse_response(int api, const char *body, size_t len, char **out, size_t *out_len) {
    return btn_ai_json_get_string(body, len, api == BTN_AI_API_LLAMA ? "content" : "response", out, out_len);
}

/* Codepunkt der gueltigen Sequenz s[0..n). */
static unsigned long utf8_decode(const unsigned char *s, size_t n) {
    if (n == 1) {
        return s[0];
    }
    unsigned long cp = s[0] & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
    for (size_t i = 1; i < n; i++) {
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    return cp;
}

/* Nichts, was im Quelltext unsichtbar wirkt oder ihn verfaelscht: Steuer-
 * zeichen (ausser Tab), DEL/C1, Richtungs-Steuerzeichen (Trojan Source),
 * Zeichen ohne Breite, BOM. */
static int suggestion_char_ok(unsigned long cp) {
    if (cp == '\t') {
        return 1;
    }
    if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) {
        return 0;
    }
    if (cp == 0x061C || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
        (cp >= 0x2060 && cp <= 0x2069) || cp == 0xFEFF) {
        return 0;
    }
    return 1;
}

size_t btn_ai_clean_suggestion(char *s, size_t len, const char *rest, size_t rest_len) {
    /* Bis zum ersten Zeilenende, Steuer-/unsichtbaren Zeichen oder
     * ungueltigen UTF-8 - der Rest davor bleibt. */
    for (size_t i = 0; i < len;) {
        size_t n = utf8_valid_len((const unsigned char *)s + i, len - i);
        if (n == 0 || !suggestion_char_ok(utf8_decode((const unsigned char *)s + i, n))) {
            len = i;
            break;
        }
        i += n;
    }
    size_t r = 0;
    while (r < rest_len && rest[r] != '\n' && rest[r] != '\r') {
        r++;
    }
    /* Laengste Ueberschneidung: Ende des Vorschlags = Anfang des Rests */
    size_t max = len < r ? len : r;
    for (size_t k = max; k > 0; k--) {
        if (memcmp(s + len - k, rest, k) == 0) {
            len -= k;
            break;
        }
    }
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        len--;
    }
    size_t lead = 0;
    while (lead < len && (s[lead] == ' ' || s[lead] == '\t')) {
        lead++;
    }
    if (lead == len) {
        len = 0;
    }
    /* Nicht mitten in einem UTF-8-Zeichen enden (das Abschneiden der
     * Ueberschneidung arbeitet byteweise) */
    if (len > 0 && (unsigned char)s[len - 1] >= 0x80) {
        size_t start = len - 1;
        while (start > 0 && len - start < 4 && ((unsigned char)s[start] & 0xC0) == 0x80) {
            start--;
        }
        if (utf8_valid_len((const unsigned char *)s + start, len - start) != len - start) {
            len = start;
        }
    }
    s[len] = '\0';
    return len;
}

char *btn_ai_config_set_value(const char *text, size_t len, const char *key, const char *value) {
    Buf b = { 0 };
    size_t kl = strlen(key);
    int done = 0;
    size_t i = 0;
    while (i < len) {
        size_t e = i;
        while (e < len && text[e] != '\n') {
            e++;
        }
        const char *line = text + i;
        size_t n = e - i, lead = 0;
        while (lead < n && isspace((unsigned char)line[lead])) {
            lead++;
        }
        if (!done && n - lead >= kl && memcmp(line + lead, key, kl) == 0) {
            size_t k = lead + kl;
            while (k < n && (line[k] == ' ' || line[k] == '\t')) {
                k++;
            }
            if (k < n && line[k] == '=') {
                buf_str(&b, key);
                buf_add(&b, "=", 1);
                buf_str(&b, value);
                done = 1;
                if (e < len) {
                    buf_add(&b, "\n", 1);
                }
                i = e + 1;
                continue;
            }
        }
        buf_add(&b, line, e < len ? n + 1 : n);
        i = e + 1;
    }
    if (!done) {
        if (b.len > 0 && b.d[b.len - 1] != '\n') {
            buf_add(&b, "\n", 1);
        }
        buf_str(&b, key);
        buf_add(&b, "=", 1);
        buf_str(&b, value);
        buf_add(&b, "\n", 1);
    }
    buf_reserve(&b, 0);
    b.d[b.len] = '\0';
    return b.d;
}

char *btn_ai_config_set_enabled(const char *text, size_t len, int enabled) {
    return btn_ai_config_set_value(text, len, "enabled", enabled ? "1" : "0");
}

/* ---- Modellliste (Ollama /api/tags) ---- */

void btn_ai_models_free(BtnAiModel *models, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(models[i].name);
    }
    free(models);
}

size_t btn_ai_parse_models(const char *json, size_t len, BtnAiModel **out) {
    *out = NULL;
    size_t i;
    if (!json_find_key(json, len, "models", &i) || i >= len || json[i] != '[') {
        return 0;
    }
    i++;
    size_t count = 0, cap = 0;
    BtnAiModel *list = NULL;
    for (;;) {
        i = skip_ws(json, len, i);
        if (i >= len || json[i] == ']') {
            break;
        }
        size_t start = i;
        if (!json_skip_value(json, len, &i)) {
            break;
        }
        char *name = NULL;
        size_t nl;
        if (json[start] == '{' && btn_ai_json_get_string(json + start, i - start, "name", &name, &nl) && nl > 0 &&
            !memchr(name, '\0', nl)) {
            size_t vp;
            long long size = 0;
            if (json_find_key(json + start, i - start, "size", &vp)) {
                size = strtoll(json + start + vp, NULL, 10);
            }
            if (count == cap) {
                cap = cap ? cap * 2 : 8;
                list = btn_xrealloc(list, btn_xmul(cap, sizeof(BtnAiModel)));
            }
            list[count].name = name;
            list[count].size = size;
            count++;
        } else {
            free(name);
        }
        i = skip_ws(json, len, i);
        if (i < len && json[i] == ',') {
            i++;
        }
    }
    *out = list;
    return count;
}

/* Modelle, die Code mit Fill-in-the-Middle koennen - am Namen erkannt. */
int btn_ai_model_is_coder(const char *name) {
    static const char *const marks[] = { "coder", "codellama", "codegemma", "codestral", "starcoder" };
    for (size_t i = 0; i < sizeof(marks) / sizeof(marks[0]); i++) {
        if (strstr(name, marks[i])) {
            return 1;
        }
    }
    return 0;
}

int btn_ai_pick_model(const BtnAiModel *models, size_t count) {
    int best = -1;
    for (size_t i = 0; i < count; i++) {
        if (btn_ai_model_is_coder(models[i].name) && (best < 0 || models[i].size < models[best].size)) {
            best = (int)i;
        }
    }
    return best;
}

int btn_ai_find_model(const BtnAiModel *models, size_t count, const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(models[i].name, name) == 0) {
            return (int)i;
        }
    }
    /* "qwen2.5-coder" meint bei Ollama "qwen2.5-coder:latest" */
    if (!strchr(name, ':')) {
        for (size_t i = 0; i < count; i++) {
            size_t n = strlen(name);
            if (strncmp(models[i].name, name, n) == 0 && strcmp(models[i].name + n, ":latest") == 0) {
                return (int)i;
            }
        }
    }
    return -1;
}

int btn_ai_rest_allows_request(const char *rest, size_t rest_len) {
    for (size_t i = 0; i < rest_len && rest[i] != '\n'; i++) {
        if (rest[i] == '\0' || !strchr(" \t\r)]}\"'`;,", rest[i])) {
            return 0;
        }
    }
    return 1;
}
