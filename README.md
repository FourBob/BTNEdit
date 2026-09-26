# BTNEdit

Ein leichtgewichtiger, nativer macOS-Texteditor - selbstgeschrieben in C, mit
einem hauchdünnen Objective-C-Shim nur für AppKit-Chrome (Fenster, Menü,
Zwischenablage, Systemdialoge, Netzwerk). Die Philosophie: so schlank wie
Notepad.exe, aber sieht und verhält sich wie eine waschechte Mac-App.

Kein `NSTextView`, kein `NSTabView`, kein `NSSearchField` - der komplette
Dokumentinhalt (Puffer, Cursor, Undo/Redo, Zeilenumbruch, Syntax-Highlighting,
Klammern, Suchen/Ersetzen, Zeilen-Befehle) ist reine C-Logik und wird selbst
über Core Graphics/Core Text gezeichnet. Läuft ab macOS 11.

## Features

### Dateien und Tabs

- Neu / Öffnen / Sichern / Sichern unter / Schließen, Zuletzt geöffnet
  (über Neustarts hinweg)
- Tabs mit eigener Tableiste; eine schon offene Datei wird nicht doppelt
  geladen, ein leerer Tab wird wiederverwendet
- Dateien aus dem Finder ins Fenster oder aufs Dock-Icon ziehen, "Öffnen
  mit" für so gut wie jedes textbasierte Format (Info.plist
  `CFBundleDocumentTypes`)
- Zeilenenden: LF (macOS/Unix), CRLF (Windows) und CR (klassisches Mac OS)
  werden beim Öffnen erkannt und beim Sichern beibehalten; umstellen über
  Ablage > Zeilenenden. Eingefügter Text wird angepasst. Dateien mit
  gemischten Zeilenenden bleiben Byte für Byte unverändert, bis man ein
  Format wählt
- Atomares Sichern (Tempdatei + `rename`), Symlinks werden aufgelöst,
  schreibgeschützte Dateien nicht überschrieben
- Dateien bis 1 GB; Warnung vor dem Öffnen einer vermutlich binären Datei
- Ungesichert-Dialog beim Schließen und Beenden (für jeden Tab, keiner geht
  verloren)
- Drucken über den System-Druckdialog, mit Syntax-Highlighting und
  seitenweisem Umbruch

### Schutz der Arbeit

- Ändert ein anderes Programm eine offene Datei, lädt BTNEdit sie still neu
  (ohne eigene Änderungen) oder fragt "Meine Version behalten" / "Neu
  laden"; ein Log, das weiterwächst, wird alle paar Sekunden nachgeladen
- Sichern warnt, bevor es eine Datei überschreibt, die sich seit dem Laden,
  dem letzten Sichern oder - bei wiederhergestellten Dokumenten - seit dem
  Absturz geändert hat; eine gelöschte Datei macht den Tab ungesichert
- Ungesicherte Dokumente werden laufend in
  `~/Library/Application Support/BTNEdit/Recovery` gesichert (nie über das
  Original) und nach einem Absturz beim nächsten Start zur
  Wiederherstellung angeboten

### Bearbeiten

- Undo/Redo mit Zusammenfassen aufeinanderfolgender Tastendrücke; Befehle
  wie "Alle ersetzen", Einrücken oder Zeilen verschieben sind je ein Schritt
- Einrücken: Return übernimmt die Einrückung der aktuellen Zeile; Tab und
  ⇧Tab rücken alle Zeilen einer Auswahl ein bzw. aus - mit Tab oder vier
  Leerzeichen, je nachdem, was die Datei überwiegend nutzt
- Zeilen-Befehle für alle Zeilen der Auswahl: Kommentar ein/aus (`⌘/`, mit
  `//`, `#` bzw. `;` je nach Sprache, hinter der gemeinsamen Einrückung),
  Zeilen duplizieren (`⇧⌘D`), Zeilen nach oben/unten verschieben
- Klammern und Anführungszeichen: automatisches Schließen und Überschreiben
  für `()`, `[]`, `{}`, `""`, `''`, Hervorhebung des zusammengehörigen Paars
