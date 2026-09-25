/* Schutz der Arbeit, die beiden reinen Module: filestamp.c (Aenderung einer
 * Datei erkennen) und recovery.c (Wiederherstellungsdateien schreiben,
 * lesen, Waisen abgestuerzter Prozesse finden) - mit echten Dateien in einem
 * Temp-Ordner. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
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

    /* nur die ctime aendert sich (chmod; so auch cp -p / touch -r, die die
     * mtime zuruecksetzen) */
    btn_file_stamp(path, &s1);
    chmod(path, 0600);
    chmod(path, 0644);
    btn_file_stamp(path, &s2);
    CHECK(s1.ctime_ns != s2.ctime_ns || s1.ctime_ns == 0, "chmod changes the ctime");
    if (s1.ctime_ns != s2.ctime_ns) {
        CHECK(!btn_file_stamp_equal(&s1, &s2), "ctime change alone: changed");
    }
    struct stat raw;
    stat(path, &raw);
    BtnFileStamp s4;
    btn_file_stamp_from_stat(&raw, &s4);
    CHECK(btn_file_stamp_equal(&s2, &s4), "stamp from stat() = stamp from path");

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
    char *name = btn_recovery_file_name(dir, "4711-99", 3);
    snprintf(want, sizeof want, "%s/4711-99-3.btnrecovery", dir);
    CHECK(name && strcmp(name, want) == 0, "file name <run>-<id>.btnrecovery");
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
    BtnFileStamp disk = { 1, 7, 123456789012ULL, 42, 1700000000123456789LL, 1700000001987654321LL };
    for (size_t split = 0; split <= tlen; split++) {
        if (!btn_recovery_write(file, path, 1, 0, 0, &disk, text, split, text + split, tlen - split)) {
            bad++;
            continue;
        }
        BtnRecovered r;
        if (!btn_recovery_read(file, &r) || r.len != tlen || memcmp(r.text, text, tlen) != 0 || !r.path ||
            strcmp(r.path, path) != 0 || r.eol != 1 || r.raw != 0 || r.binary != 0 || r.text[tlen] != 0 ||
            !btn_file_stamp_equal(&r.disk, &disk)) {
            bad++;
        }
        btn_recovery_free(&r);
    }
    CHECK(bad == 0, "round trip for every gap position, path with newline, NUL in text, file stamp (%ld bad)", bad);
    CHECK(count_entries(dir) == 1, "no temp files left behind");

    BtnRecovered r;
    CHECK(btn_recovery_write(file, NULL, 2, 1, 1, NULL, NULL, 0, NULL, 0) && btn_recovery_read(file, &r) && r.path == NULL &&
              r.len == 0 && r.text && r.eol == 2 && r.raw == 1 && r.binary == 1 && !r.disk.valid,
          "untitled, empty, CR/raw/binary, no stamp");
    btn_recovery_free(&r);

    /* Beschaedigte Dateien */
    btn_recovery_write(file, "/p", 0, 0, 0, &disk, "abc", 3, "def", 3);
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
        "BTNEdit-Recovery 1\n0 0 0 0 0\n",
        "BTNEdit-Recovery 2\n3 0 0 0 0 0 0 0 0 0 0\n",
        "BTNEdit-Recovery 2\n0 2 0 0 0 0 0 0 0 0 0\n",
        "BTNEdit-Recovery 2\n0 0 0 0 0 2 0 0 0 0 0\n",
        "BTNEdit-Recovery 2\n0 0 0 0 0\n",
        "BTNEdit-Recovery 2\n0 0 0 2 0 0 0 0 0 0 0\na\0",
        "something else",
    };
    for (size_t i = 0; i < sizeof bad_files / sizeof bad_files[0]; i++) {
        size_t n = strlen(bad_files[i]);
        if (i == 5) {
            n = strlen("BTNEdit-Recovery 2\n0 0 0 2 0 0 0 0 0 0 0\n") + 2;
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
    CHECK(!btn_recovery_write("/nonexistent-dir/x.btnrecovery", NULL, 0, 0, 0, NULL, "a", 1, NULL, 0), "unwritable dir: 0");
}

/* Haelt die Sperre eines "anderen laufenden Programms" ueber einen eigenen
 * Deskriptor (flock gilt pro offener Datei, auch im selben Prozess). */
static int hold_lock(const char *dir, const char *run) {
    char p[500];
    snprintf(p, sizeof p, "%s/%s.lock", dir, run);
    int fd = open(p, O_RDWR | O_CREAT, 0600);
    flock(fd, LOCK_EX | LOCK_NB);
    return fd;
}

static void touch_file(const char *dir, const char *name) {
    char f[600];
    snprintf(f, sizeof f, "%s/%s", dir, name);
    write_file(f, "x", 1);
}

static int exists(const char *dir, const char *name) {
    char f[600];
    snprintf(f, sizeof f, "%s/%s", dir, name);
    return access(f, F_OK) == 0;
}

static void test_orphans(void) {
    char dir[300];
    snprintf(dir, sizeof dir, "%s/orph", g_tmp);
    char *own = btn_recovery_begin_run(dir);
    CHECK(own != NULL, "run started");
    if (!own) {
        return;
    }
    char lock[400];
    snprintf(lock, sizeof lock, "%s.lock", own);
    CHECK(exists(dir, lock), "lock file of the own run");
    char *second = btn_recovery_begin_run(dir);
    CHECK(second && strcmp(second, own) != 0, "a second run gets its own id (%s / %s)", own, second ? second : "-");
    free(second);

    char name[300];
    /* abgestuerzter Lauf ohne Sperrdatei, abgestuerzter Lauf mit freier
     * Sperre, laufender fremder Lauf, eigener Lauf */
    const char *dead = "100-1", *alive = "100-3"; /* dazu "100-2": tot, Sperrdatei frei */
    unsigned ids[] = { 10, 2, 1 };
    for (int i = 0; i < 3; i++) {
        snprintf(name, sizeof name, "%s-%u.btnrecovery", dead, ids[i]);
        touch_file(dir, name);
    }
    touch_file(dir, "100-2.lock");
    touch_file(dir, "100-2-5.btnrecovery");
    int fd = hold_lock(dir, alive);
    touch_file(dir, "100-3-1.btnrecovery");
    touch_file(dir, "100-3-4.btnrecovery.AbC123");
    snprintf(name, sizeof name, "%s-1.btnrecovery", own);
    touch_file(dir, name);
    touch_file(dir, "100-1-3.btnrecovery.AbC123");
    /* Gleiche pid wie der eigene Prozess, aber ein frueherer Lauf (Neustart) */
    char same_pid[200];
    snprintf(same_pid, sizeof same_pid, "%ld-1-7.btnrecovery", (long)getpid());
    touch_file(dir, same_pid);
    const char *foreign[] = { "notes.txt", "12-3.btnrecovery.damaged", "a-1.btnrecovery", "-1.btnrecovery",
                              "5-.btnrecovery", "1-2.btnrecovery.toolong" };
    for (size_t i = 0; i < sizeof foreign / sizeof foreign[0]; i++) {
        touch_file(dir, foreign[i]);
    }
    char **files;
    size_t n = btn_recovery_find_orphans(dir, own, &files);
    const char *want[] = { "100-1-1", "100-1-2", "100-1-10", "100-2-5", NULL };
    char same_run[64];
    snprintf(same_run, sizeof same_run, "%ld-1-7", (long)getpid());
    const char *expected[6];
    size_t ne = 0;
    /* strcmp-Reihenfolge der Laeufe: "100-1" < "100-2" < "<pid>-1" nur, wenn
     * die pid mit einer Ziffer > '1' beginnt - deshalb gezielt einsortieren. */
    int pid_first = strcmp(same_run, "100-1") < 0;
    if (pid_first) {
        expected[ne++] = same_run;
    }
    for (int i = 0; want[i]; i++) {
        expected[ne++] = want[i];
    }
    if (!pid_first) {
        expected[ne++] = same_run;
    }
    CHECK(n == ne, "orphans: dead runs incl. an earlier run with our pid (%zu, want %zu)", n, ne);
    for (size_t i = 0; i < n && i < ne; i++) {
        char w[600];
        snprintf(w, sizeof w, "%s/%s.btnrecovery", dir, expected[i]);
        CHECK(strcmp(files[i], w) == 0, "orphan %zu in tab order: %s", i, files[i]);
    }
    btn_recovery_free_list(files, n);
    CHECK(!exists(dir, "100-1-3.btnrecovery.AbC123"), "half-written temp file of a dead run removed");
    CHECK(exists(dir, "100-3-4.btnrecovery.AbC123"), "temp file of a running run kept");

    /* Aufraeumen der Sperren: nur tote Laeufe ohne Dateien */
    touch_file(dir, "100-9.lock");
    touch_file(dir, "100-1.lock"); /* toter Lauf, dessen Dateien noch da sind */
    unlink(strcat(strcpy(name, dir), "/100-2-5.btnrecovery"));
    btn_recovery_cleanup_locks(dir, own);
    CHECK(!exists(dir, "100-9.lock") && !exists(dir, "100-2.lock"), "dead locks without files removed");
    CHECK(exists(dir, "100-3.lock") && exists(dir, lock), "locks of running runs kept");
    CHECK(exists(dir, "100-1.lock"), "lock of a dead run with remaining files kept");
    close(fd);
    CHECK(btn_recovery_find_orphans("/nonexistent-dir", own, &files) == 0 && files == NULL, "missing dir: none");
    free(own);
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
