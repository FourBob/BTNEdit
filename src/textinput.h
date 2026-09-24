#ifndef BTN_TEXTINPUT_H
#define BTN_TEXTINPUT_H

#include <stddef.h>
#include <stdint.h>
#include "editor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Eingabemethoden (Tottasten, Pinyin, Kana, Emoji-Palette, Akzent-Menue beim
 * Gedrueckthalten): macOS spricht ueber NSTextInputClient in UTF-16-
 * Einheiten, der Editor in Bytes. Hier die reine Umrechnung und der
 * Zustand des vorlaeufigen ("marked") Texts - ohne Cocoa, direkt testbar.
 *
 * Gezaehlt wird nach derselben Zeichenregel wie ueberall
 * (btn_utf8_char_len): ein 4-Byte-Zeichen sind 2 UTF-16-Einheiten
 * (Surrogatpaar), jedes andere Zeichen - auch ein einzelnes ungueltiges
 * Byte, das als Latin-1 gezeichnet wird - eine.
 *
 * Koordinaten fuer macOS: UTF-16-Einheiten ab einem "Ursprung" kurz vor
 * dem Cursor (Anfang seiner Zeile, hoechstens BTN_TI_WINDOW Zeichen
 * zurueck). So bleibt jede Abfrage auch in einer riesigen Datei billig;
 * macOS braucht nur den Text um den Cursor (Akzent-Menue: "ersetze das
 * Zeichen vor dem Cursor"). */
#define BTN_TI_WINDOW 1024

size_t btn_ti_utf16_len(const char *s, size_t len);
/* Byte-Offset nach u16 UTF-16-Einheiten, geklemmt auf len. Faellt u16
 * mitten in ein Surrogatpaar, zaehlt das ganze Zeichen mit - nie mitten
 * in einem Zeichen. */
size_t btn_ti_utf16_to_bytes(const char *s, size_t len, size_t u16);

/* Vorlaeufiger Text der Eingabemethode - steht NICHT im Puffer, wird am
 * Cursor darueber gezeichnet, bis die Eingabemethode ihn festschreibt.
 * sel_start/sel_end: Cursor/Markierung darin, in Bytes. */
typedef struct {
    char *text;
    size_t len;
    size_t sel_start;
    size_t sel_end;
} BtnMarkedText;

void btn_marked_set(BtnMarkedText *m, const char *utf8, size_t len, size_t sel_u16_loc, size_t sel_u16_len);
void btn_marked_clear(BtnMarkedText *m);

/* Ursprung (Byte-Offset) fuer ed - siehe oben. */
size_t btn_ti_origin(Editor *ed);
/* Selektion relativ zum Ursprung: Anfang und Laenge in UTF-16 (die Laenge
 * ist bei riesigen Selektionen auf 64 KB Text gekappt - macOS nutzt sie nur
 * zur Orientierung). */
void btn_ti_selection(Editor *ed, size_t *loc_u16, size_t *len_u16);
/* Ursprungsrelativer UTF-16-Bereich -> absolute Bytes. 0, wenn er ueber
 * das Dokumentende hinausreicht. */
int btn_ti_range_to_bytes(Editor *ed, size_t loc, size_t len, size_t *start, size_t *end);
/* Text eines ursprungsrelativen Bereichs als UTF-16 (fuer
 * attributedSubstringForProposedRange:), auf das Dokumentende und
 * hoechstens 4096 Einheiten gekappt. *actual_loc und *n: tatsaechlicher Bereich.
 * NULL, wenn loc hinter dem Dokumentende liegt. Freigabe mit free(). */
uint16_t *btn_ti_substring(Editor *ed, size_t loc, size_t len, size_t *actual_loc, size_t *n);

#ifdef __cplusplus
}
#endif

#endif /* BTN_TEXTINPUT_H */
