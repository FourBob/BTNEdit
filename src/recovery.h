#ifndef BTN_RECOVERY_H
#define BTN_RECOVERY_H

/* Wiederherstellung nach einem Absturz: ungesicherte Dokumente werden
 * regelmaessig als eigene Datei im Wiederherstellungsordner abgelegt (nie
 * ueber das Original). Reines C, ohne AppKit.
 *
 * Jeder Programmlauf hat eine eigene Kennung "<pid>-<startzeit>" und haelt,
 * solange er laeuft, eine Sperre (flock) auf "<kennung>.lock". Dateien eines
 * Laufs, dessen Sperre frei ist, stammen von einem abgestuerzten Lauf - das
 * gilt auch, wenn nach einem Neustart ein anderer Prozess dieselbe pid hat.
 *
 * Dateiformat (binaersicher - Pfade duerfen '\n', Texte NUL enthalten):
 *   "BTNEdit-Recovery 2\n"
 *   "<eol> <raw> <binary> <pfadlaenge> <textlaenge> <stempel: valid dev ino
 *    size mtime_ns ctime_ns>\n"
 *   <pfad-bytes><text-bytes>
 * Pfadlaenge 0 = unbenanntes Dokument. Der Text ist der Pufferinhalt (bei
 * raw = 0 mit '\n' als Zeilenende, eol ist das Format der Datei). Der
 * Stempel ist der Stand der Datei, auf den sich die Aenderungen beziehen -
 * hat sie sich danach geaendert (git pull nach dem Absturz), fragt Sichern
 * nach. */

#include <stddef.h>
#include "filestamp.h"

typedef struct {
    int eol;      /* BtnEol */
    int raw;
    int binary;
    char *path;   /* malloc, NULL = unbenannt */
    char *text;   /* malloc, NUL-terminiert (darf NUL enthalten, siehe len) */
    size_t len;
    BtnFileStamp disk;
} BtnRecovered;

/* "$HOME/Library/Application Support/BTNEdit/Recovery" (malloc), NULL ohne HOME. */
char *btn_recovery_dir(void);

/* Legt dir samt Elternordnern an (neue Ordner 0700). 1 = vorhanden. */
int btn_recovery_ensure_dir(const char *dir);

/* Beginnt einen Lauf: legt dir an, erzeugt die Kennung und haelt die Sperre
 * bis zum Prozessende (der Deskriptor bleibt offen). Rueckgabe: Kennung
 * (malloc) oder NULL (dann keine Wiederherstellung). */
char *btn_recovery_begin_run(const char *dir);

/* "<dir>/<kennung>-<id>.btnrecovery" (malloc). */
char *btn_recovery_file_name(const char *dir, const char *run, unsigned id);

/* Schreibt atomar (Tempdatei + rename()): der Text ist a[0..alen) +
 * b[0..blen) - direkt die beiden Haelften des Gap-Buffers. disk darf NULL
 * sein (kein Stand bekannt). 1 = geschrieben. */
int btn_recovery_write(const char *file, const char *orig_path, int eol, int raw, int binary,
                       const BtnFileStamp *disk, const char *a, size_t alen, const char *b, size_t blen);

/* 1 = gelesen und vollstaendig (out muss mit btn_recovery_free() freigegeben
 * werden); 0 = fehlt, fremdes Format, abgeschnitten oder zu lang. */
int btn_recovery_read(const char *file, BtnRecovered *out);
void btn_recovery_free(BtnRecovered *r);

/* Wiederherstellungsdateien in dir, deren Lauf nicht mehr laeuft (nicht
 * own_run, Sperre frei). *out_files: malloc-Array von malloc-Pfaden, in der
 * Reihenfolge, in der die Tabs angelegt wurden; Rueckgabe: Anzahl. Halb
 * geschriebene Tempdateien toter Laeufe werden dabei geloescht. Mit
 * btn_recovery_free_list() freigeben. */
size_t btn_recovery_find_orphans(const char *dir, const char *own_run, char ***out_files);
void btn_recovery_free_list(char **files, size_t count);

/* Sperrdateien toter Laeufe ohne verbliebene Wiederherstellungsdatei
 * entfernen (nach Wiederherstellen/Verwerfen). */
void btn_recovery_cleanup_locks(const char *dir, const char *own_run);

#endif /* BTN_RECOVERY_H */
