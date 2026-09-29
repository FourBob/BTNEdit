#ifndef BTN_ENCODING_H
#define BTN_ENCODING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zeichenkodierung einer Datei. Im Editor-Puffer steht immer UTF-8; die
 * Kodierung wird beim Laden erkannt (btn_enc_detect()), der Inhalt nach
 * UTF-8 gewandelt (btn_enc_decode()) und beim Sichern zurueck
 * (btn_enc_encode()). Reines C, ohne Editor - direkt testbar.
 *
 * Latin-1 und Windows-1252 bilden alle 256 Bytes verlustfrei ab (die fuenf
 * in Windows-1252 unbelegten Bytes 81 8D 8F 90 9D als U+0081...), ein
 * unveraenderter Text wird also Byte fuer Byte so gesichert, wie er
 * geladen wurde. Reihenfolge = Menue (BTN_MENU_ENC_*). */
typedef enum {
    BTN_ENC_UTF8 = 0,
    BTN_ENC_UTF8_BOM,
    BTN_ENC_UTF16LE,
    BTN_ENC_UTF16BE,
    BTN_ENC_LATIN1,
    BTN_ENC_WIN1252,
    BTN_ENC_COUNT
} BtnEncoding;

/* Kodierung von s[0,len): BOM (UTF-8; UTF-16 LE/BE nur, wenn die Datei auch
 * Null-Bytes enthaelt - sonst ist "ÿþ" am Anfang einer Latin-1-Datei
 * wahrscheinlicher als UTF-16 ganz ohne ASCII und Zeilenenden), sonst
 * gueltiges UTF-8 -> UTF-8; ungueltiges UTF-8 ohne eine einzige gueltige
 * Mehrbyte-Folge -> Windows-1252 (alte Westeuropa-Datei); beides gemischt
 * (UTF-8-Log mit einzelnen kaputten Bytes) -> UTF-8: die Bytes bleiben, wie
 * sie sind (Anzeige wie bisher mit Latin-1-Ersatz, Sichern unveraendert). */
BtnEncoding btn_enc_detect(const char *s, size_t len);

/* Laenge der BOM, mit der s beginnt, falls sie zu enc passt (sonst 0). */
size_t btn_enc_bom_len(const char *s, size_t len, BtnEncoding enc);

/* s[0,len) (samt einer passenden BOM, die wegfaellt) als enc lesen ->
 * neuer UTF-8-Puffer (malloc, NUL-terminiert, *out_len ohne NUL). UTF-8:
 * die Bytes unveraendert. UTF-16: ungerade Laenge oder kaputte Surrogate
 * werden zu U+FFFD und *out_lossy = 1 (dann darf so nicht gesichert
 * werden). NULL nur ohne Speicher. */
char *btn_enc_decode(const char *s, size_t len, BtnEncoding enc, size_t *out_len, int *out_lossy);

/* UTF-8 u[0,len) -> Bytes in enc (samt BOM bei UTF-8 mit BOM, bei UTF-16
 * nur mit utf16_bom - eine ohne BOM geoeffnete Datei bleibt ohne), malloc,
 * *out_len. Ein Byte, das kein gueltiges UTF-8 beginnt (roh
 * geladene Dateien), gilt wie in der Anzeige als Latin-1-Zeichen (bei
 * UTF-8 bleibt es unveraendert). Ein Zeichen, das enc nicht darstellen
 * kann: NULL und *out_bad = Offset in u (sonst (size_t)-1). */
char *btn_enc_encode(const char *u, size_t len, BtnEncoding enc, int utf16_bom, size_t *out_len, size_t *out_bad);

/* "UTF-8", "UTF-8 (BOM)", "UTF-16 LE", ... - fuer Statuszeile und Menue. */
const char *btn_enc_name(BtnEncoding enc);

#ifdef __cplusplus
}
#endif

#endif /* BTN_ENCODING_H */
