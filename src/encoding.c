#include "encoding.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Windows-1252 0x80-0x9F; 0 = unbelegt (dann U+0080+x wie Latin-1). */
static const uint16_t WIN1252_HIGH[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
};

/* Laenge (2-4) und Codepunkt der gueltigen UTF-8-Folge am Anfang von s,
 * 0 = keine (dieselben Regeln wie btn_utf8_char_len() in editor.c: keine
 * Ueberlangen, keine Surrogate, nichts ueber U+10FFFF). ASCII: 1. */
static size_t utf8_seq(const unsigned char *s, size_t avail, uint32_t *cp) {
    unsigned char b0 = s[0];
    if (b0 < 0x80) {
        *cp = b0;
        return 1;
    }
    size_t n;
    unsigned char lo = 0x80, hi = 0xBF;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        n = 2;
    } else if (b0 == 0xE0) {
        n = 3;
        lo = 0xA0;
    } else if ((b0 >= 0xE1 && b0 <= 0xEC) || b0 == 0xEE || b0 == 0xEF) {
        n = 3;
    } else if (b0 == 0xED) {
        n = 3;
        hi = 0x9F;
    } else if (b0 == 0xF0) {
        n = 4;
        lo = 0x90;
    } else if (b0 >= 0xF1 && b0 <= 0xF3) {
        n = 4;
    } else if (b0 == 0xF4) {
        n = 4;
        hi = 0x8F;
    } else {
        return 0;
    }
    if (avail < n || s[1] < lo || s[1] > hi) {
        return 0;
    }
    for (size_t i = 2; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }
    }
    uint32_t c = b0 & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
    for (size_t i = 1; i < n; i++) {
        c = (c << 6) | (s[i] & 0x3F);
    }
    *cp = c;
    return n;
}

static size_t put_utf8(char *out, uint32_t cp) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t btn_enc_bom_len(const char *s, size_t len, BtnEncoding enc) {
    const unsigned char *u = (const unsigned char *)s;
    switch (enc) {
        case BTN_ENC_UTF8_BOM:
            return len >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF ? 3 : 0;
        case BTN_ENC_UTF16LE:
            return len >= 2 && u[0] == 0xFF && u[1] == 0xFE ? 2 : 0;
        case BTN_ENC_UTF16BE:
            return len >= 2 && u[0] == 0xFE && u[1] == 0xFF ? 2 : 0;
        default:
            return 0;
    }
}

BtnEncoding btn_enc_detect(const char *s, size_t len) {
    static const BtnEncoding with_bom[] = { BTN_ENC_UTF8_BOM, BTN_ENC_UTF16LE, BTN_ENC_UTF16BE };
    for (size_t i = 0; i < sizeof(with_bom) / sizeof(with_bom[0]); i++) {
        if (btn_enc_bom_len(s, len, with_bom[i])) {
            return with_bom[i];
        }
    }
    const unsigned char *u = (const unsigned char *)s;
    size_t valid_multi = 0, invalid = 0;
    for (size_t i = 0; i < len;) {
        uint32_t cp;
        size_t n = utf8_seq(u + i, len - i, &cp);
        if (n == 0) {
            invalid++;
            i++;
        } else {
            valid_multi += n > 1;
            i += n;
        }
    }
    return invalid > 0 && valid_multi == 0 ? BTN_ENC_WIN1252 : BTN_ENC_UTF8;
}

static uint32_t single_byte_to_cp(unsigned char b, BtnEncoding enc) {
    if (enc == BTN_ENC_WIN1252 && b >= 0x80 && b <= 0x9F && WIN1252_HIGH[b - 0x80]) {
        return WIN1252_HIGH[b - 0x80];
    }
    return b;
}

