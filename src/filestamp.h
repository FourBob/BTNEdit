#ifndef BTN_FILESTAMP_H
#define BTN_FILESTAMP_H

/* Fingerabdruck einer Datei auf der Platte, um Aenderungen durch andere
 * Programme zu erkennen: Geraet + Inode (Ersetzen per rename(), wie es
 * Editoren und git tun), Groesse, Aenderungszeit in Nanosekunden (auch
 * mehrere Aenderungen gleicher Groesse innerhalb einer Sekunde) und die
 * Statusaenderungszeit ctime - die laesst sich nicht zuruecksetzen (cp -p,
 * touch -r) und faengt Dateisysteme mit grober mtime (HFS+, FAT, SMB). */

#include <sys/stat.h>
#include <sys/types.h>

typedef struct {
    int valid;           /* 0 = Datei fehlt / nicht lesbar */
    dev_t dev;
    ino_t ino;
    off_t size;
    long long mtime_ns;
    long long ctime_ns;
} BtnFileStamp;

/* Stempel aus einem schon geholten stat()/fstat()-Ergebnis. */
void btn_file_stamp_from_stat(const struct stat *st, BtnFileStamp *out);

/* Stempel fuer path (folgt Symlinks). Rueckgabe 1 = gefunden; sonst ist
 * out->valid 0. */
int btn_file_stamp(const char *path, BtnFileStamp *out);

/* 1, wenn beide denselben Dateistand beschreiben (zwei ungueltige gelten als
 * gleich: die Datei fehlt weiterhin). */
int btn_file_stamp_equal(const BtnFileStamp *a, const BtnFileStamp *b);

#endif /* BTN_FILESTAMP_H */
