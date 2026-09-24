# BTNEdit

Ein leichtgewichtiger, nativer macOS-Texteditor - selbstgeschrieben in C, mit
einem hauchdünnen Objective-C-Shim nur für AppKit-Chrome (Fenster, Menü,
Zwischenablage, Systemdialoge). Die Philosophie: so schlank wie Notepad.exe,
aber sieht und verhält sich wie eine waschechte Mac-App.

Kein `NSTextView`, kein `NSTabView`, kein `NSSearchField` - der komplette
Dokumentinhalt (Puffer, Cursor, Undo/Redo, Zeilenumbruch, Syntax-Highlighting,
Klammern, Suchen/Ersetzen) ist reine C-Logik und wird selbst über Core
Graphics/Core Text gezeichnet.

## Features

- Öffnen / Sichern / Sichern unter / Schließen / Neu
- Tabs (mehrere Dokumente gleichzeitig offen, eigene Tableiste)
- Zuletzt geöffnet (persistiert über Neustarts hinweg)
- Wortumbruch, Zeilennummern-Gutter, Statuszeile (Zeilen/Wörter/Zeichen,
  Zeilenenden)
- Zeilenenden: LF (macOS/Unix), CRLF (Windows) und CR (klassisches Mac OS)
  werden beim Öffnen erkannt und beim Sichern beibehalten; umstellen über
  Ablage > Zeilenenden. Eingefügter Text wird angepasst. Dateien mit
  gemischten Zeilenenden bleiben unverändert, bis man ein Format wählt
- Syntax-Highlighting: C/C++/Objective-C/Java, Python, Shell, JavaScript/
  TypeScript, Swift, Markdown, STL (ASCII), INI/Config, SVG, DXF (ASCII)