- Eingabemethoden: Tottasten (`^`, `´`, `` ` ``), Akzent-Menü beim
  Gedrückthalten einer Taste, chinesische/japanische Eingabe, Emoji-Palette
  (`⌃⌘Leertaste`) - im Dokument und in den Suchfeldern
- KI-Vervollständigung (optional, standardmäßig aus): nach einer kurzen
  Tipp-Pause schlägt ein lokal laufendes Code-Modell die Fortsetzung der
  Zeile vor - [siehe unten](#ki-vervollständigung-lokal)

### Suchen

- Suchen/Ersetzen mit Live-Hervorhebung aller Treffer und Trefferzähler
  ("Treffer 3 von 12"), Umschalter für reguläre Ausdrücke (POSIX ERE),
  Groß-/Kleinschreibung und ganzes Wort
- Ersetzen, Ersetzen+Weiter, Alle ersetzen; im Regex-Modus Rückreferenzen
  (`$1`/`\1`, `$0` für den ganzen Treffer) und `\t` für einen Tabulator
- Weitersuchen (`⌘G`/`⇧⌘G`) auch bei geschlossener Suchleiste, Auswahl für
  Suche verwenden (`⌘E`), Gehe zu Zeile (`⌘L`)

### Darstellung

- Wortumbruch, Zeilennummern, Statuszeile (Position, Zeilen/Wörter/Zeichen,
  Zeilenende-Format)
- Syntax-Highlighting: C/C++/Objective-C/Java, Python, Shell,
  JavaScript/TypeScript, Swift, Markdown, STL (ASCII), INI/Config, SVG,
  DXF (ASCII)
- Unsichtbare Zeichen einblenden (`⌥⌘I`): Leerzeichen `·`, Tabs `»`,
  Zeilenenden `¬`
- Dark Mode folgt automatisch dem System; Schriftgröße (`⌘+`/`⌘-`/`⌘0`) und
  unsichtbare Zeichen werden über Neustarts hinweg gemerkt
- Maus: I-Beam über dem Text, Scrollbalken (Knopf ziehen, daneben klicken
  blättert eine Seite), Markieren über den Fensterrand hinaus scrollt
  automatisch (je weiter draußen, desto schneller)
- Bedienoberfläche in der Systemsprache: Deutsch, Englisch, Französisch,
  Spanisch, Chinesisch (vereinfacht); Hilfe-Menü mit Tastenkürzel-Übersicht

Was noch fehlt und bekannte Einschränkungen: siehe [TODO.md](TODO.md).

## KI-Vervollständigung (lokal)

BTNEdit bringt kein Modell mit, sondern fragt einen Server auf dem eigenen
Rechner - [Ollama](https://ollama.com) oder `llama-server` aus llama.cpp:

```bash
ollama pull qwen2.5-coder:1.5b   # kleines Code-Modell mit Fill-in-the-Middle
ollama serve                     # falls Ollama nicht schon als App läuft
```

Dann in BTNEdit Bearbeiten > KI-Vervollständigung einschalten. BTNEdit
prüft dabei (und bei jedem Start), ob der Server erreichbar ist und welche
Modelle installiert sind - ohne Dokumenttext, nur `GET /api/tags`. Ist das
eingestellte Modell nicht installiert, nimmt es das kleinste installierte
Code-Modell (Name enthält `coder`, `codellama`, `codegemma`, `codestral`
oder `starcoder`); gibt es keins, kommt ein Hinweis. Im Untermenü
**Bearbeiten > KI-Modell** stehen:

- eine Statuszeile ("Ollama: 3 Modelle installiert", "Nicht erreichbar:
  http://…"),
- alle installierten Modelle, das gewählte mit Haken - ein Klick wechselt,
- "Liste aktualisieren" (nach `ollama pull`) und "KI-Verbindung testen…".

Die Einstellungen stehen in `~/.btnedit_ai` (wird beim ersten Einschalten
angelegt). Die Datei wird beim Start, bei jedem Umschalten, Aktualisieren und
Testen gelesen; das Menü ändert nur die Zeilen `enabled=` und `model=`,
Kommentare und eigene Einträge bleiben:

```
enabled=1
api=ollama          # oder llama (llama-server, Endpunkt /infill)
url=http://127.0.0.1:11434
model=qwen2.5-coder:1.5b
delay_ms=300        # Tipp-Pause bis zur Anfrage
max_tokens=48
```

So läuft es ab:

- Gefragt wird nach der Tipp-Pause, wenn rechts vom Cursor höchstens
  Leerraum oder Schließendes (`)`, `;`, ...) steht - nicht bei einer
  Auswahl, während einer Eingabemethode oder im Suchfeld. Jeder weitere
  Tastendruck bricht eine laufende Anfrage ab; die Oberfläche wartet nie.
- Der Vorschlag (eine Zeile) erscheint grau hinter dem Cursor: `Tab`
  übernimmt ihn als eigenen Undo-Schritt, `Esc` verwirft ihn, wer seinen
  Anfang tippt, behält den Rest; jede andere Änderung lässt ihn
  verschwinden.
- Läuft kein Server, bleibt es beim Tippen still; nur beim Einschalten,
  Aktualisieren und Testen kommt eine Meldung. Mit llama-server gibt es keine
  Modellliste (dort läuft genau das beim Start geladene Modell), geprüft
  wird nur `/health`.

Datenschutz: Nach der Tipp-Pause gehen bis zu 4 KB Text vor und 1 KB nach
dem Cursor an `url` - nur einen Server eintragen, dem man den
Dokumentinhalt anvertraut. BTNEdit geht dabei direkt dorthin (kein
System-Proxy, keine Umleitungen, höchstens 90 s pro Anfrage - die erste
lädt das Modell erst in den Speicher, danach bleibt es 30 min geladen). Unverschlüsseltes
`http://` klappt nur zum eigenen Rechner, zu IP-Adressen und `.local`-Namen
im lokalen Netz, sonst `https://`. Vorschläge werden vor der Anzeige
bereinigt: Steuer- und unsichtbare Zeichen (Richtungs-Steuerzeichen,
Nullbreite) schneiden sie ab.

