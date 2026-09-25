/* Schutz der Arbeit im Zusammenspiel von main.c (Funktionen verbatim, Stubs
 * nur fuer Fenster/Dialoge), mit echten Dateien in einem Temp-Ordner:
 * Aenderung von aussen (still neu laden, fragen, behalten), Konflikt beim
 * Sichern, geloeschte Datei, Wiederherstellungsdateien (wann geschrieben,
 * wann entfernt) und ein nachgestellter Absturz mit Wiederherstellung. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "editor.h"
#include "eol.h"
#include "gapbuffer.h"
#include "strings.h"
#include "textinput.h"
#include "filestamp.h"
#include "recovery.h"
#include "doc_extracted.h"
#include "drag_type_extracted.h"

static long fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 40) { printf("FAIL: " __VA_ARGS__); printf("\n"); } fails++; } } while (0)

/* ---- Zustand und Stubs fuer das, was die Funktionen aus main.c anfassen ---- */
#define MAX_TABS 20
static Document g_docs[MAX_TABS];
static int g_doc_count = 0, g_active_doc = 0;
static char *g_recovery_dir = NULL;
static char *g_recovery_run = NULL;
static unsigned g_next_recovery_id = 1;
static Editor *g_print_editor = NULL;
static BtnDrag g_drag = BTN_DRAG_NONE;
static BtnMarkedText g_marked;
static int g_checking_disk = 0;

static int g_choice_answers[8], g_choice_count = 0, g_choice_next = 0, g_last_escape = -1;
static char g_last_choice_title[512];
static int g_switches, g_error_alerts, g_commits;

const char *btn_tr(BtnStringId id) {
    switch (id) {
        case BTN_STR_FILE_TOO_LARGE_INFO_FMT:
        case BTN_STR_RECOVERY_INFO_FMT:
            return "%d";
        case BTN_STR_FILE_CHANGED_TITLE_FMT:
            return "changed: %s";
        case BTN_STR_SAVE_CONFLICT_TITLE_FMT:
            return "conflict: %s";
        case BTN_STR_RECOVERY_TITLE:
            return "recovery";
        case BTN_STR_UNTITLED:
            return "Untitled";
        default:
            return "%s";
    }
}
static int btn_show_choice_alert(const char *title, const char *info, const char *a, const char *b, int esc) {
    (void)info; (void)a; (void)b;
    g_last_escape = esc;
    snprintf(g_last_choice_title, sizeof g_last_choice_title, "%s", title);
    g_choice_count++;
    return g_choice_answers[g_choice_next++ % 8];
}
static int btn_show_binary_file_warning(const char *n) { (void)n; return 1; }
static void btn_show_error_alert(const char *t, const char *i) { (void)t; (void)i; g_error_alerts++; }
static char *btn_show_save_panel(const char *p) { (void)p; return NULL; }
static char *btn_dup_cstring(const char *s) { return strdup(s); }
static void btn_set_window_title(const char *t) { (void)t; }
static void add_recent_file(const char *p) { (void)p; }
static void switch_to_tab(int idx) { g_switches++; g_active_doc = idx; }
static void close_find_bar(void) {}
static void sync_window_state(void) {}
static void clamp_scroll(void) {}
static void sync_scroll_to_cursor(void) {}
static void commit_marked(void) { g_commits++; }
static void btn_app_request_redraw(void) {}
static Document *active_doc(void);

/* Ein anderes Programm schreibt die Datei gerade fertig, waehrend BTNEdit
 * sie liest: nach dem ersten fread() haengt es noch etwas an. */
static const char *g_race_path = NULL;
static size_t race_fread(void *buf, size_t size, size_t n, FILE *f) {
    size_t r = fread(buf, size, n, f);
    if (g_race_path) {
        FILE *w = fopen(g_race_path, "ab");
        fputs("-LATE", w);
        fclose(w);
        g_race_path = NULL;
    }
    return r;
}
#define fread race_fread
#include "protect_extracted.h"
#undef fread

