#ifndef BTN_HIGHLIGHT_H
#define BTN_HIGHLIGHT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_TOK_NORMAL,
    BTN_TOK_KEYWORD,
    BTN_TOK_STRING,
    BTN_TOK_COMMENT,
    BTN_TOK_NUMBER,
    BTN_TOK_PREPROCESSOR
} BtnTokenKind;

typedef struct {
    size_t start;  /* relativ zum uebergebenen Text */
    size_t len;
    BtnTokenKind kind;
} BtnToken;

/* Sprachdefinition (Keywords + Kommentarstile) - opak, wird nur ueber
 * btn_highlight_lang_for_path() beschafft und an die anderen Funktionen
 * durchgereicht. */
typedef struct BtnLangSpec BtnLangSpec;

/* Erkennt die Sprache anhand der Dateiendung; NULL bei unbekannter Endung
 * oder path==NULL (dann keine Hervorhebung). */
const BtnLangSpec *btn_highlight_lang_for_path(const char *path);

/* Tokenisiert eine einzelne logische Zeile. starts_in_comment gibt an, ob
 * die Zeile bereits innerhalb eines mehrzeiligen Blockkommentars beginnt
 * (vom vorherigen Aufruf/derselben Funktion fuer die vorherige Zeile
 * uebernehmen); *ends_in_comment berichtet den Zustand fuer die naechste
 * Zeile zurueck - so lassen sich mehrzeilige Blockkommentare mit einem
 * einzigen Vorwaertsdurchlauf ueber das Dokument korrekt verfolgen, ohne
 * eine zweite, separate Scan-Funktion zu brauchen.
 *
 * Schreibt bis zu max_tokens Tokens nach out_tokens (max_tokens darf 0
 * sein, wenn nur *ends_in_comment interessiert) und gibt die tatsaechliche
 * Tokenanzahl zurueck (kann groesser als max_tokens sein - ueberzaehlige
 * werden nicht geschrieben, aber mitgezaehlt). */
size_t btn_highlight_tokenize(const char *text, size_t len, const BtnLangSpec *lang,
                               int starts_in_comment, int *ends_in_comment,
                               BtnToken *out_tokens, size_t max_tokens);

#ifdef __cplusplus
}
#endif

#endif /* BTN_HIGHLIGHT_H */
