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
- Maus: I-Beam-Zeiger über dem Text, Scrollbalken (Knopf ziehen, daneben
  klicken blättert eine Seite), Markieren über den Fensterrand hinaus
  scrollt automatisch (je weiter draußen, desto schneller), Dateien aus dem
  Finder ins Fenster ziehen öffnet sie in Tabs
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
- Zeilen-Befehle: Kommentar ein/aus (`⌘/`, mit `//`, `#` bzw. `;` (INI) je
  nach Sprache, hinter der gemeinsamen Einrückung des Blocks), Zeilen duplizieren (`⇧⌘D`),
  Zeilen nach oben/unten verschieben (`⌥⌘[` / `⌥⌘]` oder `⌥⌘↑` / `⌥⌘↓`) - jeweils für alle
  Zeilen der Auswahl und als ein Undo-Schritt
- Unsichtbare Zeichen einblenden (`⌥⌘I`): Leerzeichen `·`, Tabs `»`,
  Zeilenenden `¬`; wird über Neustarts hinweg gemerkt
- Dark Mode - folgt automatisch dem System-Erscheinungsbild
- Schriftgröße anpassen (`⌘+`/`⌘-`/`⌘0` für Zurücksetzen) - wird über
  Neustarts hinweg gemerkt
- Einrücken: Return übernimmt die Einrückung der aktuellen Zeile; Tab und
  ⇧Tab rücken alle Zeilen einer Selektion ein bzw. aus - mit Tab oder vier
  Leerzeichen, je nachdem, was die Datei überwiegend nutzt (auch ein
  einzelner Tab wird in einer Leerzeichen-Datei zu Leerzeichen)
- Undo/Redo mit Coalescing aufeinanderfolgender Tastendrücke
- Dateien bis 1 GB (größere werden mit einer Meldung abgelehnt)
- Klammern und Anführungszeichen: Auto-Vervollständigung/Typdurchlauf für
  `()`, `[]`, `{}`, `""`, `''`, sowie Hervorhebung des zusammengehörigen Paars
