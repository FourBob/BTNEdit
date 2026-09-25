#!/bin/bash
# Baut und startet alle Tests unter tests/ - aufgerufen von "make test".
#
#   CC=clang|gcc     Compiler (Standard: cc). Auf macOS clang, unter Linux gcc
#                    (clang dort oft ohne ASan-Laufzeit).
#   SANITIZE=0       ohne AddressSanitizer/UBSan bauen (schneller, weniger streng)
#   ONLY=name        nur Tests, deren Name "name" enthaelt
#   TEST_EXTRA_CFLAGS zusaetzliche Flags fuer Compiler und Linker (make test
#                    setzt hier z.B. -isysroot aus BTN_SDK)
#
# Die Tests laufen ohne macOS-Frameworks: editor.c, eol.c, gapbuffer.c, highlight.c
# und strings.c werden direkt gelinkt, die reinen C-Teile aus main.c/render.c
# erzeugt tests/gen_headers.py bei jedem Lauf frisch aus dem Quelltext.
set -u
cd "$(dirname "$0")/.."

CC=${CC:-cc}
OUT=build/tests
GEN=$OUT/gen
mkdir -p "$OUT"

python3 tests/gen_headers.py src "$GEN" || { echo "Header-Erzeugung fehlgeschlagen"; exit 1; }

CFLAGS="-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Isrc -I$GEN -Itests ${TEST_EXTRA_CFLAGS:-}"
if [ "${SANITIZE:-1}" != 0 ]; then
    CFLAGS="$CFLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
fi
EDITOR_SRC="src/editor.c src/eol.c src/gapbuffer.c src/highlight.c"

# Name | zusaetzliche Quellen/Flags | Umgebung beim Start
TESTS=(
    "test_editor_basics|$EDITOR_SRC|"
    "test_utf8_column|$EDITOR_SRC|"
    "test_utf8_rule|$EDITOR_SRC|"
    "test_char_boundaries|$EDITOR_SRC|"
    "test_regex_replace|$EDITOR_SRC|"
    "test_tab_search|$EDITOR_SRC|"
    "test_layout_cache|$EDITOR_SRC|"
    "test_layout_cache_lang|$EDITOR_SRC|"
    "test_gapbuffer|src/gapbuffer.c|"
    "test_undo|$OUT/editor_inject.o src/eol.c src/gapbuffer.c|"
    "test_eol|$EDITOR_SRC|"
    "test_eol_glue|$EDITOR_SRC src/filestamp.c|"
    "test_indent|$EDITOR_SRC|"
    "test_textinput|$EDITOR_SRC src/textinput.c|"
    "test_shortcuts|$EDITOR_SRC|"
    "test_mouse|$EDITOR_SRC -Itests/stubs|"
    "test_recovery|src/recovery.c src/filestamp.c|"
    "test_protect|$EDITOR_SRC src/recovery.c src/filestamp.c|"
    "test_oom|src/gapbuffer.c|ASAN_OPTIONS=allocator_may_return_null=1"
    "test_strings|src/strings.c|"
    "test_close_flow||"
    "test_save_atomic||"
    "test_save_links_perms||"
    "test_file_io|src/filestamp.c|"
    "test_font_size||"
    "test_row_capacity||"
    "test_regex_budget||"
    "test_tab_label||"
)

# test_undo braucht editor.c mit simulierbaren Allokationsfehlern.
if ! $CC $CFLAGS -include tests/inject.h -c src/editor.c -o $OUT/editor_inject.o 2>$OUT/editor_inject.log; then
    cat $OUT/editor_inject.log
    echo "editor.c (inject) konnte nicht gebaut werden"
    exit 1
fi

pass=0
fail=0
failed=()
for entry in "${TESTS[@]}"; do
    IFS='|' read -r name extra env <<<"$entry"
    if [ -n "${ONLY:-}" ] && [[ "$name" != *"$ONLY"* ]]; then
        continue
    fi
    log=$OUT/$name.log
    # shellcheck disable=SC2086
    if ! $CC $CFLAGS -o "$OUT/$name" "tests/$name.c" $extra -lm >"$log" 2>&1; then
        echo "BUILD FAIL  $name"
        sed 's/^/    /' "$log" | head -20
        fail=$((fail + 1))
        failed+=("$name")
        continue
    fi
    start=$(date +%s)
    # shellcheck disable=SC2086
    if env $env "$OUT/$name" >>"$log" 2>&1; then
        printf "ok          %-26s %3ss\n" "$name" "$(( $(date +%s) - start ))"
        pass=$((pass + 1))
    else
        echo "FAIL        $name"
        grep -m 15 -E "FAIL|ERROR|runtime error|Assertion" "$log" | sed 's/^/    /'
        echo "    (vollstaendig: $log)"
        fail=$((fail + 1))
        failed+=("$name")
    fi
done

echo
echo "$pass bestanden, $fail fehlgeschlagen"
if [ $fail -ne 0 ]; then
    printf '  %s\n' "${failed[@]}"
    exit 1
fi
