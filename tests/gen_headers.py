#!/usr/bin/env python3
"""Erzeugt die *_extracted.h-Header fuer die Tests aus dem AKTUELLEN Quelltext.

main.c, render.c und shim.m brauchen macOS-Frameworks und lassen sich nicht
ohne Weiteres in einen Test linken. Ihre reinen C-Teile (Regex-Suche,
Sichern, Layout, ...) werden deshalb verbatim herausgeschnitten und von den
Tests eingebunden. Weil das bei jedem Testlauf neu passiert, testet ein Test
immer den Code, der im Repo steht - keine veralteten Kopien.

Aufruf: gen_headers.py <src-Verzeichnis> <Ausgabe-Verzeichnis>

Spezifikationen pro Header:
  name        Funktionsdefinition, die in Spalte 0 beginnt, bis zur ersten
              Zeile, die nur "}" enthaelt
  #NAME       einzeiliges #define
  struct:NAME "static struct { ... } NAME;"-Block
  typedef:NAME "typedef struct/enum { ... } NAME;"-Block
"""
import os
import re
import sys

HEADERS = {
    "regex_extracted.h": ("main.c", [
        "#BTN_MAX_REGEX_GROUPS", "#BTN_MAX_EXPANDED_REPLACEMENT_LEN",
        "regexec_flags_for", "regex_search_from", "regex_next_scan",
        "regex_escape_literal", "regex_translate_tab_escapes", "compile_search_regex",
        "find_match", "collect_all_matches", "collect_all_matches_unbounded",
        "replacement_has_backreferences", "replacement_needs_expansion", "expand_replacement"]),
    "save_extracted.h": ("main.c", ["write_stream_checked", "write_file_atomic", "write_file_contents"]),
    "close_extracted.h": ("main.c", ["should_close"]),
    "fileio_extracted.h": ("main.c", ["#BTN_MAX_FILE_MB", "#BTN_MAX_FILE_SIZE",
                                      "read_file_contents", "load_recent_files"]),
    "expensive_extracted.h": ("main.c", ["#BTN_LIVE_REGEX_MAX_COPIES",
                                         "regex_too_expensive_for_live_search"]),
    "replsel_extracted.h": ("main.c", ["replace_selection"]),
    "doc_extracted.h": ("main.c", ["typedef:Document", "doc_is_dirty", "mark_doc_saved"]),
    "eol_glue_extracted.h": ("main.c", [
        "typedef:BtnReadResult", "#BTN_MAX_FILE_MB", "#BTN_MAX_FILE_SIZE", "basename_of", "looks_binary",
        "write_stream_checked", "write_file_atomic", "write_file_contents", "read_file_contents",
        "show_file_error", "set_doc_line_ending", "open_file_path", "perform_save_doc"]),
    "render_pure_extracted.h": ("render.c", [
        "rows_push", "layout_build", "struct:g_layout", "btn_layout_get",
        "btn_layout_row_for_offset", "btn_row_offset_for_column", "first_row_of_line",
        "line_bounds_from_rows", "row_of_line_start", "struct:g_cstate", "cstate_reserve",
        "comment_state_before_line"]),
    "render_decode_extracted.h": ("render.c", ["decode_row_for_display"]),
    "render_helpers_extracted.h": ("render.c", ["utf8_safe_cut", "utf8_prefix_bytes"]),
    "cap_extracted.h": ("render.c", ["btn_visible_row_capacity"]),
    "fontsize_extracted.h": ("render.c", ["btn_render_set_font_size"]),
    "footer_fmt_extracted.h": ("render.c", ["btn_footer_format_ok"]),
}


def extract(src, spec, path):
    if spec.startswith("#"):
        m = re.search(r"^#define " + re.escape(spec[1:]) + r"\b.*$", src, re.M)
        if not m:
            sys.exit(f"{path}: #define {spec[1:]} nicht gefunden")
        return m.group(0)
    if spec.startswith("typedef:"):
        name = spec[len("typedef:"):]
        end_marker = "} " + name + ";\n"
        end = src.find(end_marker)
        start = src.rfind("\ntypedef ", 0, end) + 1 if end >= 0 else 0
        if end < 0 or start <= 0:
            sys.exit(f"{path}: typedef {name} nicht gefunden")
        return src[start:end + len(end_marker)]
    if spec.startswith("struct:"):
        name = spec[len("struct:"):]
        end_marker = "} " + name + ";\n"
        end = src.find(end_marker)
        start = src.rfind("static struct {", 0, end) if end >= 0 else -1
        if start < 0:
            sys.exit(f"{path}: struct {name} nicht gefunden")
        return src[start:end + len(end_marker)]
    # Funktionsdefinition: Zeile in Spalte 0 mit "name(", die kein Prototyp ist
    for m in re.finditer(r"^[A-Za-z_][^\n;{}]*\b" + re.escape(spec) + r"\(", src, re.M):
        start = m.start()
        body = src.find("{", start)
        semi = src.find(";", start)
        if body < 0 or (0 <= semi < body):
            continue  # Prototyp oder Aufruf
        end = src.find("\n}\n", start)
        return src[start:end + 3]
    sys.exit(f"{path}: Funktion {spec} nicht gefunden")


def main():
    src_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    cache = {}
    for header, (src_name, specs) in HEADERS.items():
        path = os.path.join(src_dir, src_name)
        src = cache.setdefault(path, open(path, encoding="utf-8").read())
        parts = [extract(src, s, path) for s in specs]
        with open(os.path.join(out_dir, header), "w", encoding="utf-8") as f:
            f.write(f"/* Automatisch erzeugt aus {src_name} von tests/gen_headers.py - nicht bearbeiten. */\n")
            f.write("\n".join(parts) + "\n")


if __name__ == "__main__":
    main()