- Eingabemethoden: Tottasten (`^`, `´`, `` ` ``), das Akzent-Menü beim
  Gedrückthalten einer Taste, chinesische/japanische Eingabe und die
  Emoji-Palette (Ctrl+⌘+Leertaste) - im Dokument und in den Suchfeldern
- UI-Sprache folgt der Systemeinstellung: Deutsch, Englisch, Französisch,
  Spanisch, Chinesisch (vereinfacht)
- Ungesichert-Dialog beim Schließen/Beenden (pro Tab, keiner geht verloren)
- Schutz der Arbeit: Ändert ein anderes Programm eine offene Datei, lädt
  BTNEdit sie still neu (ohne eigene Änderungen) oder fragt "Meine Version
  behalten" / "Neu laden"; Sichern warnt, bevor es eine Datei überschreibt,
  die sich seit dem Laden, dem letzten Sichern oder - bei wiederhergestellten
  Dokumenten - seit dem Absturz geändert hat; eine gelöschte Datei macht
  den Tab ungesichert. Ungesicherte Dokumente werden laufend in
  `~/Library/Application Support/BTNEdit/Recovery` gesichert und nach einem
  Absturz beim nächsten Start zur Wiederherstellung angeboten (Grenzen
  siehe TODO.md)
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
| `src/textinput.c`/`.h` | Eingabemethoden: UTF-16 ↔ Bytes nach der Zeichenregel, vorläufiger Text, Bereiche relativ zum Cursor. `shim.m` übersetzt nur `NSTextInputClient`. |
| `src/eol.c`/`.h` | Zeilenenden erkennen, im Puffer auf `\n` vereinheitlichen und beim Sichern zurückwandeln. |
| `src/filestamp.c`/`.h` | Fingerabdruck einer Datei (Inode, Größe, Änderungszeit in ns), um Änderungen durch andere Programme zu erkennen. |
| `src/recovery.c`/`.h` | Wiederherstellungsdateien schreiben/lesen (binärsicher, atomar) und die eines abgestürzten Laufs finden. |
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

Neuesten Stand von GitHub holen, sauber neu bauen und starten (klont bei
Bedarf, sichert lokale Änderungen per `git stash`, nimmt SDK 26.5, falls
vorhanden - siehe unten):

```bash
scripts/update-and-build.sh            # Klon in ~/Downloads/BTN
scripts/update-and-build.sh ~/code/BTN --test
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

Die Tests brauchen kein macOS und kein Xcode-Projekt (für Tests, die
`render.h`/`shim.h` einbinden, ersetzt `tests/stubs/` die CoreGraphics-Typen): `editor.c`,
`eol.c`, `gapbuffer.c`, `highlight.c`, `strings.c`, `filestamp.c` und `recovery.c` werden direkt gelinkt. Die reinen
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
| `test_textinput` | Eingabemethoden: UTF-16-Umrechnung, nachgestellte Abläufe (Tottaste, Pinyin, Akzent-Menü, Emoji, Suchfeld) mit dem Code aus `main.c` |
| `test_shortcuts` | Weitersuchen, Auswahl für Suche (auch mit NUL-Byte), Tab-Wechsel mit Umlauf |
| `test_mouse` | Scrollbalken-Geometrie samt Umkehrung, I-Beam-Flächen, Autoscroll-Tempo; `on_mouse()` aus `main.c` mit echtem Layout: Markieren mit Autoscroll-Takt, Knopf ziehen, Seite blättern |
| `test_lines` | Kommentar ein/aus (Einrückung, Leerzeilen, `#`, Selektion, Undo), Duplizieren, Verschieben (Ränder, letzte Zeile ohne Umbruch), Fuzz-Rückwege; Markierungen für unsichtbare Zeichen gegen die Spaltenregel; Menü-Verdrahtung aus `main.c` |
| `test_indent` | Auto-Indent bei Return, Tab/⇧Tab über mehrere Zeilen, Tab vs. Leerzeichen, Fuzz: Ausrücken nach Einrücken = Original |
| `test_save_atomic`, `test_save_links_perms`, `test_file_io` | atomares Sichern, Symlinks, Schreibschutz, Laden, Recent-Liste |
| `test_close_flow` | Schließen/Beenden verliert nie ungesicherte Änderungen |
| `test_eol` | Zeilenenden: Erkennung, bytegenauer Round-Trip LF/CRLF/CR, Umwandeln aus zwei Pufferhälften |
| `test_eol_glue` | Laden/Sichern/Menü aus `main.c` mit echten Dateien: gemischte und Binärdateien bleiben bytegleich |
| `test_gapbuffer`, `test_oom` | Gap-Buffer und Speichermangel-Helfer |
| `test_recovery` | Datei-Fingerabdruck (gleiche Größe, 1 ns, `rename`), Wiederherstellungsdateien: Round-Trip, beschädigte Dateien, Waisen toter Prozesse |
| `test_protect` | Schutz der Arbeit aus `main.c` mit echten Dateien: still neu laden, Nachfrage, Behalten, Konflikt beim Sichern, gelöschte Datei, wann Wiederherstellungsdateien entstehen/verschwinden, nachgestellter Absturz |
| `test_strings`, `test_tab_label`, `test_font_size`, `test_row_capacity` | Übersetzungstabelle, Tab-Beschriftung, Schriftgröße, sichtbare Zeilen |

Die Objective-C-Seite (`shim.m`: Tastatur, Maus, Dialoge) lässt sich so nicht
testen. `make test-objc` (nur macOS) erzeugt deshalb die echte View und prüft
die Eingabemethoden-Schnittstelle mit Protokollaufrufen und künstlichen
Tasten-Events, das Ablegen von Dateien und den Autoscroll-Takt. Dafür baut die CI (`.github/workflows/build.yml`) bei jedem Push auf
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
| Weitersuchen / Rückwärts suchen (auch bei geschlossener Suchleiste) | `⌘G` / `⇧⌘G` |
| Auswahl für Suche verwenden | `⌘E` |
| In der Suchleiste: nächster/vorheriger Treffer | `Return` / `⇧Return` |
| In der Suchleiste: Ersetzen+Weiter / Alle ersetzen | `Return` (im Ersetzen-Feld) / `⌘Return` oder Klick auf den "Alle ersetzen"-Knopf |
| Suchleiste schließen | `Esc` |
| Gehe zu Zeile | `⌘L` |
| Vergrößern / Verkleinern / Tatsächliche Größe | `⌘+` / `⌘-` / `⌘0` |
| Wort-/Zeilensprung, Zeilenanfang/-ende | `⌥←/→`, `⌘←/→`, Pos1/Ende |
| Zeilen ein-/ausrücken (Selektion über mehrere Zeilen) | `Tab` / `⇧Tab` |
| Kommentar ein/aus | `⌘/` |
| Zeilen duplizieren | `⇧⌘D` |
| Zeilen nach oben / unten verschieben | `⌥⌘[` / `⌥⌘]` oder `⌥⌘↑` / `⌥⌘↓` |
| Unsichtbare Zeichen einblenden | `⌥⌘I` |
| Nächster / vorheriger Tab | `⌃Tab` / `⌃⇧Tab` oder `⇧⌘]` / `⇧⌘[` |
| Im Dock ablegen / Vollbild | `⌘M` / `⌃⌘F` |