/* ---- Hilfen ---- */
static char g_tmp[256];

static void answers(int a, int b) {
    g_choice_answers[0] = a;
    g_choice_answers[1] = b;
    g_choice_next = 0;
    g_choice_count = 0;
}

static void write_raw(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

/* Anderes Programm sichert: neue Datei + rename() (neue Inode). */
static void external_replace(const char *path, const char *text) {
    char tmp[400];
    snprintf(tmp, sizeof tmp, "%s.other", path);
    write_raw(tmp, text);
    rename(tmp, path);
}

/* Anderes Programm schreibt an Ort und Stelle, gleiche Groesse - nur die
 * mtime verraet es (hier sicher um 1 s verschoben). */
static void external_inplace(const char *path, const char *text) {
    struct stat st;
    stat(path, &st);
    write_raw(path, text);
    struct timespec ts[2] = { { 0, UTIME_OMIT }, { st.st_mtime + 1, 0 } };
    utimensat(AT_FDCWD, path, ts, 0);
}

static int file_is(const char *path, const char *text) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    char buf[256];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return n == strlen(text) && memcmp(buf, text, n) == 0;
}

static int doc_text_is(Document *d, const char *text) {
    size_t len;
    char *t = editor_copy_all(&d->editor, &len);
    int ok = len == strlen(text) && memcmp(t, text, len) == 0;
    free(t);
    return ok;
}

static void type_text(Document *d, const char *text) {
    editor_set_cursor(&d->editor, editor_length(&d->editor), 0);
    editor_insert_text(&d->editor, text, strlen(text));
}

static void reset_docs(void) {
    for (int i = 0; i < g_doc_count; i++) {
        editor_free(&g_docs[i].editor);
        free(g_docs[i].path);
        free(g_docs[i].recovery_file);
    }
    memset(g_docs, 0, sizeof g_docs);
    g_doc_count = 0;
    g_active_doc = 0;
    add_tab();
}

static int dir_entries(const char *dir) {
    char cmd[512];
    snprintf(cmd, sizeof cmd, "ls -A '%s' 2>/dev/null | wc -l", dir);
    FILE *p = popen(cmd, "r");
    int n = -1;
    if (fscanf(p, "%d", &n) != 1) {
        n = -1;
    }
    pclose(p);
    return n;
}

