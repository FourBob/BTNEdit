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
- Wortumbruch, Zeilennummern-Gutter, Statuszeile (Zeilen/Wörter/Zeichen)
- Syntax-Highlighting: C/C++/Objective-C/Java, Python, Shell, JavaScript/
  TypeScript, Swift, Markdown
- Suchen/Ersetzen mit regulären Ausdrücken (POSIX ERE), Ersetzen/Alle ersetzen
- Undo/Redo mit Coalescing aufeinanderfolgender Tastendrücke
- Klammern und Anführungszeichen: Auto-Vervollständigung/Typdurchlauf für
  `()`, `[]`, `{}`, `""`, `''`, sowie Hervorhebung des zusammengehörigen Paars
- UI-Sprache folgt der Systemeinstellung: Deutsch, Englisch, Französisch,
  Spanisch, Chinesisch (vereinfacht)
- Ungesichert-Dialog beim Schließen/Beenden (pro Tab, keiner geht verloren)
- Hilfe-Menü mit Tastenkürzel-Übersicht
- Drucken (über den System-Druckdialog, mit Syntax-Highlighting, seitenweise
  umgebrochen)

Was noch fehlt bzw. bekannte Einschränkungen: siehe [TODO.md](TODO.md).

## Architektur

| Datei | Verantwortung |
|---|---|
| `src/editor.c`/`.h` | Reine, präsentationsunabhängige Inhaltslogik: Gap-Buffer, Cursor/Selektion, Undo/Redo, Klammer-Matching. Kennt weder Core Text noch Fensterbreite. |
| `src/render.c`/`.h` | Core Graphics/Core Text: Wortumbruch-Layout (abhängig von Fensterbreite), Zeichnen von Text/Cursor/Selektion/Gutter/Statuszeile/Tableiste/Suchleiste, Hit-Testing. |
| `src/highlight.c`/`.h` | Reiner Tokenizer für Syntax-Highlighting, unabhängig von Editor/Core Text. |
| `src/strings.c`/`.h` | Übersetzungstabelle für die UI-Sprache (EN/DE/FR/ES/ZH) - reines C, damit main.c ohne Foundation auskommt. |
| `src/gapbuffer.c`/`.h` | Der Gap Buffer selbst (Puffer-Grundlage von editor.c). |
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
| In der Suchleiste: Ersetzen+Weiter / Alle ersetzen | `Return` (im Ersetzen-Feld) / `⌘Return` |
| Suchleiste schließen | `Esc` |
| Wort-/Zeilensprung, Zeilenanfang/-ende | `⌥←/→`, `⌘←/→`, Pos1/Ende |
