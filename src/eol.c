#include "eol.h"
#include "gapbuffer.h" /* btn_xmalloc */

#include <string.h>

BtnEol btn_eol_detect(const char *s, size_t len, int *out_mixed) {
    size_t lf = 0, crlf = 0, cr = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\n') {
            lf++;
        } else if (s[i] == '\r') {
            if (i + 1 < len && s[i + 1] == '\n') {
                crlf++;
                i++;
            } else {
                cr++;
            }
        }
    }
    if (out_mixed) {
        *out_mixed = ((lf != 0) + (crlf != 0) + (cr != 0)) > 1;
    }
    if (crlf > lf && crlf >= cr) {
        return BTN_EOL_CRLF;
    }
    if (cr > lf && cr > crlf) {
        return BTN_EOL_CR;
    }
    return BTN_EOL_LF;
}

size_t btn_eol_normalize(char *s, size_t len) {
    char *cr = memchr(s, '\r', len);
    if (!cr) {
        return len; /* Normalfall: nichts zu tun, kein Byte-fuer-Byte-Lauf */
    }
    size_t o = (size_t)(cr - s);
    for (size_t i = o; i < len; i++) {
        if (s[i] == '\r') {
            if (i + 1 < len && s[i + 1] == '\n') {
                continue; /* das '\n' folgt im naechsten Durchlauf */
            }
            s[o++] = '\n';
        } else {
            s[o++] = s[i];
        }
    }
    return o;
}

/* Ein Durchlauf ueber a und b hintereinander: out == NULL zaehlt nur die
 * Zielgroesse, sonst wird geschrieben. Ein '\r' bleibt "offen", bis das
 * naechste Byte zeigt, ob es zu einem "\r\n" gehoert - auch ueber die Grenze
 * von a nach b hinweg. */
static size_t encode_pass(const char *a, size_t alen, const char *b, size_t blen, BtnEol eol, char *out) {
    const char *seg[2] = { a, b };
    const size_t seglen[2] = { alen, blen };
    size_t o = 0;
    int pending_cr = 0;
#define EMIT(ch) do { if (out) { out[o] = (ch); } o++; } while (0)
#define EMIT_EOL() do { if (eol != BTN_EOL_LF) { EMIT('\r'); } if (eol != BTN_EOL_CR) { EMIT('\n'); } } while (0)
    for (int s = 0; s < 2; s++) {
        for (size_t i = 0; i < seglen[s]; i++) {
            char c = seg[s][i];
            if (pending_cr) {
                pending_cr = 0;
                EMIT_EOL();
                if (c == '\n') {
                    continue; /* "\r\n" = ein Zeilenende */
                }
            }
            if (c == '\r') {
                pending_cr = 1;
            } else if (c == '\n') {
                EMIT_EOL();
            } else {
                EMIT(c);
            }
        }
    }
    if (pending_cr) {
        EMIT_EOL();
    }
#undef EMIT_EOL
#undef EMIT
    return o;
}

char *btn_eol_encode_segments(const char *a, size_t alen, const char *b, size_t blen, BtnEol eol,
                              size_t *out_len) {
    /* Erst zaehlen, dann schreiben: nur EIN Puffer passender Groesse (ein
     * Dokument kann bis zu 1 GB gross sein). */
    size_t n = encode_pass(a, alen, b, blen, eol, NULL);
    char *out = btn_xmalloc(n + 1);
    encode_pass(a, alen, b, blen, eol, out);
    out[n] = '\0';
    *out_len = n;
    return out;
}

char *btn_eol_encode(const char *s, size_t len, BtnEol eol, size_t *out_len) {
    return btn_eol_encode_segments(s, len, "", 0, eol, out_len);
}

const char *btn_eol_name(BtnEol eol) {
    switch (eol) {
        case BTN_EOL_CRLF:
            return "CRLF";
        case BTN_EOL_CR:
            return "CR";
        default:
            return "LF";
    }
}