/* ---- Aenderungen von aussen ---- */
static void test_external_changes(void) {
    char a[300], b[300];
    snprintf(a, sizeof a, "%s/a.txt", g_tmp);
    snprintf(b, sizeof b, "%s/b.txt", g_tmp);
    reset_docs();
    write_raw(a, "one\r\ntwo\r\n");
    open_file_path(&g_docs[0], a);
    Document *d = &g_docs[0];
    CHECK(d->disk.valid && !doc_is_dirty(d) && d->eol == BTN_EOL_CRLF, "loaded, stamp taken");
    answers(1, 1);
    CHECK(check_doc_on_disk(0, 1) == 0 && g_choice_count == 0, "unchanged file: nothing happens");

    /* ohne eigene Aenderungen: still neu laden, Cursor bleibt in Reichweite */
    editor_set_cursor(&d->editor, 6, 0);
    external_replace(a, "ONE\r\nTWO\r\nthree\r\n");
    CHECK(check_doc_on_disk(0, 1) == 1 && g_choice_count == 0, "changed, no edits: reloaded without asking");
    CHECK(doc_text_is(d, "ONE\nTWO\nthree\n") && !doc_is_dirty(d) && d->editor.cursor == 6, "new text, clean, cursor kept");
    external_inplace(a, "ONE\r\nTWO\r\nTHREE\r\n");
    CHECK(check_doc_on_disk(0, 0) == 1 && doc_text_is(d, "ONE\nTWO\nTHREE\n"), "in-place rewrite of the same size detected (timer)");
    editor_set_cursor(&d->editor, editor_length(&d->editor), 0);
    external_replace(a, "x\n");
    check_doc_on_disk(0, 0);
    CHECK(d->editor.cursor == 2 && d->editor.anchor == 2, "cursor clamped to the shorter text");
    external_replace(a, "\xC3\xA4\xC3\xB6\n");
    editor_set_cursor(&d->editor, 1, 0);
    external_replace(a, "\xC3\xA4\xC3\xB6\xC3\xBC\n");
    check_doc_on_disk(0, 0);
    CHECK(d->editor.cursor == 0, "cursor inside a character moves to its start (%zu)", d->editor.cursor);

    /* mit eigenen Aenderungen: der Timer fragt nicht */
    type_text(d, "mine");
    external_replace(a, "theirs\n");
    CHECK(check_doc_on_disk(0, 0) == 0 && g_choice_count == 0 && doc_text_is(d, "\xC3\xA4\xC3\xB6\xC3\xBC\nmine"),
          "edits + change, timer: no dialog, text kept");
    /* Aktivierung fragt: behalten */
    answers(1, 1);
    CHECK(check_doc_on_disk(0, 1) == 1 && g_choice_count == 1 && strcmp(g_last_choice_title, "changed: a.txt") == 0,
          "edits + change: asks (%s)", g_last_choice_title);
    CHECK(g_last_escape == 0, "'changed on disk' dialog: Escape does not reload");
    CHECK(doc_text_is(d, "\xC3\xA4\xC3\xB6\xC3\xBC\nmine") && doc_is_dirty(d), "keep: own text stays, still unsaved");
    CHECK(check_doc_on_disk(0, 1) == 0 && g_choice_count == 1, "after keep: not asked again");
    CHECK(perform_save_doc(d, 0) == 1 && g_choice_count == 1 && file_is(a, "\xC3\xA4\xC3\xB6\xC3\xBC\nmine"),
          "after keep: save overwrites without a conflict dialog");
    /* Aktivierung fragt: neu laden */
    type_text(d, "!");
    external_replace(a, "fresh\r\n");
    answers(0, 0);
    check_doc_on_disk(0, 1);
    CHECK(g_choice_count == 1 && doc_text_is(d, "fresh\n") && !doc_is_dirty(d), "reload: disk version, clean");

    /* Konflikt beim Sichern */
    type_text(d, "ours");
    external_inplace(a, "THEIR\r\n");
    answers(0, 0);
    CHECK(perform_save_doc(d, 0) == 0 && g_choice_count == 1 && strcmp(g_last_choice_title, "conflict: a.txt") == 0,
          "save conflict: asks, cancel returns 0");
    CHECK(g_last_escape == 1, "save conflict: Escape cancels");
    CHECK(file_is(a, "THEIR\r\n") && doc_is_dirty(d), "cancel: other program's version untouched, doc unsaved");
    answers(1, 1);
    CHECK(perform_save_doc(d, 0) == 1 && file_is(a, "fresh\r\nours") && !doc_is_dirty(d), "save anyway: our text written");
    answers(1, 1);
    type_text(d, "2");
    CHECK(perform_save_doc(d, 0) == 1 && g_choice_count == 0, "next save without external change: no dialog");

    /* geloescht */
    unlink(a);
    CHECK(check_doc_on_disk(0, 1) == 1 && d->missing_on_disk && doc_is_dirty(d) && !doc_has_edits(d) && g_choice_count == 0,
          "deleted: unsaved, no dialog");
    CHECK(check_doc_on_disk(0, 1) == 0, "still deleted: nothing new");
    CHECK(perform_save_doc(d, 0) == 1 && file_is(a, "fresh\r\nours2") && !doc_is_dirty(d) && g_choice_count == 0,
          "save recreates the deleted file without a conflict dialog");
    unlink(a);
    check_doc_on_disk(0, 0);
    external_replace(a, "back\n");
    CHECK(check_doc_on_disk(0, 0) == 1 && doc_text_is(d, "back\n") && !doc_is_dirty(d), "file reappears: reloaded, clean");
    /* ersetzt durch einen Ordner: gilt als verschwunden */
    unlink(a);
    mkdir(a, 0700);
    check_doc_on_disk(0, 0);
    CHECK(d->missing_on_disk && doc_text_is(d, "back\n"), "replaced by a directory: missing, text kept");
    rmdir(a);

    /* Datei wird waehrend des Lesens fertig geschrieben: erkannt */
    external_replace(a, "part1");
    g_race_path = a;
    reload_doc(d);
    CHECK(doc_text_is(d, "part1") && !doc_is_dirty(d), "reload read the first part");
    CHECK(check_doc_on_disk(0, 0) == 1 && doc_text_is(d, "part1-LATE"), "rest written during the read: noticed next time");
    reset_docs();
    d = &g_docs[0];
    g_race_path = a;
    open_file_path(d, a);
    CHECK(check_doc_on_disk(0, 0) == 1 && doc_text_is(d, "part1-LATE-LATE"), "same for opening");

    /* Neu laden scheitert (Datei jetzt zu gross): alter Stand bleibt, Sichern fragt */
    type_text(d, "");
    external_replace(a, "x");
    if (truncate(a, (off_t)BTN_MAX_FILE_MB * 1024 * 1024 + 1) == 0) {
        check_doc_on_disk(0, 0);
        CHECK(d->missing_on_disk && doc_text_is(d, "part1-LATE-LATE"), "too large on reload: text kept, unsaved");
        answers(0, 0);
        CHECK(perform_save_doc(d, 0) == 0 && g_choice_count == 1, "too large on reload: save asks before overwriting");
        struct stat big;
        CHECK(stat(a, &big) == 0 && big.st_size > 1000, "cancel: the big file is untouched");
        external_replace(a, "small again");
        check_doc_on_disk(0, 0);
        CHECK(doc_text_is(d, "small again") && !doc_is_dirty(d), "readable again: reloaded on the next check");
    }

    /* Hintergrund-Tab wird vor der Frage sichtbar */
    write_raw(b, "bee\n");
    int idx = add_tab();
    open_file_path(&g_docs[idx], b);
    g_active_doc = 0;
    type_text(&g_docs[idx], "!");
    external_replace(b, "BEE\n");
    g_switches = 0;
    answers(1, 1);
    check_doc_on_disk(idx, 1);
    CHECK(g_switches == 1 && g_active_doc == idx && g_choice_count == 1, "background tab shown before asking");

    /* Aktivierung: alle Tabs, danach zurueck zum urspruenglichen Tab */
    reset_docs();
    write_raw(a, "a\n");
    write_raw(b, "b\n");
    open_file_path(&g_docs[0], a);
    idx = add_tab();
    open_file_path(&g_docs[idx], b);
    g_active_doc = 0;
    type_text(&g_docs[1], "edit");
    external_replace(a, "A\n");
    external_replace(b, "B\n");
    answers(0, 0);
    g_commits = 0;
    Editor printing;
    g_print_editor = &printing;
    on_activate();
    CHECK(g_choice_count == 0 && doc_text_is(&g_docs[0], "a\n"), "activation while printing: nothing happens");
    g_print_editor = NULL;
    on_activate();
    CHECK(doc_text_is(&g_docs[0], "A\n") && doc_text_is(&g_docs[1], "B\n") && g_choice_count == 1 && g_active_doc == 0,
          "activation: clean tab reloaded, edited tab asked (reload), active tab restored");
    CHECK(g_commits == 1, "activation commits a running composition first");

    /* Timer: nie fragen; den aktiven Tab waehrend Markieren/Eingabe in Ruhe lassen */
    type_text(&g_docs[1], "x");
    external_replace(b, "B2\n");
    external_replace(a, "A2\n");
    g_drag = BTN_DRAG_TEXT;
    answers(1, 1);
    on_timer();
    CHECK(g_choice_count == 0 && doc_text_is(&g_docs[0], "A\n") && doc_text_is(&g_docs[1], "B\nx"),
          "timer: active tab skipped while dragging, edited tab not asked");
    g_drag = BTN_DRAG_NONE;
    on_timer();
    CHECK(doc_text_is(&g_docs[0], "A2\n") && g_choice_count == 0, "timer: clean active tab reloaded afterwards");
    external_replace(a, "A3\n");
    on_timer();
    CHECK(doc_text_is(&g_docs[0], "A2\n"), "timer: a tab is checked at most every few seconds");
    g_docs[0].disk_check_time -= 100;
    on_timer();
    CHECK(doc_text_is(&g_docs[0], "A3\n"), "timer: checked again after the interval");
}