- Suchen/Ersetzen mit regulären Ausdrücken (POSIX ERE), Ersetzen/Alle ersetzen;
  Live-Hervorhebung aller Treffer beim Tippen samt Trefferzähler ("3 von 12
  Treffern"), Rückmeldung nach Ersetzen einzelner Treffer; Umschalter für
  Groß-/Kleinschreibung und ganzes Wort; Rückreferenzen im Ersetzen-Feld
  (`$1`/`\1` bzw. `$0`/`\0` für den kompletten Treffer) im Regex-Modus,
  dort außerdem `\t` für einen Tabulator (Such- und Ersetzen-Feld, `\\` für
  einen Backslash im Ersetzen-Feld); "Alle ersetzen" ist ein einziger
  Undo-Schritt
- Gehe zu Zeile (`⌘L`)
- Dark Mode - folgt automatisch dem System-Erscheinungsbild
- Schriftgröße anpassen (`⌘+`/`⌘-`/`⌘0` für Zurücksetzen) - wird über
  Neustarts hinweg gemerkt
- Undo/Redo mit Coalescing aufeinanderfolgender Tastendrücke
- Dateien bis 1 GB (größere werden mit einer Meldung abgelehnt)
- Klammern und Anführungszeichen: Auto-Vervollständigung/Typdurchlauf für
  `()`, `[]`, `{}`, `""`, `''`, sowie Hervorhebung des zusammengehörigen Paars
- UI-Sprache folgt der Systemeinstellung: Deutsch, Englisch, Französisch,
  Spanisch, Chinesisch (vereinfacht)
- Ungesichert-Dialog beim Schließen/Beenden (pro Tab, keiner geht verloren)
- Hilfe-Menü mit Tastenkürzel-Übersicht
- Drucken (über den System-Druckdialog, mit Syntax-Highlighting, seitenweise
  umgebrochen)
- Bietet sich in Finders "Öffnen mit" für so gut wie jedes textbasierte
  Dateiformat an (per Info.plist `CFBundleDocumentTypes`), nicht nur für die
  oben genannten Highlighting-Sprachen - Warnung vor dem Öffnen einer
  vermutlich binären Datei schützt trotzdem vor versehentlichem Bearbeiten

Was noch fehlt bzw. bekannte Einschränkungen: siehe [TODO.md](TODO.md).

## Architektur

| Datei | Verantwortung |
|---|---|
| `src/editor.c`/`.h` | Reine, präsentationsunabhängige Inhaltslogik: Gap-Buffer, Cursor/Selektion, Undo/Redo, Klammer-Matching. Kennt weder Core Text noch Fensterbreite. |
| `src/render.c`/`.h` | Core Graphics/Core Text: Wortumbruch-Layout (abhängig von Fensterbreite), Zeichnen von Text/Cursor/Selektion/Gutter/Statuszeile/Tableiste/Suchleiste, Hit-Testing. |
| `src/highlight.c`/`.h` | Reiner Tokenizer für Syntax-Highlighting, unabhängig von Editor/Core Text. |
| `src/strings.c`/`.h` | Übersetzungstabelle für die UI-Sprache (EN/DE/FR/ES/ZH) - reines C, damit main.c ohne Foundation auskommt. |
| `src/gapbuffer.c`/`.h` | Der Gap Buffer selbst (Puffer-Grundlage von editor.c). |
| `src/eol.c`/`.h` | Zeilenenden erkennen, im Puffer auf `\n` vereinheitlichen und beim Sichern zurückwandeln. |
| `src/shim.m`/`.h` | Der einzige Objective-C-Code: NSWindow/NSMenu/NSApplication/Event-Weiterleitung/NSPasteboard/NSOpenPanel/NSSavePanel/NSAlert - reine Chrome, keine Content-Widgets. |
| `src/main.c` | Reines C: verdrahtet Shim-Callbacks mit editor.c/render.c, Datei-I/O, Tab-/Dokumentverwaltung, Scroll-Zustand. |

## Bauen

Voraussetzungen: macOS mit installierten Xcode-Kommandozeilenwerkzeugen
(`xcode-select --install` - liefert neben `clang` auch `iconutil`, das
`make` fürs App-Icon aus `resources/AppIcon.iconset` braucht).

```bash
make        # baut build/BTNEdit.app
make run    # baut und öffnet die App
make clean  # räumt auf
```

Kein Xcode-Projekt nötig - ein einfaches `Makefile` reicht (`clang`,
`-framework Cocoa -framework CoreText -framework CoreGraphics`).

Falls der Link-Schritt mit `ld: ... tapi error: malformed file` /
`unknown architecture` abbricht: das ist kein Fehler im Code, sondern ein
bekannter Toolchain-Bug bei manchen (insbesondere sehr neuen/Beta-)
Xcode-/SDK-Kombinationen, bei denen der Linker die `.tbd`-Stub-Dateien der
aktuell aufgelösten SDK-Version nicht parsen kann. Abhilfe: eine ältere,
bereits auf der Maschine vorhandene SDK-Version erzwingen (Pfad ggf. mit
`ls /Library/Developer/CommandLineTools/SDKs/` ermitteln):

```bash
make clean
make BTN_SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk
```

## Entwicklung und Tests

```bash
make test                  # alle Tests (tests/), mit AddressSanitizer/UBSan
make test TEST_CC=gcc      # dasselbe unter Linux
ONLY=undo make test        # nur Tests, deren Name "undo" enthält
make bench                 # Laufzeit pro Tastendruck bei großen Dokumenten
```

Die Tests brauchen kein macOS und kein Xcode-Projekt: `editor.c`,
`eol.c`, `gapbuffer.c`, `highlight.c` und `strings.c` werden direkt gelinkt. Die reinen
C-Teile aus `main.c` und `render.c` (Regex-Suche, Sichern, Laden, Layout, ...)
schneidet `tests/gen_headers.py` bei jedem Lauf frisch aus dem Quelltext
heraus - ein Test prüft also immer den Code, der gerade im Repo steht. Welche
Funktionen das sind, steht in der Liste oben in dieser Datei; wer eine davon
umbenennt, muss sie dort nachziehen.

| Test | Prüft |
|---|---|
| `test_editor_basics` | Pfeiltasten, Löschen und Klammer-Umschließen auf Mehrbyte-Zeichen und NUL-Bytes |
| `test_utf8_rule`, `test_utf8_column`, `test_char_boundaries` | die eine Zeichenregel für Cursor, Umbruch, Spalten, Anzeige und Regex-Treffer |
| `test_regex_replace`, `test_tab_search`, `test_regex_budget` | Suchen/Ersetzen, Rückreferenzen, `\t`, Komplexitätsdeckel der Live-Suche |
| `test_layout_cache`, `test_layout_cache_lang` | Layout- und Kommentar-Cache gegen einen frischen Aufbau |
| `test_undo` | Undo-Gruppen und Fuzzing mit simulierten Allokationsfehlern |
| `test_save_atomic`, `test_save_links_perms`, `test_file_io` | atomares Sichern, Symlinks, Schreibschutz, Laden, Recent-Liste |
| `test_close_flow` | Schließen/Beenden verliert nie ungesicherte Änderungen |
| `test_eol` | Zeilenenden: Erkennung, bytegenauer Round-Trip LF/CRLF/CR, Umwandeln aus zwei Pufferhälften |
| `test_eol_glue` | Laden/Sichern/Menü aus `main.c` mit echten Dateien: gemischte und Binärdateien bleiben bytegleich |
| `test_gapbuffer`, `test_oom` | Gap-Buffer und Speichermangel-Helfer |
| `test_strings`, `test_tab_label`, `test_font_size`, `test_row_capacity` | Übersetzungstabelle, Tab-Beschriftung, Schriftgröße, sichtbare Zeilen |

Die Objective-C-Seite (`shim.m`: Tastatur, Maus, Dialoge) lässt sich so nicht
testen. Dafür baut die CI (`.github/workflows/build.yml`) bei jedem Push auf
einem macOS-Runner die echte App mit `-Werror`, startet sie kurz und führt die
Tests mit Apples Regex-Engine aus; ein zweiter Job führt sie unter Linux mit
gcc aus. Neue Tests: `tests/test_<name>.c` anlegen und in `tests/run_tests.sh`
in die Liste `TESTS` eintragen.

## Tastenkürzel

| Aktion | Shortcut |
|---|---|
| Neu / Öffnen / Sichern / Sichern unter | `⌘N` / `⌘O` / `⌘S` / `⇧⌘S` |
| Schließen (Tab) | `⌘W` |
| Widerrufen / Wiederholen | `⌘Z` / `⇧⌘Z` |
| Ausschneiden / Kopieren / Einfügen | `⌘X` / `⌘C` / `⌘V` |
| Alles auswählen | `⌘A` |
| Suchen und Ersetzen öffnen | `⌘F` |
| In der Suchleiste: nächster/vorheriger Treffer | `Return` / `⇧Return` |
| In der Suchleiste: Ersetzen+Weiter / Alle ersetzen | `Return` (im Ersetzen-Feld) / `⌘Return` oder Klick auf den "Alle ersetzen"-Knopf |
| Suchleiste schließen | `Esc` |
| Gehe zu Zeile | `⌘L` |
| Vergrößern / Verkleinern / Tatsächliche Größe | `⌘+` / `⌘-` / `⌘0` |
| Wort-/Zeilensprung, Zeilenanfang/-ende | `⌥←/→`, `⌘←/→`, Pos1/Ende |
