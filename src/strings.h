#ifndef BTN_STRINGS_H
#define BTN_STRINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Bediensprache der UI (Menues, Dialoge, Fenstertitel-Fallback) - komplett
 * getrennt von der Syntax-Highlighting-Spracherkennung in highlight.h, die
 * sich auf den INHALT einer Datei bezieht, nicht auf die Bedienoberflaeche. */
typedef enum {
    BTN_LANG_EN = 0,
    BTN_LANG_DE,
    BTN_LANG_FR,
    BTN_LANG_ES,
    BTN_LANG_ZH,
    BTN_LANG_COUNT
} BtnUiLang;

typedef enum {
    BTN_STR_ABOUT_PREFIX = 0,
    BTN_STR_QUIT_PREFIX,
    BTN_STR_FILE_MENU,
    BTN_STR_NEW,
    BTN_STR_OPEN,
    BTN_STR_RECENT,
    BTN_STR_RECENT_EMPTY,
    BTN_STR_SAVE,
    BTN_STR_SAVE_AS,
    BTN_STR_CLOSE,
    BTN_STR_PRINT,
    BTN_STR_EDIT_MENU,
    BTN_STR_UNDO,
    BTN_STR_REDO,
    BTN_STR_CUT,
    BTN_STR_COPY,
    BTN_STR_PASTE,
    BTN_STR_SELECT_ALL,
    BTN_STR_FIND,
    BTN_STR_UNTITLED,
    /* Enthaelt ein "%s" fuer den Dateinamen - mit snprintf zu formatieren,
     * nicht mit NSString stringWithFormat:/%@ (siehe btn_tr()-Kommentar). */
    BTN_STR_SAVE_PROMPT_TITLE_FMT,
    BTN_STR_SAVE_PROMPT_INFO,
    BTN_STR_BTN_SAVE,
    BTN_STR_BTN_DONT_SAVE,
    BTN_STR_BTN_CANCEL,
    BTN_STR_FIND_SEARCH_LABEL,
    BTN_STR_FIND_REPLACE_LABEL,
    BTN_STR_FIND_NOT_FOUND,
    /* Enthaelt ein "%d" fuer die Trefferanzahl - siehe BTN_STR_SAVE_PROMPT_TITLE_FMT. */
    BTN_STR_FIND_REPLACED_FMT,
    BTN_STR_HELP_MENU,
    BTN_STR_HELP_SHORTCUTS,
    BTN_STR_HELP_TITLE,
    /* Mehrzeilige Liste aller Tastenkuerzel (echte '\n', kein Format-String)
     * fuer den NSAlert der Hilfe-Uebersicht. */
    BTN_STR_HELP_BODY,
    BTN_STR_COUNT
} BtnStringId;

/* Setzt die aktive Bediensprache (main() ruft das einmal beim Start anhand
 * von btn_app_detect_system_language() auf, bevor das Menue gebaut wird). */
void btn_strings_set_language(BtnUiLang lang);

/* Uebersetzter, NUL-terminierter UTF-8-String in der aktuell aktiven
 * Sprache - reines Nachschlagen in einer statischen Tabelle, kein malloc,
 * caller darf/soll NICHT free() aufrufen. Enthaelt bewusst "%s" statt "%@",
 * damit main.c (reines C, kein Foundation) dieselbe Tabelle nutzen kann wie
 * shim.m; shim.m formatiert mit snprintf() und wandelt danach erst in
 * NSString um. */
const char *btn_tr(BtnStringId id);

/* Ordnet einen Sprachcode wie "de-DE", "de_DE", "de" oder "zh-Hans-CN"
 * (macOS liefert diese Formate aus NSLocale.preferredLanguages) der
 * passenden BtnUiLang zu; unbekannte/NULL-Codes fallen auf Englisch zurueck.
 * Reine Stringlogik, damit der Shim (der die Sprachcodes von NSLocale holt)
 * sie nicht selbst parsen muss. */
BtnUiLang btn_strings_lang_from_code(const char *code);

#ifdef __cplusplus
}
#endif

#endif /* BTN_STRINGS_H */