### Wenn keine Vorschläge kommen

1. **Bearbeiten > KI-Modell** zeigt, ob der Server erreichbar ist und
   welches Modell gewählt ist. **KI-Verbindung testen…** darunter holt die
   Modellliste und schickt dann eine kleine Anfrage an das gewählte Modell:
   die Meldung nennt Modell und Vorschlag oder den Fehler des Servers.
2. Ist der Haken bei Bearbeiten > KI-Vervollständigung gesetzt?
3. Gefragt wird nur, wenn rechts vom Cursor höchstens Leerraum oder
   Schließendes steht, und der Vorschlag ist eine Zeile: am Ende von
   `int main() {` will das Modell meist eine neue Zeile beginnen - dann gibt
   es nichts anzuzeigen. Mitten in einer Zeile wie `for (int i = 0; `
   kommt eher etwas.
4. Die erste Anfrage nach dem Start von Ollama dauert, bis das Modell
   geladen ist.
5. Details im Terminal: `BTNEDIT_AI_DEBUG=1 build/BTNEdit.app/Contents/MacOS/BTNEdit`
   (direkt starten, nicht über `open`) gibt jede Anfrage und Antwort aus. Der Server selbst lässt sich prüfen mit:

   ```bash
   curl -s http://127.0.0.1:11434/api/generate -d '{"model":"qwen2.5-coder:1.5b",
     "prompt":"def add(a, b):\n    return ","suffix":"\n","stream":false}'
   ```

## Tastenkürzel

| Aktion | Shortcut |
|---|---|
| Neu / Öffnen / Sichern / Sichern unter | `⌘N` / `⌘O` / `⌘S` / `⇧⌘S` |
| Tab schließen / Drucken | `⌘W` / `⌘P` |
| Widerrufen / Wiederholen | `⌘Z` / `⇧⌘Z` |
| Ausschneiden / Kopieren / Einfügen / Alles auswählen | `⌘X` / `⌘C` / `⌘V` / `⌘A` |
| Suchen und Ersetzen öffnen | `⌘F` |
| Weitersuchen / rückwärts suchen (auch bei geschlossener Suchleiste) | `⌘G` / `⇧⌘G` |
| Auswahl für Suche verwenden | `⌘E` |
| In der Suchleiste: nächster / vorheriger Treffer | `Return` / `⇧Return` |
| In der Suchleiste: Ersetzen+Weiter / Alle ersetzen | `Return` (im Ersetzen-Feld) / `⌘Return` oder Knopf "Alle ersetzen" |
| Suchleiste schließen | `Esc` |
| Gehe zu Zeile | `⌘L` |
| Wortsprung, Zeilenanfang/-ende, Dokumentanfang/-ende | `⌥←/→`, `⌘←/→` oder Pos1/Ende, `⌘↑/↓` |
| Zeilen ein-/ausrücken | `Tab` / `⇧Tab` |
| Kommentar ein/aus | `⌘/` |
| Zeilen duplizieren | `⇧⌘D` |
| Zeilen nach oben / unten verschieben | `⌥⌘[` / `⌥⌘]` oder `⌥⌘↑` / `⌥⌘↓` |
| KI-Vorschlag übernehmen / verwerfen | `Tab` / `Esc` |
| Emoji-Palette | `⌃⌘Leertaste` |
| Vergrößern / Verkleinern / Tatsächliche Größe | `⌘+` / `⌘-` / `⌘0` |
| Unsichtbare Zeichen einblenden | `⌥⌘I` |
| Nächster / vorheriger Tab | `⌃Tab` / `⌃⇧Tab` oder `⇧⌘]` / `⇧⌘[` |
| Im Dock ablegen / Vollbild | `⌘M` / `⌃⌘F` |

