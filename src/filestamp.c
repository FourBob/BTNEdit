#include "filestamp.h"

#include <string.h>
#include <sys/stat.h>

int btn_file_stamp(const char *path, BtnFileStamp *out) {
    memset(out, 0, sizeof(*out));
    struct stat st;
    if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    out->valid = 1;
    out->dev = st.st_dev;
    out->ino = st.st_ino;
    out->size = st.st_size;
#ifdef __APPLE__
    out->mtime_ns = (long long)st.st_mtimespec.tv_sec * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
    out->mtime_ns = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#endif
    return 1;
}

int btn_file_stamp_equal(const BtnFileStamp *a, const BtnFileStamp *b) {
    if (!a->valid || !b->valid) {
        return a->valid == b->valid;
    }
    return a->dev == b->dev && a->ino == b->ino && a->size == b->size && a->mtime_ns == b->mtime_ns;
}
