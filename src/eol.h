#ifndef BTN_EOL_H
#define BTN_EOL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zeilenenden einer Datei. Im Editor-Puffer steht immer nur '\n'; das
 * Format der Datei wird beim Laden erkannt (btn_eol_detect()), der Inhalt
 * vereinheitlicht (btn_eol_normalize()) und beim Sichern zurueckgewandelt
 * (btn_eol_encode()). Reines C, ohne Editor - direkt testbar. */
typedef enum {
    BTN_EOL_LF = 0, /* macOS/Unix */
    BTN_EOL_CRLF,   /* Windows */
    BTN_EOL_CR      /* klassisches Mac OS */
} BtnEol;

/* Haeufigstes Zeilenende in s[0,len); bei Gleichstand LF vor CRLF vor CR,
 * ohne jedes Zeilenende LF. *out_mixed (falls nicht NULL) = 1, wenn mehr
 * als eine Art vorkommt. "\r\n" zaehlt als ein CRLF, nie als CR + LF. */
BtnEol btn_eol_detect(const char *s, size_t len, int *out_mixed);

/* Wandelt "\r\n" und einzelnes '\r' in s an Ort und Stelle zu '\n'.
 * Rueckgabe: neue Laenge (<= len). */
size_t btn_eol_normalize(char *s, size_t len);

/* Neuer Puffer mit jedem '\n' als eol (fuers Sichern), *out_len = seine
 * Laenge. NULL bei BTN_EOL_LF - dann ist s bereits richtig. Andere '\r'
 * in s bleiben unveraendert. Freigabe mit free(). */
char *btn_eol_encode(const char *s, size_t len, BtnEol eol, size_t *out_len);

/* "LF", "CRLF" oder "CR" - fuer Statuszeile und Menue. */
const char *btn_eol_name(BtnEol eol);

#ifdef __cplusplus
}
#endif

#endif /* BTN_EOL_H */
