/* Schutz der Arbeit, die beiden reinen Module: filestamp.c (Aenderung einer
 * Datei erkennen) und recovery.c (Wiederherstellungsdateien schreiben,
 * lesen, Waisen abgestuerzter Prozesse finden) - mit echten Dateien in einem
 * Temp-Ordner. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "filestamp.h"
#include "recovery.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 30) { printf("FAIL: " __VA_ARGS__); printf("\n"); } fails++; } } while (0)

static char g_tmp[256];

static void write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    fwrite(data, 1, len, f);
    fclose(f);
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(*len + 1);
    *len = fread(buf, 1, *len, f);
    fclose(f);
    return buf;
}

static long dead_pid(void) {
    pid_t p = fork();
    if (p == 0) {
        _exit(0);
    }
    waitpid(p, NULL, 0);
    return (long)p;
}

static int count_entries(const char *dir) {
    char cmd[512];
    snprintf(cmd, sizeof cmd, "ls -A '%s' | wc -l", dir);
    FILE *p = popen(cmd, "r");
    int n = -1;
    if (fscanf(p, "%d", &n) != 1) {
        n = -1;
    }
    pclose(p);
    return n;
}

static void test_stamps(void) {
    char path[300], other[300];
    snprintf(path, sizeof path, "%s/a.txt", g_tmp);
    snprintf(other, sizeof other, "%s/b.txt", g_tmp);
    write_file(path, "hello", 5);
    BtnFileStamp s1, s2;
    CHECK(btn_file_stamp(path, &s1) && s1.valid && s1.size == 5, "stamp of an existing file");
    btn_file_stamp(path, &s2);
    CHECK(btn_file_stamp_equal(&s1, &s2), "unchanged file: same stamp");

    /* gleiche Groesse, gleiche Inode, nur 1 ns spaeter */
    write_file(path, "HELLO", 5);
    struct timespec ts[2] = { { 0, UTIME_OMIT }, { 1700000000, 5 } };
    utimensat(AT_FDCWD, path, ts, 0);
    btn_file_stamp(path, &s1);
    ts[1].tv_nsec = 6;
    utimensat(AT_FDCWD, path, ts, 0);
    btn_file_stamp(path, &s2);
    CHECK(!btn_file_stamp_equal(&s1, &s2) && s1.ino == s2.ino && s1.size == s2.size,
          "same size and inode, mtime 1 ns later: changed");

    /* Ersetzen per rename() (Editoren, git): neue Inode, selbst mit
     * gleicher Groesse und gleicher mtime */
    write_file(other, "HELLO", 5);
    utimensat(AT_FDCWD, other, ts, 0);
    rename(other, path);
    BtnFileStamp s3;
    btn_file_stamp(path, &s3);
    CHECK(!btn_file_stamp_equal(&s2, &s3), "replaced via rename with equal size/mtime: changed (inode)");

    unlink(path);
    BtnFileStamp m1, m2;
    CHECK(!btn_file_stamp(path, &m1) && !m1.valid, "missing file: invalid stamp");
    btn_file_stamp(path, &m2);
    CHECK(btn_file_stamp_equal(&m1, &m2), "still missing: equal");
    CHECK(!btn_file_stamp_equal(&m1, &s3) && !btn_file_stamp_equal(&s3, &m1), "missing vs present: changed");
    CHECK(!btn_file_stamp(g_tmp, &m1), "directory: no stamp");
    CHECK(!btn_file_stamp(NULL, &m1) && !m1.valid, "NULL path: no stamp");
}

static void test_dir(void) {
    char home[300];
    snprintf(home, sizeof home, "%s/home", g_tmp);
    setenv("HOME", home, 1);
    char *dir = btn_recovery_dir();
    char want[400];
    snprintf(want, sizeof want, "%s/Library/Application Support/BTNEdit/Recovery", home);
    CHECK(dir && strcmp(dir, want) == 0, "recovery dir under HOME (%s)", dir ? dir : "NULL");
    CHECK(btn_recovery_ensure_dir(dir), "nested directory created");
    struct stat st;
    CHECK(stat(dir, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 077) == 0, "private (0700)");
    CHECK(btn_recovery_ensure_dir(dir), "existing directory is fine");
    char *name = btn_recovery_file_name(dir, 4711, 3);
    snprintf(want, sizeof want, "%s/4711-3.btnrecovery", dir);
    CHECK(name && strcmp(name, want) == 0, "file name <pid>-<id>.btnrecovery");
    free(name);
    free(dir);
    setenv("HOME", "", 1);
    CHECK(btn_recovery_dir() == NULL, "empty HOME: no recovery");
    unsetenv("HOME");
    CHECK(btn_recovery_dir() == NULL, "no HOME: no recovery");
}

