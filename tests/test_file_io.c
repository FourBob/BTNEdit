/* Datei laden (Groessendeckel, Verzeichnis, FIFO, malloc-Fehler) und Recent-
 * Files-Liste (ueberlange Zeilen) aus main.c. */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "filestamp.h"

static int fail_malloc = 0;
static void *test_malloc(size_t n) { return fail_malloc ? NULL : malloc(n); }

typedef enum { BTN_READ_OK = 0, BTN_READ_FAILED, BTN_READ_TOO_LARGE } BtnReadResult;
#define BTN_MAX_RECENT_FILES 10
static char *g_recent_paths[BTN_MAX_RECENT_FILES];
static int g_recent_count = 0;
static const char *g_list_path;
static char *recent_file_list_path(void) { return strdup(g_list_path); }
static char *btn_dup_cstring(const char *s) { return strdup(s); }

#define malloc test_malloc
#include "fileio_extracted.h"
#undef malloc

static int fails = 0;
#define CHECK(c, m) do { printf("%s %s\n", (c) ? "ok  " : "FAIL", m); if (!(c)) fails++; } while (0)

int main(void) {
    char dir[] = "/tmp/b4fioXXXXXX";
    if (!mkdtemp(dir)) return 2;
    char p[512];
    size_t len; BtnReadResult r; char *b;

    snprintf(p, sizeof p, "%s/a.txt", dir);
    FILE *f = fopen(p, "wb"); fwrite("h\0\xE4llo\n", 1, 7, f); fclose(f);
    BtnFileStamp st0, st1;
    b = read_file_contents(p, &len, &r, &st0);
    btn_file_stamp(p, &st1);
    CHECK(b && st0.valid && btn_file_stamp_equal(&st0, &st1), "read returns the stamp of the file it read");
    free(b);
    b = read_file_contents(p, &len, &r, NULL);
    CHECK(b && r == BTN_READ_OK && len == 7 && memcmp(b, "h\0\xE4llo\n", 7) == 0 && b[7] == 0, "normal file incl. NUL/Latin-1 bytes");
    free(b);

    fail_malloc = 1;
    b = read_file_contents(p, &len, &r, NULL);
    CHECK(!b && r == BTN_READ_FAILED, "malloc failure -> FAILED, no crash");
    fail_malloc = 0;

    snprintf(p, sizeof p, "%s/empty", dir);
    f = fopen(p, "wb"); fclose(f);
    b = read_file_contents(p, &len, &r, NULL);
    CHECK(b && r == BTN_READ_OK && len == 0, "empty file");
    free(b);

    b = read_file_contents(dir, &len, &r, NULL);
    CHECK(!b && r == BTN_READ_FAILED, "directory -> FAILED (was: opened as empty file)");

    snprintf(p, sizeof p, "%s/missing", dir);
    b = read_file_contents(p, &len, &r, NULL);
    CHECK(!b && r == BTN_READ_FAILED, "missing file -> FAILED");

    snprintf(p, sizeof p, "%s/fifo", dir);
    if (mkfifo(p, 0644) == 0) {
        alarm(5);   /* alte Logik: fopen() blockiert hier fuer immer */
        b = read_file_contents(p, &len, &r, NULL);
        alarm(0);
        CHECK(!b && r == BTN_READ_FAILED, "named pipe -> FAILED without blocking");
        unlink(p);
    }

    snprintf(p, sizeof p, "%s/huge", dir);
    int fd = open(p, O_CREAT | O_WRONLY, 0644);
    int sparse_ok = fd >= 0 && ftruncate(fd, (off_t)2 << 40) == 0;   /* 2 TB, sparse */
    close(fd);
    if (sparse_ok) {
        b = read_file_contents(p, &len, &r, NULL);
        CHECK(!b && r == BTN_READ_TOO_LARGE, "2 TB sparse file -> TOO_LARGE");
        fd = open(p, O_WRONLY); ftruncate(fd, BTN_MAX_FILE_SIZE + 1); close(fd);
        b = read_file_contents(p, &len, &r, NULL);
        CHECK(!b && r == BTN_READ_TOO_LARGE, "cap + 1 byte -> TOO_LARGE");
        fd = open(p, O_WRONLY); ftruncate(fd, BTN_MAX_FILE_SIZE); close(fd);
        fail_malloc = 1;   /* exakt am Deckel: darf versuchen zu allozieren (hier injiziert fehlschlagend) */
        b = read_file_contents(p, &len, &r, NULL);
        fail_malloc = 0;
        CHECK(!b && r == BTN_READ_FAILED, "exactly cap -> allowed (reaches malloc)");
    } else {
        printf("skip sparse tests (ftruncate unsupported)\n");
    }
    unlink(p);

    /* Recent-Liste: ueberlange Zeile, deren Rest ein existierender Pfad ist */
    char a[512], c[512];
    snprintf(a, sizeof a, "%s/a.txt", dir);
    snprintf(c, sizeof c, "%s/empty", dir);
    snprintf(p, sizeof p, "%s/recent", dir);
    f = fopen(p, "w");
    fprintf(f, "%s\n", a);
    for (int i = 0; i < 4095; i++) fputc('x', f);
    fprintf(f, "%s\n", c);                 /* alte Logik: "%s" als eigener Eintrag */
    fprintf(f, "%s/nope\n", dir);          /* existiert nicht */
    fprintf(f, "%s\r\n", c);               /* CRLF */
    fprintf(f, "%s", a);                   /* letzte Zeile ohne \n */
    fclose(f);
    g_list_path = p;
    load_recent_files();
    printf("recent entries: %d\n", g_recent_count);
    for (int i = 0; i < g_recent_count; i++) printf("  [%d] %s\n", i, g_recent_paths[i]);
    CHECK(g_recent_count == 3, "3 entries: a, empty (CRLF), a (no newline)");
    CHECK(g_recent_count == 3 && strcmp(g_recent_paths[0], a) == 0 && strcmp(g_recent_paths[1], c) == 0 &&
          strcmp(g_recent_paths[2], a) == 0, "over-long line and its tail dropped");

    /* Genau 4095 Zeichen + \n passt noch (fgets liest 4095 + '\n'? nein: Puffer 4096 -> 4095 Zeichen, '\n' bleibt) */
    for (int i = 0; i < g_recent_count; i++) free(g_recent_paths[i]);
    g_recent_count = 0;
    f = fopen(p, "w");
    for (int i = 0; i < 4094; i++) fputc('y', f);
    fprintf(f, "\n%s\n", a);
    fclose(f);
    load_recent_files();
    CHECK(g_recent_count == 1 && strcmp(g_recent_paths[0], a) == 0, "4094-char line fits (skipped: missing file), next line kept");
    for (int i = 0; i < g_recent_count; i++) free(g_recent_paths[i]);

    char cmd[600]; snprintf(cmd, sizeof cmd, "rm -rf %s", dir); system(cmd);
    printf("%s\n", fails ? "FAILED" : "ALL TESTS PASSED");
    return fails != 0;
}