/* ---- Wiederherstellungsdateien ---- */
static void test_autosave(void) {
    char a[300];
    snprintf(a, sizeof a, "%s/r.txt", g_tmp);
    free(g_recovery_dir);
    g_recovery_dir = malloc(400);
    snprintf(g_recovery_dir, 400, "%s/Recovery", g_tmp);
    g_recovery_run = btn_recovery_begin_run(g_recovery_dir);
    CHECK(g_recovery_run != NULL, "recovery run started");
    reset_docs();
    write_raw(a, "saved\r\n");
    open_file_path(&g_docs[0], a);
    Document *d = &g_docs[0];
    autosave_recovery(1000);
    CHECK(d->recovery_file == NULL && dir_entries(g_recovery_dir) == 1, "clean doc: no recovery file (only the run's lock)");

    type_text(d, "draft");
    autosave_recovery(1000);
    BtnRecovered r;
    CHECK(d->recovery_file && btn_recovery_read(d->recovery_file, &r), "unsaved doc: recovery file written at once");
    if (d->recovery_file) {
        char want[64];
        snprintf(want, sizeof want, "/%s-%u.btnrecovery", g_recovery_run, d->recovery_id);
        CHECK(strstr(d->recovery_file, want) != NULL, "named after pid and doc id (%s)", d->recovery_file);
    }
    CHECK(r.len == 11 && memcmp(r.text, "saved\ndraft", 11) == 0 && r.path && strcmp(r.path, a) == 0 &&
              r.eol == BTN_EOL_CRLF && !r.raw && btn_file_stamp_equal(&r.disk, &d->disk),
          "recovery holds text, path, line endings and the file stamp");
    btn_recovery_free(&r);

    type_text(d, "+");
    autosave_recovery(1002);
    btn_recovery_read(d->recovery_file, &r);
    CHECK(r.len == 11, "changed again after 2 s: not yet rewritten");
    btn_recovery_free(&r);
    autosave_recovery(1000 + BTN_RECOVERY_INTERVAL);
    btn_recovery_read(d->recovery_file, &r);
    CHECK(r.len == 12, "after the interval: rewritten");
    btn_recovery_free(&r);
    write_raw(d->recovery_file, "tampered");
    autosave_recovery(2000);
    CHECK(!btn_recovery_read(d->recovery_file, &r), "no change since the last write: not rewritten");
    type_text(d, "~");
    autosave_recovery(500); /* frueher als die letzte Sicherung (1005) */
    CHECK(btn_recovery_read(d->recovery_file, &r) && r.len == 13, "clock went backwards: still written");
    btn_recovery_free(&r);

    /* Nur das Zeilenende umgestellt zaehlt auch */
    d->eol = BTN_EOL_LF;
    autosave_recovery(3000);
    CHECK(btn_recovery_read(d->recovery_file, &r) && r.eol == BTN_EOL_LF, "line-ending change rewrites");
    btn_recovery_free(&r);
    d->eol = BTN_EOL_CRLF;

    /* Wieder sauber ohne Sichern (z.B. "Nicht sichern"): Datei weg */
    char *file = strdup(d->recovery_file);
    mark_doc_saved(d);
    autosave_recovery(4000);
    CHECK(d->recovery_file == NULL && access(file, F_OK) != 0, "clean again: recovery file removed");
    type_text(d, "x");
    autosave_recovery(5000);
    CHECK(d->recovery_file && strcmp(d->recovery_file, file) == 0, "unsaved again: same file name");
    CHECK(perform_save_doc(d, 0) == 1 && d->recovery_file == NULL && access(file, F_OK) != 0,
          "saving removes the recovery file immediately");
    free(file);

    /* Geloeschte Datei ohne eigene Aenderungen wird gesichert */
    unlink(a);
    check_doc_on_disk(0, 0);
    autosave_recovery(6000);
    CHECK(d->recovery_file != NULL, "deleted file: its text is kept in a recovery file");
    type_text(d, "y");
    external_replace(a, "other\n");
    answers(0, 0);
    check_doc_on_disk(0, 1);
    CHECK(d->recovery_file == NULL && doc_text_is(d, "other\n"), "reload after a change removes the recovery file");

    /* Grosse Dokumente seltener */
    reset_docs();
    d = &g_docs[0];
    size_t big = (size_t)BTN_RECOVERY_BYTES_PER_SECOND * 2 + 10;
    char *text = malloc(big);
    memset(text, 'x', big);
    editor_insert_text(&d->editor, text, big);
    free(text);
    autosave_recovery(10000);
    type_text(d, "!");
    autosave_recovery(10000 + BTN_RECOVERY_INTERVAL + 1);
    CHECK(d->recovery_seq != d->editor.edit_seq, "40 MB: not rewritten after the normal interval");
    autosave_recovery(10000 + BTN_RECOVERY_INTERVAL + 2);
    CHECK(d->recovery_seq == d->editor.edit_seq, "40 MB: rewritten two seconds later");

    /* Ohne Ordner (kein HOME): nichts */
    char *saved_dir = g_recovery_dir;
    g_recovery_dir = NULL;
    reset_docs();
    type_text(&g_docs[0], "z");
    autosave_recovery(20000);
    CHECK(g_docs[0].recovery_file == NULL, "no recovery dir: nothing written");
    g_recovery_dir = saved_dir;
    reset_docs();
}

