#include "recovery.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define RECOVERY_MAGIC "BTNEdit-Recovery 2\n"
#define RECOVERY_SUFFIX ".btnrecovery"
#define LOCK_SUFFIX ".lock"
#define RECOVERY_MAX_PATH ((size_t)1 << 16)

static char *join(const char *a, const char *b) {
    size_t n = strlen(a) + strlen(b) + 1;
    char *s = malloc(n);
    if (s) {
        snprintf(s, n, "%s%s", a, b);
    }
    return s;
}

char *btn_recovery_dir(void) {
    const char *home = getenv("HOME");
    if (!home || !*home) {
        return NULL;
    }
    return join(home, "/Library/Application Support/BTNEdit/Recovery");
}

int btn_recovery_ensure_dir(const char *dir) {
    char *p = join(dir, "");
    if (!p) {
        return 0;
    }
    /* Jede Ebene einzeln ("mkdir -p"); schon vorhandene sind in Ordnung. */
    for (char *s = p + 1; *s; s++) {
        if (*s == '/') {
            *s = '\0';
            mkdir(p, 0700);
            *s = '/';
        }
    }
    mkdir(p, 0700);
    struct stat st;
    int ok = stat(p, &st) == 0 && S_ISDIR(st.st_mode);
    free(p);
    return ok;
}

static char *lock_path(const char *dir, const char *run) {
    size_t n = strlen(dir) + strlen(run) + sizeof(LOCK_SUFFIX) + 2;
    char *s = malloc(n);
    if (s) {
        snprintf(s, n, "%s/%s" LOCK_SUFFIX, dir, run);
    }
    return s;
}

