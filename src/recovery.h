#ifndef BTN_RECOVERY_H
#define BTN_RECOVERY_H

/* Wiederherstellung nach einem Absturz: ungesicherte Dokumente werden
 * regelmaessig als eigene Datei im Wiederherstellungsordner abgelegt (nie
 * ueber das Original). Beim naechsten Start gehoeren Dateien, deren Prozess
 * nicht mehr laeuft, zu einem abgestuerzten Lauf und koennen wieder
 * geoeffnet werden. Reines C, ohne AppKit.
 *
 * Dateiformat (binaersicher - Pfade duerfen '\n', Texte NUL enthalten):
 *   "BTNEdit-Recovery 1\n"
 *   "<eol> <raw> <binary> <pfadlaenge> <textlaenge>\n"
 *   <pfad-bytes><text-bytes>
 * Pfadlaenge 0 = unbenanntes Dokument. Der Text ist der Pufferinhalt (bei
 * raw = 0 mit '\n' als Zeilenende, eol ist das Format der Datei). */

#include <stddef.h>

typedef struct {
    int eol;      /* BtnEol */
    int raw;
    int binary;
    char *path;   /* malloc, NULL = unbenannt */
    char *text;   /* malloc, NUL-terminiert (darf NUL enthalten, siehe len) */
    size_t len;
} BtnRecovered;

/* "$HOME/Library/Application Support/BTNEdit/Recovery" (malloc), NULL ohne HOME. */
char *btn_recovery_dir(void);

/* Legt dir samt Elternordnern an (neue Ordner 0700). 1 = vorhanden. */
int btn_recovery_ensure_dir(const char *dir);

/* "<dir>/<pid>-<id>.btnrecovery" (malloc). */
char *btn_recovery_file_name(const char *dir, long pid, unsigned id);

/* Schreibt atomar (Tempdatei + rename()): der Text ist a[0..alen) +
 * b[0..blen) - direkt die beiden Haelften des Gap-Buffers. 1 = geschrieben. */
int btn_recovery_write(const char *file, const char *orig_path, int eol, int raw, int binary,
                       const char *a, size_t alen, const char *b, size_t blen);

/* 1 = gelesen und vollstaendig (out muss mit btn_recovery_free() freigegeben
 * werden); 0 = fehlt, fremdes Format, abgeschnitten oder zu lang. */
int btn_recovery_read(const char *file, BtnRecovered *out);
void btn_recovery_free(BtnRecovered *r);

/* Wiederherstellungsdateien in dir, deren Prozess nicht mehr laeuft (nicht
 * self_pid, kill(pid, 0) meldet ESRCH). *out_files: malloc-Array von
 * malloc-Pfaden, sortiert; Rueckgabe: Anzahl. Mit
 * btn_recovery_free_list() freigeben. */
size_t btn_recovery_find_orphans(const char *dir, long self_pid, char ***out_files);
void btn_recovery_free_list(char **files, size_t count);

#endif /* BTN_RECOVERY_H */
