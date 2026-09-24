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

char *btn_eol_encode(const char *s, size_t len, BtnEol eol, size_t *out_len) {
    if (eol == BTN_EOL_LF) {
        *out_len = len;
        return NULL;
    }
    size_t newlines = 0;
    for (const char *p = s; (p = memchr(p, '\n', len - (size_t)(p - s))) != NULL; p++) {
        newlines++;
    }
    size_t n = len + (eol == BTN_EOL_CRLF ? newlines : 0);
    char *out = btn_xmalloc(n + 1);
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\n') {
            out[o++] = '\r';
            if (eol == BTN_EOL_CRLF) {
                out[o++] = '\n';
            }
        } else {
            out[o++] = s[i];
        }
    }
    out[o] = '\0';
    *out_len = o;
    return out;
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
