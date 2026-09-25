#include "recovery.h"

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define RECOVERY_MAGIC "BTNEdit-Recovery 1\n"
#define RECOVERY_SUFFIX ".btnrecovery"
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

char *btn_recovery_file_name(const char *dir, long pid, unsigned id) {
    size_t n = strlen(dir) + 64;
    char *s = malloc(n);
    if (s) {
        snprintf(s, n, "%s/%ld-%u" RECOVERY_SUFFIX, dir, pid, id);
    }
    return s;
}

static int write_all(FILE *f, const char *data, size_t len) {
    return len == 0 || fwrite(data, 1, len, f) == len;
}

int btn_recovery_write(const char *file, const char *orig_path, int eol, int raw, int binary,
                       const char *a, size_t alen, const char *b, size_t blen) {
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
    int ok = fprintf(f, RECOVERY_MAGIC "%d %d %d %zu %zu\n", eol, raw, binary, path_len, alen + blen) > 0 &&
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
    char line[128];
    int eol, raw, binary;
    size_t path_len, text_len;
    int ok = fstat(fileno(f), &st) == 0 && S_ISREG(st.st_mode) &&
             fread(magic, 1, sizeof(RECOVERY_MAGIC) - 1, f) == sizeof(RECOVERY_MAGIC) - 1 &&
             memcmp(magic, RECOVERY_MAGIC, sizeof(RECOVERY_MAGIC) - 1) == 0 &&
             fgets(line, sizeof(line), f) && strchr(line, '\n') &&
             sscanf(line, "%d %d %d %zu %zu", &eol, &raw, &binary, &path_len, &text_len) == 5 &&
             eol >= 0 && eol <= 2 && (raw == 0 || raw == 1) && (binary == 0 || binary == 1) &&
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
    return 1;
}

void btn_recovery_free(BtnRecovered *r) {
    free(r->path);
    free(r->text);
    memset(r, 0, sizeof(*r));
}

static int pid_alive(long pid) {
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
}

/* "<pid>-<id>.btnrecovery" (tmp = 0) oder deren Tempdatei
 * "<pid>-<id>.btnrecovery.XXXXXX" (tmp = 1). Rueckgabe: pid, -1 = fremd. */
static long parse_name(const char *name, int *tmp) {
    char *end;
    long pid = strtol(name, &end, 10);
    if (end == name || pid <= 0 || *end != '-') {
        return -1;
    }
    const char *id = end + 1;
    strtoul(id, &end, 10);
    if (end == id || strncmp(end, RECOVERY_SUFFIX, strlen(RECOVERY_SUFFIX)) != 0) {
        return -1;
    }
    end += strlen(RECOVERY_SUFFIX);
    *tmp = *end != '\0';
    if (*tmp && (end[0] != '.' || strlen(end) != 7)) {
        return -1;
    }
    return pid;
}

/* Nach pid, dann id - in der Reihenfolge, in der die Tabs angelegt wurden. */
static int cmp_recovery_files(const void *pa, const void *pb) {
    const char *a = strrchr(*(char *const *)pa, '/') + 1;
    const char *b = strrchr(*(char *const *)pb, '/') + 1;
    char *ea, *eb;
    long pid_a = strtol(a, &ea, 10), pid_b = strtol(b, &eb, 10);
    if (pid_a != pid_b) {
        return pid_a < pid_b ? -1 : 1;
    }
    unsigned long id_a = strtoul(ea + 1, NULL, 10), id_b = strtoul(eb + 1, NULL, 10);
    return id_a < id_b ? -1 : id_a > id_b;
}

size_t btn_recovery_find_orphans(const char *dir, long self_pid, char ***out_files) {
    *out_files = NULL;
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    size_t count = 0, cap = 0;
    char **files = NULL;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int tmp = 0;
        long pid = parse_name(e->d_name, &tmp);
        if (pid < 0 || pid == self_pid || pid_alive(pid)) {
            continue;
        }
        char *full = malloc(strlen(dir) + strlen(e->d_name) + 2);
        if (!full) {
            continue;
        }
        sprintf(full, "%s/%s", dir, e->d_name);
        if (tmp) {
            unlink(full); /* halb geschriebene Tempdatei eines toten Prozesses */
            free(full);
            continue;
        }
        if (count == cap) {
            cap = cap ? cap * 2 : 8;
            char **grown = realloc(files, cap * sizeof(char *));
            if (!grown) {
                free(full);
                break;
            }
            files = grown;
        }
        files[count++] = full;
    }
    closedir(d);
    if (count > 1) {
        qsort(files, count, sizeof(char *), cmp_recovery_files);
    }
    *out_files = files;
    return count;
}

void btn_recovery_free_list(char **files, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(files[i]);
    }
    free(files);
}