## Dateien, die BTNEdit anlegt

| Datei | Inhalt |
|---|---|
| `~/.btnedit_recent` | Zuletzt geöffnete Dateien (eine pro Zeile) |
| `~/.btnedit_prefs` | Schriftgröße, unsichtbare Zeichen ein/aus |
| `~/.btnedit_ai` | Einstellungen der KI-Vervollständigung (erst beim ersten Einschalten) |
| `~/Library/Application Support/BTNEdit/Recovery/` | Wiederherstellungsdateien ungesicherter Dokumente und eine Sperrdatei je laufendem Programm; nach normalem Beenden leer bis auf die Sperrdatei |

## Bauen

Voraussetzungen: macOS mit installierten Xcode-Kommandozeilenwerkzeugen
(`xcode-select --install` - liefert neben `clang` auch `iconutil`, das
`make` fürs App-Icon aus `resources/AppIcon.iconset` braucht). Kein
Xcode-Projekt nötig - ein einfaches `Makefile` reicht (`clang`,
`-framework Cocoa -framework CoreText -framework CoreGraphics`).

```bash
make        # baut build/BTNEdit.app
make run    # baut und öffnet die App
make clean  # räumt auf
```

Neuesten Stand von GitHub holen, sauber neu bauen und starten (klont bei
Bedarf, sichert lokale Änderungen per `git stash` und lokale Commits in
einem Backup-Branch, nimmt SDK 26.5, falls vorhanden - siehe unten):

```bash
scripts/update-and-build.sh                    # Klon in ~/Downloads/BTN
scripts/update-and-build.sh ~/code/BTN --test  # anderer Ordner, danach make test
```

Falls der Link-Schritt mit `ld: ... tapi error: malformed file` /
`unknown architecture` abbricht: das ist kein Fehler im Code, sondern ein
bekannter Toolchain-Bug bei manchen (insbesondere sehr neuen/Beta-)
Xcode-/SDK-Kombinationen, bei denen der Linker die `.tbd`-Stub-Dateien der
aktuell aufgelösten SDK-Version nicht parsen kann. Abhilfe: eine ältere,
bereits auf der Maschine vorhandene SDK-Version erzwingen (Pfad ggf. mit
`ls /Library/Developer/CommandLineTools/SDKs/` ermitteln); `BTN_SDK` gilt
für `make`, `make test` und `make test-objc`:

```bash
make clean
make BTN_SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk
```

## Architektur