static long dead_pid(void) {
    pid_t p = fork();
    if (p == 0) {
        _exit(0);
    }
    waitpid(p, NULL, 0);
    return (long)p;
}

/* Die Dateien dieses Prozesses einem toten Prozess zuordnen - wie nach einem
 * Absturz. */
static int g_crashes = 0;
static void simulate_crash(void) {
    g_crashes++;
    for (int i = 0; i < g_doc_count; i++) {
        Document *d = &g_docs[i];
        if (!d->recovery_file) {
            continue;
        }
        char to[600];
        snprintf(to, sizeof to, "%s/999-%d-%u.btnrecovery", g_recovery_dir, g_crashes, d->recovery_id);
        rename(d->recovery_file, to);
        free(d->recovery_file);
        d->recovery_file = NULL;
    }
    reset_docs();
}

static void test_restore(void) {
    char a[300], dmg[600];
    snprintf(a, sizeof a, "%s/crash.txt", g_tmp);
    write_raw(a, "on disk\n");
    /* Lauf 1: benanntes und unbenanntes Dokument, beide ungesichert */
    reset_docs();
    open_file_path(&g_docs[0], a);
    type_text(&g_docs[0], "lost?");
    int idx = add_tab();
    type_text(&g_docs[idx], "untitled \0text");
    editor_insert_text(&g_docs[idx].editor, "\0!", 2);
    autosave_recovery(100);
    simulate_crash();
    snprintf(dmg, sizeof dmg, "%s/%ld-0-99.btnrecovery", g_recovery_dir, dead_pid());
    write_raw(dmg, "garbage");

    /* Lauf 2: Wiederherstellen */
    answers(1, 1);
    restore_recovered_documents();
    CHECK(g_choice_count == 1 && g_last_escape == 0, "asked once, Escape does not discard");
    CHECK(g_doc_count == 2, "two tabs restored (%d)", g_doc_count);
    CHECK(g_docs[0].path && strcmp(g_docs[0].path, a) == 0 && doc_text_is(&g_docs[0], "on disk\nlost?") &&
              doc_is_dirty(&g_docs[0]),
          "titled doc: path, text, unsaved");
    size_t len;
    char *t = editor_copy_all(&g_docs[1].editor, &len);
    CHECK(g_docs[1].path == NULL && len == 11 && memcmp(t, "untitled \0!", 11) == 0 && doc_is_dirty(&g_docs[1]),
          "untitled doc with NUL byte restored in order");
    free(t);
    editor_undo(&g_docs[0].editor);
    CHECK(doc_is_dirty(&g_docs[0]), "restored doc stays unsaved even after undo");
    CHECK(g_docs[0].recovery_file && g_docs[1].recovery_file, "restored tabs have their own recovery files at once");
    char **files;
    size_t n = btn_recovery_find_orphans(g_recovery_dir, g_recovery_run, &files);
    btn_recovery_free_list(files, n);
    CHECK(n == 0, "old files gone, damaged one not offered again (%zu)", n);
    char damaged[700];
    snprintf(damaged, sizeof damaged, "%s.damaged", dmg);
    CHECK(access(damaged, F_OK) == 0, "damaged file kept as *.damaged");
    CHECK(check_doc_on_disk(0, 1) == 0 && g_choice_count == 1, "restored doc: no 'changed on disk' dialog");
    answers(1, 1);
    CHECK(perform_save_doc(&g_docs[0], 0) == 1 && g_choice_count == 0 && file_is(a, "on disk\nlost?"),
          "saving the restored doc: no conflict dialog");

    /* Beim Start per "Oeffnen mit" schon geoeffnet: derselbe Tab */
    reset_docs();
    open_file_path(&g_docs[0], a);
    type_text(&g_docs[0], "#2");
    autosave_recovery(200);
    simulate_crash();
    open_file_path(&g_docs[0], a);
    answers(1, 1);
    restore_recovered_documents();
    CHECK(g_doc_count == 1 && doc_text_is(&g_docs[0], "on disk\nlost?#2") && doc_is_dirty(&g_docs[0]),
          "file already open at launch: recovered text goes into its tab");

    /* Kein Tab mehr frei: die Datei bleibt fuer den naechsten Start */
    type_text(&g_docs[0], "#full");
    autosave_recovery(monotonic_seconds() + 100); /* die Wiederherstellung oben schrieb mit echter Zeit */
    simulate_crash();
    type_text(&g_docs[0], "busy");
    while (g_doc_count < MAX_TABS) {
        type_text(&g_docs[add_tab()], "busy");
    }
    answers(1, 1);
    restore_recovered_documents();
    n = btn_recovery_find_orphans(g_recovery_dir, g_recovery_run, &files);
    btn_recovery_free_list(files, n);
    CHECK(g_doc_count == MAX_TABS && n == 1, "all tabs in use: not restored, file kept (%zu)", n);
    reset_docs();
    restore_recovered_documents();
    CHECK(doc_text_is(&g_docs[0], "on disk\nlost?#2#full"), "restored on the next start");
    for (int i = 0; i < g_doc_count; i++) {
        discard_recovery(&g_docs[i]);
    }
    reset_docs();
    open_file_path(&g_docs[0], a);

    /* Datei nach dem Absturz geaendert (git pull): Sichern fragt */
    type_text(&g_docs[0], "#pull");
    autosave_recovery(monotonic_seconds() + 200);
    simulate_crash();
    external_replace(a, "pulled\n");
    answers(1, 1);
    restore_recovered_documents();
    answers(0, 0);
    CHECK(perform_save_doc(&g_docs[0], 0) == 0 && g_choice_count == 1 && file_is(a, "pulled\n"),
          "file changed after the crash: saving the restored doc asks first");
    for (int i = 0; i < g_doc_count; i++) {
        discard_recovery(&g_docs[i]);
    }

    /* Zwei Sicherungen derselben Datei: zwei Tabs, keine geht verloren */
    reset_docs();
    open_file_path(&g_docs[0], a);
    type_text(&g_docs[0], "#one");
    idx = add_tab();
    g_docs[idx].path = strdup(a); /* zweiter Tab mit derselben Datei (z.B. Sichern unter) */
    type_text(&g_docs[idx], "#two");
    autosave_recovery(monotonic_seconds() + 300);
    simulate_crash();
    answers(1, 1);
    restore_recovered_documents();
    CHECK(g_doc_count == 2 && doc_text_is(&g_docs[0], "pulled\n#one") && doc_text_is(&g_docs[1], "#two"),
          "two recoveries of one file: both restored in separate tabs");
    for (int i = 0; i < g_doc_count; i++) {
        discard_recovery(&g_docs[i]);
    }

    /* Neue Sicherung laesst sich nicht schreiben (Platte voll): alte bleibt */
    reset_docs();
    type_text(&g_docs[0], "precious text");
    autosave_recovery(monotonic_seconds() + 400);
    simulate_crash();
    signal(SIGXFSZ, SIG_IGN);
    struct rlimit old_limit, tiny;
    getrlimit(RLIMIT_FSIZE, &old_limit);
    tiny = old_limit;
    tiny.rlim_cur = 8;
    setrlimit(RLIMIT_FSIZE, &tiny);
    answers(1, 1);
    restore_recovered_documents();
    setrlimit(RLIMIT_FSIZE, &old_limit);
    n = btn_recovery_find_orphans(g_recovery_dir, g_recovery_run, &files);
    btn_recovery_free_list(files, n);
    CHECK(doc_text_is(&g_docs[0], "precious text") && g_docs[0].recovery_file == NULL && n == 1,
          "new recovery file not written: the old one is kept (%zu)", n);
    reset_docs();
    answers(1, 1);
    restore_recovered_documents();
    CHECK(doc_text_is(&g_docs[0], "precious text") && g_docs[0].recovery_file != NULL, "restored on the next start");
    for (int i = 0; i < g_doc_count; i++) {
        discard_recovery(&g_docs[i]);
    }
    reset_docs();
    open_file_path(&g_docs[0], a);

    /* Verwerfen */
    type_text(&g_docs[0], "#3");
    autosave_recovery(300);
    simulate_crash();
    answers(0, 0);
    restore_recovered_documents();
    n = btn_recovery_find_orphans(g_recovery_dir, g_recovery_run, &files);
    btn_recovery_free_list(files, n);
    CHECK(g_choice_count == 1 && n == 0 && g_doc_count == 1 && doc_is_blank(&g_docs[0]), "discard: files removed, nothing opened");
    answers(1, 1);
    restore_recovered_documents();
    CHECK(g_choice_count == 0, "nothing left: no dialog");
    reset_docs();
}

int main(void) {
    snprintf(g_tmp, sizeof g_tmp, "/tmp/btn_protect_XXXXXX");
    if (!mkdtemp(g_tmp)) {
        return 1;
    }
    test_external_changes();
    test_autosave();
    test_restore();
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_tmp);
    if (system(cmd) != 0) {
        printf("cleanup failed\n");
    }
    for (int i = 0; i < g_doc_count; i++) {
        editor_free(&g_docs[i].editor);
        free(g_docs[i].path);
        free(g_docs[i].recovery_file);
    }
    free(g_recovery_dir);
    free(g_recovery_run);
    printf("%s: %ld checks, %ld failures\n", fails ? "FAILED" : "ALL PASSED", checks, fails);
    return fails != 0;
}
