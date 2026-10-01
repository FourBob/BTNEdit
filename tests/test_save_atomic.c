/* Atomares Sichern: Schreibfehler lassen das Original unangetastet, keine
 * Tempdatei-Reste, Rechte bleiben. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static int failures = 0;
static int g_fail_fwrite = 0;

/* Fehlerinjektion: verhaelt sich wie fwrite, meldet aber auf Wunsch "nichts
 * geschrieben" (wie bei ENOSPC). Muss VOR dem Makro definiert sein. */
static size_t test_fwrite(const void *p, size_t sz, size_t n, FILE *f) {
    if (g_fail_fwrite) {
        return 0;
    }
    return fwrite(p, sz, n, f);
}
#define fwrite test_fwrite

/* write_stream_checked + write_file_contents verbatim aus main.c (per sed beim Kompilieren). */
#include "save_extracted.h"

static int count_entries(const char *dir) {
    DIR *d = opendir(dir);
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
            n++;
        }
    }
    closedir(d);
    return n;
}

static char *read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { *len = 0; return NULL; }
    char *buf = malloc(4096);
    *len = fread(buf, 1, 4096, f);
    fclose(f);
    return buf;
}

static void check(int cond, const char *label) {
    printf("%s [%s]\n", cond ? "ok  " : "FAIL", label);
    if (!cond) failures++;
}

int main(void) {
    char dir[] = "/tmp/btn_save_XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 2; }
    char path[512];
    snprintf(path, sizeof(path), "%s/doc.txt", dir);
    size_t len;
    char *got;

    /* (a) neue Datei schreiben */
    check(write_file_contents(path, "hello world\n", 12) == 1, "a: new file returns 1");
    got = read_all(path, &len);
    check(got && len == 12 && memcmp(got, "hello world\n", 12) == 0, "a: content written");
    free(got);
    check(count_entries(dir) == 1, "a: no temp file left behind");

    /* (b) Original vorhanden, Schreiben schlaegt fehl -> Original unangetastet */
    g_fail_fwrite = 1;
    check(write_file_contents(path, "NEW CONTENT\n", 12) == 0, "b: failed write returns 0");
    g_fail_fwrite = 0;
    got = read_all(path, &len);
    check(got && len == 12 && memcmp(got, "hello world\n", 12) == 0, "b: original content intact");
    free(got);
    check(count_entries(dir) == 1, "b: temp file cleaned up");

    /* (c) Rechte des Originals bleiben erhalten */
    chmod(path, 0640);
    check(write_file_contents(path, "v2\n", 3) == 1, "c: overwrite returns 1");
    struct stat st;
    stat(path, &st);
    check((st.st_mode & 07777) == 0640, "c: mode 0640 preserved");
    got = read_all(path, &len);
    check(got && len == 3 && memcmp(got, "v2\n", 3) == 0, "c: content replaced");
    free(got);

    /* (d) Fehler ohne vorhandenes Original -> keine Datei, kein Temp-Rest */
    char path2[512];
    snprintf(path2, sizeof(path2), "%s/new.txt", dir);
    g_fail_fwrite = 1;
    check(write_file_contents(path2, "x", 1) == 0, "d: failed write of new file returns 0");
    g_fail_fwrite = 0;
    check(access(path2, F_OK) != 0, "d: no file created");
    check(count_entries(dir) == 1, "d: no temp file left behind");

    /* (e) Pfad ohne Verzeichnisanteil (relativ, cwd) */
    char cwd[512];
    getcwd(cwd, sizeof(cwd));
    chdir(dir);
    check(write_file_contents("rel.txt", "r\n", 2) == 1, "e: relative path works");
    check(access("rel.txt", F_OK) == 0, "e: file exists");
    chdir(cwd);

    /* (g) NEUE Datei bekommt umask-Rechte (0644 bei 022), nicht mkstemps 0600 */
    char path3[512];
    snprintf(path3, sizeof(path3), "%s/brandnew.txt", dir);
    umask(022);
    check(write_file_contents(path3, "n\n", 2) == 1, "g: new file written");
    stat(path3, &st);
    check((st.st_mode & 07777) == 0644, "g: new file has umask mode 0644 (not 0600)");

    /* (f) leere Datei (0 Byte) */
    check(write_file_contents(path, "", 0) == 1, "f: empty write returns 1");
    got = read_all(path, &len);
    check(len == 0, "f: file is empty");
    free(got);

    printf(failures ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