char *btn_recovery_begin_run(const char *dir) {
    if (!btn_recovery_ensure_dir(dir)) {
        return NULL;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    char run[64];
    snprintf(run, sizeof(run), "%ld-%lld", (long)getpid(), (long long)tv.tv_sec * 1000000LL + tv.tv_usec);
    char *lock = lock_path(dir, run);
    if (!lock) {
        return NULL;
    }
    int fd = open(lock, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    free(lock);
    if (fd < 0) {
        return NULL;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return NULL;
    }
    /* fd bleibt bis zum Prozessende offen - so lange gilt der Lauf als lebendig. */
    return strdup(run);
}

char *btn_recovery_file_name(const char *dir, const char *run, unsigned id) {
    size_t n = strlen(dir) + strlen(run) + 32;
    char *s = malloc(n);
    if (s) {
        snprintf(s, n, "%s/%s-%u" RECOVERY_SUFFIX, dir, run, id);
    }
    return s;
}

static int write_all(FILE *f, const char *data, size_t len) {
    return len == 0 || fwrite(data, 1, len, f) == len;
}

int btn_recovery_write(const char *file, const char *orig_path, int eol, int raw, int binary,
                       const BtnFileStamp *disk, const char *a, size_t alen, const char *b, size_t blen) {
    BtnFileStamp none = { 0 };
    if (!disk) {
        disk = &none;
    }
    size_t path_len = orig_path ? strlen(orig_path) : 0;
    char *tmp = join(file, ".XXXXXX");
    if (!tmp) {
        return 0;
    }
    int fd = mkstemp(tmp);
    if (fd < 0) {
        free(tmp);
        return 0;
    }
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        unlink(tmp);
        free(tmp);
        return 0;
    }
    int ok = fprintf(f, RECOVERY_MAGIC "%d %d %d %zu %zu %d %llu %llu %lld %lld %lld\n", eol, raw, binary, path_len,
                     alen + blen, disk->valid, (unsigned long long)disk->dev, (unsigned long long)disk->ino,
                     (long long)disk->size, disk->mtime_ns, disk->ctime_ns) > 0 &&
             write_all(f, orig_path, path_len) && write_all(f, a, alen) && write_all(f, b, blen) &&
             fflush(f) == 0 && fsync(fileno(f)) == 0;
    if (fclose(f) != 0) {
        ok = 0;
    }
    if (ok && rename(tmp, file) != 0) {
        ok = 0;
    }
    if (!ok) {
        unlink(tmp);
    }
    free(tmp);
    return ok;
}

int btn_recovery_read(const char *file, BtnRecovered *out) {
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(file, "rb");
    if (!f) {
        return 0;
    }
    struct stat st;
    char magic[sizeof(RECOVERY_MAGIC)];
    char line[256];
    int eol, raw, binary, valid;
    size_t path_len, text_len;
    unsigned long long dev, ino;
    long long size, mtime_ns, ctime_ns;
    int ok = fstat(fileno(f), &st) == 0 && S_ISREG(st.st_mode) &&
             fread(magic, 1, sizeof(RECOVERY_MAGIC) - 1, f) == sizeof(RECOVERY_MAGIC) - 1 &&
             memcmp(magic, RECOVERY_MAGIC, sizeof(RECOVERY_MAGIC) - 1) == 0 &&
             fgets(line, sizeof(line), f) && strchr(line, '\n') &&
             sscanf(line, "%d %d %d %zu %zu %d %llu %llu %lld %lld %lld", &eol, &raw, &binary, &path_len, &text_len,
                    &valid, &dev, &ino, &size, &mtime_ns, &ctime_ns) == 11 &&
             eol >= 0 && eol <= 2 && (raw == 0 || raw == 1) && (binary == 0 || binary == 1) && (valid == 0 || valid == 1) &&
             path_len < RECOVERY_MAX_PATH;
    /* Genau Kopf + Pfad + Text, nicht mehr und nicht weniger (abgeschnitten
     * oder angehaengt = beschaedigt). */
    if (ok) {
        long head = ftell(f);
        ok = head > 0 && text_len <= (size_t)st.st_size &&
             (off_t)((size_t)head + path_len + text_len) == st.st_size;
    }
    if (ok) {
        out->path = path_len ? malloc(path_len + 1) : NULL;
        out->text = malloc(text_len + 1);
        ok = out->text && (!path_len || out->path) &&
             (!path_len || fread(out->path, 1, path_len, f) == path_len) &&
             fread(out->text, 1, text_len, f) == text_len &&
             (!path_len || !memchr(out->path, '\0', path_len));
    }
    fclose(f);
    if (!ok) {
        btn_recovery_free(out);
        return 0;
    }
    if (out->path) {
        out->path[path_len] = '\0';
    }
    out->text[text_len] = '\0';
    out->len = text_len;
    out->eol = eol;
    out->raw = raw;
    out->binary = binary;
    if (valid) {
        out->disk.valid = 1;
        out->disk.dev = (dev_t)dev;
        out->disk.ino = (ino_t)ino;
        out->disk.size = (off_t)size;
        out->disk.mtime_ns = mtime_ns;
        out->disk.ctime_ns = ctime_ns;
    }
    return 1;
}

void btn_recovery_free(BtnRecovered *r) {
    free(r->path);
    free(r->text);
    memset(r, 0, sizeof(*r));
}

/* Laeuft der Lauf run noch? Seine Sperre ist dann belegt. Ohne Sperrdatei
 * (oder mit freier Sperre) ist er beendet bzw. abgestuerzt. */
static int run_alive(const char *dir, const char *run) {
    char *lock = lock_path(dir, run);
    if (!lock) {
        return 1; /* im Zweifel nichts anfassen */
    }
    int fd = open(lock, O_RDWR | O_CLOEXEC);
    free(lock);
    if (fd < 0) {
        return errno != ENOENT;
    }
    int alive = flock(fd, LOCK_EX | LOCK_NB) != 0;
    close(fd); /* gibt eine eben genommene Sperre wieder frei */
    return alive;
}

/* "<run>-<id>.btnrecovery" (tmp = 0) oder deren Tempdatei
 * "<run>-<id>.btnrecovery.XXXXXX" (tmp = 1); run besteht aus Ziffern und
 * '-'. Rueckgabe: Laenge von run, 0 = fremde Datei; *id = Nummer. */
static size_t parse_name(const char *name, int *tmp, unsigned long *id) {
    const char *suffix = strstr(name, RECOVERY_SUFFIX);
    if (!suffix) {
        return 0;
    }
    const char *end = suffix + strlen(RECOVERY_SUFFIX);
    *tmp = *end != '\0';
    if (*tmp && (end[0] != '.' || strlen(end) != 7)) {
        return 0;
    }
    const char *dash = suffix;
    while (dash > name && dash[-1] != '-') {
        dash--;
    }
    if (dash == suffix || dash - 1 <= name) {
        return 0;
    }
    for (const char *c = dash; c < suffix; c++) {
        if (*c < '0' || *c > '9') {
            return 0;
        }
    }
    for (const char *c = name; c < dash - 1; c++) {
        if ((*c < '0' || *c > '9') && *c != '-') {
            return 0;
        }
    }
    *id = strtoul(dash, NULL, 10);
    return (size_t)(dash - 1 - name);
}

typedef struct {
    char *path;
    char *run;
    unsigned long id;
} Orphan;

/* Nach Lauf, dann Nummer - in der Reihenfolge, in der die Tabs angelegt
 * wurden. */
static int cmp_orphans(const void *pa, const void *pb) {
    const Orphan *a = pa, *b = pb;
    int c = strcmp(a->run, b->run);
    if (c != 0) {
        return c;
    }
    return a->id < b->id ? -1 : a->id > b->id;
}

size_t btn_recovery_find_orphans(const char *dir, const char *own_run, char ***out_files) {
    *out_files = NULL;
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    size_t count = 0, cap = 0;
    Orphan *list = NULL;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int tmp = 0;
        unsigned long id = 0;
        size_t run_len = parse_name(e->d_name, &tmp, &id);
        if (run_len == 0) {
            continue;
        }
        char *run = strndup(e->d_name, run_len);
        if (!run || (own_run && strcmp(run, own_run) == 0) || run_alive(dir, run)) {
            free(run);
            continue;
        }
        char *full = malloc(strlen(dir) + strlen(e->d_name) + 2);
        if (!full) {
            free(run);
            continue;
        }
        sprintf(full, "%s/%s", dir, e->d_name);
        if (tmp) {
            unlink(full); /* halb geschriebene Tempdatei eines toten Laufs */
            free(full);
            free(run);
            continue;
        }
        if (count == cap) {
            cap = cap ? cap * 2 : 8;
            Orphan *grown = realloc(list, cap * sizeof(Orphan));
            if (!grown) {
                free(full);
                free(run);
                break;
            }
            list = grown;
        }
        list[count].path = full;
        list[count].run = run;
        list[count].id = id;
        count++;
    }
    closedir(d);
    if (count == 0) {
        free(list);
        return 0;
    }
    qsort(list, count, sizeof(Orphan), cmp_orphans);
    char **files = malloc(count * sizeof(char *));
    for (size_t i = 0; i < count; i++) {
        if (files) {
            files[i] = list[i].path;
        } else {
            free(list[i].path);
        }
        free(list[i].run);
    }
    free(list);
    *out_files = files;
    return files ? count : 0;
}