| Datei | Verantwortung |
|---|---|
| `src/editor.c`/`.h` | Reine, präsentationsunabhängige Inhaltslogik: Cursor/Selektion, Undo/Redo samt Gruppen, Einrücken, Zeilen-Befehle (Kommentar, Duplizieren, Verschieben), Klammer-Matching. Kennt weder Core Text noch Fensterbreite. |
| `src/gapbuffer.c`/`.h` | Der Gap Buffer selbst (Puffer-Grundlage von editor.c) und die Speicher-Helfer. |
| `src/render.c`/`.h` | Core Graphics/Core Text: Wortumbruch-Layout (abhängig von Fensterbreite, gecacht), Zeichnen von Text/Cursor/Selektion/Gutter/Statuszeile/Tableiste/Suchleiste/Scrollbalken, unsichtbare Zeichen, vorläufiger Text der Eingabemethoden, KI-Geistertext, Hit-Testing. |
| `src/highlight.c`/`.h` | Reiner Tokenizer für Syntax-Highlighting und das Kommentarzeichen je Sprache, unabhängig von Editor/Core Text. |
| `src/textinput.c`/`.h` | Eingabemethoden: UTF-16 ↔ Bytes nach der Zeichenregel, vorläufiger Text, Bereiche relativ zum Cursor. `shim.m` übersetzt nur `NSTextInputClient`. |
| `src/eol.c`/`.h` | Zeilenenden erkennen, im Puffer auf `\n` vereinheitlichen und beim Sichern zurückwandeln. |
| `src/filestamp.c`/`.h` | Fingerabdruck einer Datei (Gerät, Inode, Größe, mtime und ctime in ns), um Änderungen durch andere Programme zu erkennen. |
| `src/recovery.c`/`.h` | Wiederherstellungsdateien schreiben/lesen (binärsicher, atomar) und die abgestürzter Läufe finden (Sperrdatei je Lauf). |
| `src/ai.c`/`.h` | KI-Vervollständigung ohne Netzwerk: Einstellungen, JSON der Anfrage (Ollama/llama-server), Antwort auswerten, Vorschlag bereinigen. |
| `src/strings.c`/`.h` | Übersetzungstabelle für die UI-Sprache (EN/DE/FR/ES/ZH) - reines C, damit main.c ohne Foundation auskommt. |
| `src/shim.m`/`.h` | Der einzige Objective-C-Code: Fenster, Menü, Tastatur/Maus (inkl. `NSTextInputClient`, Autoscroll-Takt, I-Beam-Flächen, Drag & Drop), Zwischenablage, Systemdialoge, Timer, HTTP-Anfragen (`NSURLSession`) - reine Chrome, keine Content-Widgets. |
| `src/main.c` | Reines C: verdrahtet die Shim-Callbacks mit editor.c/render.c; Tabs und Dokumente, Datei-I/O, Suchen/Ersetzen, Scrollen und Maus, Schutz der Arbeit, Ablauf der KI-Vervollständigung. |

## Entwicklung und Tests

```bash
make test                  # alle Tests (tests/), mit AddressSanitizer/UBSan
make test TEST_CC=gcc      # dasselbe unter Linux
ONLY=undo make test        # nur Tests, deren Name "undo" enthält
make test-objc             # nur macOS: Tests der echten View aus shim.m
make bench                 # Laufzeit pro Tastendruck bei großen Dokumenten
```

Die C-Tests brauchen kein macOS und kein Xcode-Projekt: `editor.c`, `eol.c`,
`gapbuffer.c`, `highlight.c`, `strings.c`, `textinput.c`, `filestamp.c`,
`recovery.c` und `ai.c` werden direkt gelinkt; für Tests, die
`render.h`/`shim.h` einbinden, ersetzt `tests/stubs/` die CoreGraphics-Typen.
Die reinen C-Teile aus `main.c` und `render.c` (Suchen, Laden, Sichern,
Layout, Maus, Schutz der Arbeit, KI-Ablauf, ...) schneidet
`tests/gen_headers.py` bei jedem Lauf frisch aus dem Quelltext heraus - ein
Test prüft also immer den Code, der gerade im Repo steht. Welche Funktionen
das sind, steht in der Liste oben in `tests/gen_headers.py`; wer eine davon
umbenennt, muss sie dort nachziehen. Netzwerk, Timer und Dialoge sind in
diesen Tests Stubs.

