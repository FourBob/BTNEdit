#ifndef BTN_GAPBUFFER_H
#define BTN_GAPBUFFER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *data;
    size_t capacity;
    size_t gap_start;
    size_t gap_end;
} GapBuffer;

/* Allokation, die nie NULL liefert: bei erschoepftem Speicher Meldung nach
 * stderr und abort() - fuer Stellen, die einen Fehlschlag nicht sauber
 * zurueckmelden koennen (Gap-Buffer, Row-Layout). Ein kontrollierter Abbruch
 * mit Meldung statt eines Schreibzugriffs ueber NULL, der je nach Stelle
 * erst viel spaeter und an ganz anderer Stelle auffiele. Wo ein Fehlschlag
 * sauber behandelbar ist (Datei laden, Undo-Verlauf), wird er dort
 * behandelt statt hierueber. */
void *btn_xmalloc(size_t n);
void *btn_xrealloc(void *p, size_t n);
/* n * size mit Ueberlaufpruefung (Ueberlauf zaehlt als erschoepfter Speicher). */
size_t btn_xmul(size_t n, size_t size);

void gb_init(GapBuffer *gb, size_t initial_capacity);
void gb_free(GapBuffer *gb);
void gb_insert(GapBuffer *gb, size_t pos, const char *text, size_t len);
void gb_delete(GapBuffer *gb, size_t pos, size_t len);

/* Inline im Header statt in gapbuffer.c: gb_char_at() laeuft in jeder
 * Byte-Schleife (Layout, Zeilen-/Wortzaehlung, Suche) - als Funktionsaufruf
 * ueber die Dateigrenze kostete das bei einem 10-MB-Dokument einen
 * erheblichen Teil jedes Tastendrucks. */
static inline size_t gb_length(const GapBuffer *gb) {
    return gb->capacity - (gb->gap_end - gb->gap_start);
}

static inline char gb_char_at(const GapBuffer *gb, size_t pos) {
    if (pos < gb->gap_start) {
        return gb->data[pos];
    }
    return gb->data[pos + (gb->gap_end - gb->gap_start)];
}

/* Die beiden Haelften des Inhalts (vor und hinter der Luecke), ohne Kopie -
 * gueltig bis zur naechsten Aenderung des Puffers. */
static inline void gb_segments(const GapBuffer *gb, const char **a, size_t *alen, const char **b, size_t *blen) {
    *a = gb->data;
    *alen = gb->gap_start;
    *b = gb->data + gb->gap_end;
    *blen = gb->capacity - gb->gap_end;
}

/* Gibt einen neu allokierten, NUL-terminierten Ausschnitt zurueck (caller muss free() aufrufen). */
char *gb_copy_range(const GapBuffer *gb, size_t start, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BTN_GAPBUFFER_H */