void btn_recovery_cleanup_locks(const char *dir, const char *own_run) {
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    char **dead = NULL;
    size_t n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        size_t sl = strlen(LOCK_SUFFIX);
        if (len <= sl || strcmp(e->d_name + len - sl, LOCK_SUFFIX) != 0) {
            continue;
        }
        char *run = strndup(e->d_name, len - sl);
        if (!run || (own_run && strcmp(run, own_run) == 0) || run_alive(dir, run)) {
            free(run);
            continue;
        }
        char **grown = realloc(dead, (n + 1) * sizeof(char *));
        if (!grown) {
            free(run);
            break;
        }
        dead = grown;
        dead[n++] = run;
    }
    /* Nur Sperren ohne verbliebene Datei ihres Laufs (auch *.damaged zaehlt
     * nicht - die wird nie wieder angeboten). */
    for (size_t i = 0; i < n; i++) {
        size_t rl = strlen(dead[i]);
        int has_files = 0;
        rewinddir(d);
        while ((e = readdir(d)) != NULL) {
            int tmp;
            unsigned long id;
            if (parse_name(e->d_name, &tmp, &id) == rl && strncmp(e->d_name, dead[i], rl) == 0) {
                has_files = 1;
                break;
            }
        }
        if (!has_files) {
            char *lock = lock_path(dir, dead[i]);
            if (lock) {
                unlink(lock);
                free(lock);
            }
        }
        free(dead[i]);
    }
    free(dead);
    closedir(d);
}

void btn_recovery_free_list(char **files, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(files[i]);
    }
    free(files);
}