| Test | Prüft |
|---|---|
| `test_editor_basics` | Pfeiltasten, Löschen und Klammer-Umschließen auf Mehrbyte-Zeichen und NUL-Bytes |
| `test_utf8_rule`, `test_utf8_column`, `test_char_boundaries` | die eine Zeichenregel für Cursor, Umbruch, Spalten, Anzeige und Regex-Treffer |
| `test_regex_replace`, `test_tab_search`, `test_regex_budget` | Suchen/Ersetzen, Rückreferenzen, `\t`, Komplexitätsdeckel der Live-Suche |
| `test_layout_cache`, `test_layout_cache_lang` | Layout- und Kommentar-Cache gegen einen frischen Aufbau |
| `test_undo` | Undo-Gruppen und Fuzzing mit simulierten Allokationsfehlern |
| `test_indent` | Auto-Indent bei Return, Tab/⇧Tab über mehrere Zeilen, Tab vs. Leerzeichen, Fuzz: Ausrücken nach Einrücken = Original |
| `test_lines` | Kommentar ein/aus (Einrückung, Leerzeilen, `#`/`;`, Selektion, Undo), Duplizieren, Verschieben (Ränder, letzte Zeile ohne Umbruch, CRLF), Fuzz-Rückwege; Markierungen für unsichtbare Zeichen gegen die Spaltenregel; Menü-Verdrahtung aus `main.c` |
| `test_textinput` | Eingabemethoden: UTF-16-Umrechnung, nachgestellte Abläufe (Tottaste, Pinyin, Akzent-Menü, Emoji, Suchfeld) mit dem Code aus `main.c` |
| `test_shortcuts` | Weitersuchen, Auswahl für Suche (auch mit NUL-Byte), Tab-Wechsel mit Umlauf |
| `test_mouse` | Scrollbalken-Geometrie samt Umkehrung, I-Beam-Flächen, Autoscroll-Tempo; `on_mouse()` aus `main.c` mit echtem Layout: Markieren mit Autoscroll-Takt, Knopf ziehen, Seite blättern |
| `test_eol` | Zeilenenden: Erkennung, bytegenauer Round-Trip LF/CRLF/CR, Umwandeln aus zwei Pufferhälften |
| `test_eol_glue` | Laden/Sichern/Menü aus `main.c` mit echten Dateien: gemischte und Binärdateien bleiben bytegleich |
| `test_save_atomic`, `test_save_links_perms`, `test_file_io` | atomares Sichern, Symlinks, Schreibschutz, Laden, Recent-Liste |
| `test_close_flow` | Schließen/Beenden verliert nie ungesicherte Änderungen |
| `test_recovery` | Datei-Fingerabdruck (gleiche Größe, 1 ns, `rename`, ctime), Wiederherstellungsdateien: Round-Trip, beschädigte Dateien, Sperren und verwaiste Dateien abgestürzter Läufe |
| `test_protect` | Schutz der Arbeit aus `main.c` mit echten Dateien: still neu laden, Nachfrage, Behalten, Konflikt beim Sichern, gelöschte Datei, Schreiben während des Lesens, volle Platte, wann Wiederherstellungsdateien entstehen/verschwinden, nachgestellter Absturz |
| `test_ai` | KI: Einstellungsdatei, JSON-Rundlauf (auch kaputtes UTF-8, Steuerzeichen), Antworten (Escapes, Surrogatpaare, verschachtelte Werte, kaputtes JSON), Vorschlag bereinigen, Modellliste lesen und Code-Modell wählen |
| `test_ai_glue` | KI-Ablauf aus `main.c`: wann gefragt wird, Kontextgrenzen, veraltete/fehlerhafte Antworten, Geistertext, Tab als eigener Undo-Schritt, Weitertippen, Abbrechen, Einstellungsdatei, Modellliste (fehlendes Modell ersetzen, Server nicht erreichbar, kein Code-Modell), Verbindungstest |
| `test_gapbuffer`, `test_oom` | Gap-Buffer und Speichermangel-Helfer |
| `test_strings`, `test_tab_label`, `test_font_size`, `test_row_capacity` | Übersetzungstabelle, Tab-Beschriftung, Schriftgröße, sichtbare Zeilen |

Die Objective-C-Seite lässt sich so nicht testen. `make test-objc` (nur
macOS) erzeugt deshalb die echte View aus `shim.m` und prüft sie mit
Protokollaufrufen und künstlichen Events: Eingabemethoden und Tasten, die
Menü-Tastenkürzel, das Ablegen von Dateien, Autoscroll- und Pausen-Timer
und HTTP-Anfragen gegen einen kleinen lokalen Testserver (Antwort,
Abbrechen, abgelehnte Verbindung).

Die CI (`.github/workflows/build.yml`) baut bei jedem Push auf einem
macOS-Runner die echte App mit `-Werror`, startet sie kurz, führt die
C-Tests mit Apples Regex-Engine und `make test-objc` aus; ein zweiter Job
führt die C-Tests unter Linux mit gcc aus. Neue Tests: `tests/test_<name>.c`
anlegen und in `tests/run_tests.sh` in die Liste `TESTS` eintragen.
