#include "filestamp.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

void btn_file_stamp_from_stat(const struct stat *st, BtnFileStamp *out) {
    memset(out, 0, sizeof(*out));
    if (!S_ISREG(st->st_mode)) {
        return;
    }
    out->valid = 1;
    out->dev = st->st_dev;
    out->ino = st->st_ino;
    out->size = st->st_size;
#ifdef __APPLE__
    out->mtime_ns = (long long)st->st_mtimespec.tv_sec * 1000000000LL + st->st_mtimespec.tv_nsec;
    out->ctime_ns = (long long)st->st_ctimespec.tv_sec * 1000000000LL + st->st_ctimespec.tv_nsec;
#else
    out->mtime_ns = (long long)st->st_mtim.tv_sec * 1000000000LL + st->st_mtim.tv_nsec;
    out->ctime_ns = (long long)st->st_ctim.tv_sec * 1000000000LL + st->st_ctim.tv_nsec;
#endif
}

int btn_file_stamp(const char *path, BtnFileStamp *out) {
    memset(out, 0, sizeof(*out));
    struct stat st;
    if (!path || stat(path, &st) != 0) {
        return 0;
    }
    btn_file_stamp_from_stat(&st, out);
    return out->valid;
}

int btn_file_stamp_equal(const BtnFileStamp *a, const BtnFileStamp *b) {
    if (!a->valid || !b->valid) {
        return a->valid == b->valid;
    }
    return a->dev == b->dev && a->ino == b->ino && a->size == b->size && a->mtime_ns == b->mtime_ns &&
           a->ctime_ns == b->ctime_ns;
}

uint64_t btn_hash_bytes(uint64_t h, const void *data, size_t len) {
    const unsigned char *p = data;
    for (size_t i = 0; i < len; i++) {
        h = (h ^ p[i]) * 1099511628211ULL;
    }
    return h;
}

int btn_file_hash(const char *path, uint64_t *out) {
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) {
        return 0;
    }
    uint64_t h = BTN_HASH_SEED;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        h = btn_hash_bytes(h, buf, n);
    }
    int ok = !ferror(f);
    fclose(f);
    if (ok) {
        *out = h;
    }
    return ok;
}
