#!/bin/bash
# BTNEdit: neuesten Stand von GitHub holen, bauen und starten.
#
#   scripts/update-and-build.sh [Ordner] [--test]
#
#   Ordner   wohin geklont wird bzw. wo der Klon liegt (Standard: ~/Downloads/BTN)
#   --test   nach dem Bauen zusaetzlich "make test" ausfuehren
#
# Behandelt die bekannten Stolpersteine:
#   - SDK 27.0 bricht den Linker ("tapi error: malformed file") -> 26.5 nehmen
#   - alte .o-Dateien gegen ein anderes SDK -> "make clean"
#   - lokaler Branch ohne Upstream -> explizit von origin holen
#   - darueber entpacktes Archiv / lokale Aenderungen -> in "git stash" sichern
#   - lokale Commits, die es auf GitHub nicht gibt -> Backup-Branch
set -euo pipefail

REPO_URL=https://github.com/FourBob/BTNEdit.git
BRANCH=claude/macos-text-editor-swift-18v14h
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk

DIR="$HOME/Downloads/BTN"
RUN_TESTS=0
for arg in "$@"; do
    case "$arg" in
        --test) RUN_TESTS=1 ;;
        *) DIR="$arg" ;;
    esac
done

if [ ! -d "$DIR/.git" ]; then
    echo ">> Klone $REPO_URL nach $DIR"
    git clone --branch "$BRANCH" "$REPO_URL" "$DIR"
    cd "$DIR"
else
    cd "$DIR"
    git fetch origin "$BRANCH"

    if [ -n "$(git status --porcelain)" ]; then
        git stash push -u -m "Backup vor Update $(date '+%Y-%m-%d %H:%M')"
        echo ">> Lokale Aenderungen in 'git stash' gesichert (zurueck: git stash pop)"
    fi

    git checkout -q "$BRANCH" 2>/dev/null || git checkout -q -b "$BRANCH" "origin/$BRANCH"
    if [ -n "$(git log --oneline "origin/$BRANCH..HEAD")" ]; then
        BACKUP="backup/$(date '+%Y%m%d-%H%M%S')"
        git branch "$BACKUP"
        echo ">> Lokale Commits gesichert in Branch $BACKUP"
    fi
    git reset -q --hard "origin/$BRANCH"
    git branch -q --set-upstream-to="origin/$BRANCH"
fi
echo ">> Stand: $(git log --oneline -1)"

SDK_ARG=()
if [ -d "$SDK" ]; then
    SDK_ARG=(BTN_SDK="$SDK")
else
    echo ">> $SDK nicht gefunden - baue mit Standard-SDK"
fi

make clean
make ${SDK_ARG[@]+"${SDK_ARG[@]}"}

# Tests brauchen dasselbe SDK wie die App. Schlagen sie fehl, wird die App
# trotzdem geoeffnet - das Ergebnis steht dann am Ende.
TEST_RESULT=""
if [ "$RUN_TESTS" = 1 ]; then
    if make test ${SDK_ARG[@]+"${SDK_ARG[@]}"}; then
        TEST_RESULT=">> Tests: alle bestanden"
    else
        TEST_RESULT=">> Tests: FEHLGESCHLAGEN (Logs in $DIR/build/tests/*.log)"
    fi
fi

echo ">> Fertig: $DIR/build/BTNEdit.app"
open build/BTNEdit.app
if [ -n "$TEST_RESULT" ]; then
    echo "$TEST_RESULT"
    [[ "$TEST_RESULT" != *FEHLGESCHLAGEN* ]]
fi
