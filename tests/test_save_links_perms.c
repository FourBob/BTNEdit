/* Sichern ueber Symlinks (Ziel wird geschrieben, Link bleibt) und
 * schreibgeschuetzte Dateien (nur ohne root). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "save_extracted.h"

static int failures = 0;
static void check(int cond, const char *label) {
    printf("%s [%s]\n", cond ? "ok  " : "FAIL", label);
    if (!cond) failures++;
}

static int file_equals(const char *path, const char *expected) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return n == strlen(expected) && memcmp(buf, expected, n) == 0;
}

int main(void) {
    char dir[] = "/tmp/btn_save2_XXXXXX";
    if (!mkdtemp(dir)) return 2;
    char target[512], link[512], ro[512];
    snprintf(target, sizeof(target), "%s/target.txt", dir);
    snprintf(link, sizeof(link), "%s/link.txt", dir);
    snprintf(ro, sizeof(ro), "%s/readonly.txt", dir);
    struct stat st;

    /* Symlink: durch den Link schreiben, Link bleibt Link */
    check(write_file_contents(target, "old\n", 4) == 1, "setup target");
    symlink("target.txt", link);
    check(write_file_contents(link, "new\n", 4) == 1, "L1 save via symlink returns 1");
    lstat(link, &st);
    check(S_ISLNK(st.st_mode), "L2 link is still a symlink (not replaced by regular file)");
    check(file_equals(target, "new\n"), "L3 symlink target received new content");

    /* Schreibgeschuetzt: nicht ueberschreiben (nur wenn nicht root - root ignoriert W_OK-Rechte) */
    check(write_file_contents(ro, "keep\n", 5) == 1, "setup readonly file");
    chmod(ro, 0444);
    if (geteuid() != 0) {
        check(write_file_contents(ro, "CHANGED\n", 8) == 0, "R1 read-only file: save refused");
        check(file_equals(ro, "keep\n"), "R2 read-only content untouched");
    } else {
        printf("skip [R1/R2 read-only: running as root, access(W_OK) always succeeds]\n");
    }

    /* Neue Datei (realpath ENOENT) funktioniert weiterhin */
    char fresh[512];
    snprintf(fresh, sizeof(fresh), "%s/fresh.txt", dir);
    check(write_file_contents(fresh, "f\n", 2) == 1 && file_equals(fresh, "f\n"), "N1 new file via ENOENT path");

    printf(failures ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