char *btn_enc_decode(const char *s, size_t len, BtnEncoding enc, size_t *out_len, int *out_lossy) {
    *out_lossy = 0;
    size_t bom = btn_enc_bom_len(s, len, enc);
    const unsigned char *u = (const unsigned char *)s + bom;
    len -= bom;
    char *out;
    size_t o = 0;
    if (enc == BTN_ENC_UTF8 || enc == BTN_ENC_UTF8_BOM) {
        out = malloc(len + 1);
        if (!out) {
            return NULL;
        }
        memcpy(out, u, len);
        o = len;
    } else if (enc == BTN_ENC_LATIN1 || enc == BTN_ENC_WIN1252) {
        if (len > (SIZE_MAX - 1) / 3) {
            return NULL;
        }
        out = malloc(len * 3 + 1); /* hoechstens 3 UTF-8-Bytes je Byte */
        if (!out) {
            return NULL;
        }
        for (size_t i = 0; i < len; i++) {
            o += put_utf8(out + o, single_byte_to_cp(u[i], enc));
        }
    } else {
        /* UTF-16: je 2 Bytes hoechstens 3 UTF-8-Bytes, ein Paar (4) -> 4 */
        if (len / 2 > (SIZE_MAX - 4) / 3) {
            return NULL;
        }
        out = malloc((len / 2) * 3 + 4);
        if (!out) {
            return NULL;
        }
        int le = enc == BTN_ENC_UTF16LE;
        size_t i = 0;
        while (i + 1 < len) {
            uint32_t w = le ? (uint32_t)(u[i] | (u[i + 1] << 8)) : (uint32_t)((u[i] << 8) | u[i + 1]);
            i += 2;
            uint32_t cp = w;
            if (w >= 0xD800 && w <= 0xDBFF && i + 1 < len) {
                uint32_t w2 = le ? (uint32_t)(u[i] | (u[i + 1] << 8)) : (uint32_t)((u[i] << 8) | u[i + 1]);
                if (w2 >= 0xDC00 && w2 <= 0xDFFF) {
                    cp = 0x10000 + ((w - 0xD800) << 10) + (w2 - 0xDC00);
                    i += 2;
                } else {
                    cp = 0xFFFD;
                    *out_lossy = 1;
                }
            } else if (w >= 0xD800 && w <= 0xDFFF) {
                cp = 0xFFFD;
                *out_lossy = 1;
            }
            o += put_utf8(out + o, cp);
        }
        if (i < len) { /* ungerade Laenge */
            o += put_utf8(out + o, 0xFFFD);
            *out_lossy = 1;
        }
    }
    out[o] = '\0';
    *out_len = o;
    return out;
}

char *btn_enc_encode(const char *u8, size_t len, BtnEncoding enc, size_t *out_len, size_t *out_bad) {
    *out_bad = (size_t)-1;
    const unsigned char *u = (const unsigned char *)u8;
    size_t cap;
    if (enc == BTN_ENC_UTF16LE || enc == BTN_ENC_UTF16BE) {
        if (len > (SIZE_MAX - 2) / 2) {
            return NULL;
        }
        cap = len * 2 + 2; /* je Byte hoechstens 2 (ein 1-Byte-Zeichen) */
    } else {
        cap = len + 3;
    }
    char *out = malloc(cap + 1);
    if (!out) {
        return NULL;
    }
    size_t o = 0;
    if (enc == BTN_ENC_UTF8 || enc == BTN_ENC_UTF8_BOM) {
        if (enc == BTN_ENC_UTF8_BOM) {
            memcpy(out, "\xEF\xBB\xBF", 3);
            o = 3;
        }
        memcpy(out + o, u, len);
        o += len;
    } else {
        int le = enc == BTN_ENC_UTF16LE;
        if (enc == BTN_ENC_UTF16LE || enc == BTN_ENC_UTF16BE) {
            out[o++] = (char)(le ? 0xFF : 0xFE);
            out[o++] = (char)(le ? 0xFE : 0xFF);
        }
        for (size_t i = 0; i < len;) {
            uint32_t cp;
            size_t n = utf8_seq(u + i, len - i, &cp);
            if (n == 0) { /* kein UTF-8: wie in der Anzeige ein Latin-1-Zeichen */
                cp = u[i];
                n = 1;
            }
            if (enc == BTN_ENC_LATIN1 || enc == BTN_ENC_WIN1252) {
                int b = -1;
                if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) {
                    b = (int)cp;
                } else if (cp >= 0x80 && cp <= 0x9F) {
                    /* Latin-1: C1-Steuerzeichen; Windows-1252: nur die
                     * unbelegten Bytes (sonst waere es ein anderes Zeichen) */
                    b = enc == BTN_ENC_LATIN1 || !WIN1252_HIGH[cp - 0x80] ? (int)cp : -1;
                }
                if (b < 0 && enc == BTN_ENC_WIN1252) {
                    for (int k = 0; k < 32; k++) {
                        if (WIN1252_HIGH[k] && WIN1252_HIGH[k] == cp) {
                            b = 0x80 + k;
                            break;
                        }
                    }
                }
                if (b < 0) {
                    *out_bad = i;
                    free(out);
                    return NULL;
                }
                out[o++] = (char)b;
            } else {
                uint32_t units[2];
                int nu = 1;
                if (cp >= 0x10000) {
                    units[0] = 0xD800 + ((cp - 0x10000) >> 10);
                    units[1] = 0xDC00 + ((cp - 0x10000) & 0x3FF);
                    nu = 2;
                } else {
                    units[0] = cp;
                }
                for (int k = 0; k < nu; k++) {
                    out[o++] = (char)(le ? (units[k] & 0xFF) : (units[k] >> 8));
                    out[o++] = (char)(le ? (units[k] >> 8) : (units[k] & 0xFF));
                }
            }
            i += n;
        }
    }
    out[o] = '\0';
    *out_len = o;
    return out;
}

const char *btn_enc_name(BtnEncoding enc) {
    static const char *const names[BTN_ENC_COUNT] = { "UTF-8",     "UTF-8 (BOM)",  "UTF-16 LE",
                                                      "UTF-16 BE", "ISO-8859-1", "Windows-1252" };
    return enc >= 0 && enc < BTN_ENC_COUNT ? names[enc] : "?";
}