static void test_roundtrip(void) {
    char dir[300], file[400];
    snprintf(dir, sizeof dir, "%s/rt", g_tmp);
    btn_recovery_ensure_dir(dir);
    snprintf(file, sizeof file, "%s/1-1.btnrecovery", dir);

    const char text[] = "line 1\nNUL\0inside\n\xC3\xA4\xFF end";
    size_t tlen = sizeof text - 1;
    const char *path = "/Users/x/odd name\nwith newline.txt";
    long bad = 0;
    for (size_t split = 0; split <= tlen; split++) {
        if (!btn_recovery_write(file, path, 1, 0, 0, text, split, text + split, tlen - split)) {
            bad++;
            continue;
        }
        BtnRecovered r;
        if (!btn_recovery_read(file, &r) || r.len != tlen || memcmp(r.text, text, tlen) != 0 || !r.path ||
            strcmp(r.path, path) != 0 || r.eol != 1 || r.raw != 0 || r.binary != 0 || r.text[tlen] != 0) {
            bad++;
        }
        btn_recovery_free(&r);
    }
    CHECK(bad == 0, "round trip for every gap position, path with newline, NUL in text (%ld bad)", bad);
    CHECK(count_entries(dir) == 1, "no temp files left behind");

    BtnRecovered r;
    CHECK(btn_recovery_write(file, NULL, 2, 1, 1, NULL, 0, NULL, 0) && btn_recovery_read(file, &r) && r.path == NULL &&
              r.len == 0 && r.text && r.eol == 2 && r.raw == 1 && r.binary == 1,
          "untitled, empty, CR/raw/binary");
    btn_recovery_free(&r);

    /* Beschaedigte Dateien */
    btn_recovery_write(file, "/p", 0, 0, 0, "abc", 3, "def", 3);
    size_t full_len;
    char *full = slurp(file, &full_len);
    long accepted = 0;
    for (size_t cut = 0; cut < full_len; cut++) {
        write_file(file, full, cut);
        if (btn_recovery_read(file, &r)) {
            accepted++;
            btn_recovery_free(&r);
        }
    }
    CHECK(accepted == 0, "every truncation rejected (%ld accepted)", accepted);
    char *longer = malloc(full_len + 1);
    memcpy(longer, full, full_len);
    longer[full_len] = 'x';
    write_file(file, longer, full_len + 1);
    CHECK(!btn_recovery_read(file, &r), "trailing garbage rejected");
    write_file(file, full, full_len);
    CHECK(btn_recovery_read(file, &r), "intact file accepted again");
    btn_recovery_free(&r);
    free(longer);
    free(full);
    const char *bad_files[] = {
        "BTNEdit-Recovery 2\n0 0 0 0 0\n",
        "BTNEdit-Recovery 1\n3 0 0 0 0\n",
        "BTNEdit-Recovery 1\n0 2 0 0 0\n",
        "BTNEdit-Recovery 1\n0 0 0 0\n",
        "BTNEdit-Recovery 1\n0 0 0 2 0\na\0",
        "something else",
    };
    for (size_t i = 0; i < sizeof bad_files / sizeof bad_files[0]; i++) {
        size_t n = strlen(bad_files[i]);
        if (i == 4) {
            n = strlen("BTNEdit-Recovery 1\n0 0 0 2 0\n") + 2;
        }
        write_file(file, bad_files[i], n);
        int ok = btn_recovery_read(file, &r);
        CHECK(!ok, "damaged header %zu rejected", i);
        if (ok) {
            btn_recovery_free(&r);
        }
    }
    unlink(file);
    CHECK(!btn_recovery_read(file, &r), "missing file");
    CHECK(!btn_recovery_write("/nonexistent-dir/x.btnrecovery", NULL, 0, 0, 0, "a", 1, NULL, 0), "unwritable dir: 0");
}

static void test_orphans(void) {
    char dir[300], f[500];
    snprintf(dir, sizeof dir, "%s/orph", g_tmp);
    btn_recovery_ensure_dir(dir);
    long dead = dead_pid(), self = (long)getpid(), alive = (long)getppid();
    unsigned ids[] = { 10, 2, 1 };
    for (int i = 0; i < 3; i++) {
        snprintf(f, sizeof f, "%s/%ld-%u.btnrecovery", dir, dead, ids[i]);
        write_file(f, "x", 1);
    }
    snprintf(f, sizeof f, "%s/%ld-1.btnrecovery", dir, self);
    write_file(f, "x", 1);
    snprintf(f, sizeof f, "%s/%ld-1.btnrecovery", dir, alive);
    write_file(f, "x", 1);
    snprintf(f, sizeof f, "%s/%ld-3.btnrecovery.AbC123", dir, dead);
    write_file(f, "x", 1);
    snprintf(f, sizeof f, "%s/%ld-4.btnrecovery.AbC123", dir, alive);
    write_file(f, "x", 1);
    const char *foreign[] = { "notes.txt", "12-3.btnrecovery.damaged", "-1-2.btnrecovery", "x-1.btnrecovery", "5-.btnrecovery" };
    for (size_t i = 0; i < sizeof foreign / sizeof foreign[0]; i++) {
        snprintf(f, sizeof f, "%s/%s", dir, foreign[i]);
        write_file(f, "x", 1);
    }
    char **files;
    size_t n = btn_recovery_find_orphans(dir, self, &files);
    CHECK(n == 3, "three orphans of the dead process (%zu)", n);
    char want[500];
    for (size_t i = 0; i < n && i < 3; i++) {
        snprintf(want, sizeof want, "%s/%ld-%u.btnrecovery", dir, dead, i == 0 ? 1u : i == 1 ? 2u : 10u);
        CHECK(strcmp(files[i], want) == 0, "orphan %zu in tab order: %s", i, files[i]);
    }
    btn_recovery_free_list(files, n);
    snprintf(f, sizeof f, "%s/%ld-3.btnrecovery.AbC123", dir, dead);
    CHECK(access(f, F_OK) != 0, "half-written temp file of a dead process removed");
    snprintf(f, sizeof f, "%s/%ld-4.btnrecovery.AbC123", dir, alive);
    CHECK(access(f, F_OK) == 0, "temp file of a running process kept");
    CHECK(btn_recovery_find_orphans("/nonexistent-dir", self, &files) == 0 && files == NULL, "missing dir: none");
}

int main(void) {
    snprintf(g_tmp, sizeof g_tmp, "/tmp/btn_recovery_XXXXXX");
    if (!mkdtemp(g_tmp)) {
        return 1;
    }
    test_stamps();
    test_dir();
    test_roundtrip();
    test_orphans();
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_tmp);
    if (system(cmd) != 0) {
        printf("cleanup failed\n");
    }
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
