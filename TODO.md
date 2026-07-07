# Offene Punkte

Wird laufend aktualisiert - neue Punkte kommen dazu, erledigte werden entfernt
(nicht nur abgehakt liegen gelassen).

## Gewünschte Verbesserungen

- Suchen: Anzeige der Anzahl der Fundstellen (z.B. "3 von 12 Treffern"
  in der Statusanzeige der Suchleiste).
- Suchen/Ersetzen: Anzeige des Ergebnisses des Ersetzens der ersten
  Fundstelle (Rückmeldung, dass/was ersetzt wurde), nicht nur bei
  "Alle ersetzen".

## Bekannte Einschränkungen (bewusst zurückgestellt, kein akuter Bug)

- Cursor-/Selektions-/Hit-Testing-Spaltenrechnung (`editor_visual_column_in_range`
  & co.) zählt UTF-8-**Bytes**, nicht Codepoints - bei nicht-ASCII-Text
  (Umlaute, Emoji, CJK) kann die Cursor-Darstellung/Klick-Position von der
  visuellen Spalte leicht abweichen. Durchgängig auf Codepoints umzustellen
  wäre eine größere, zusammenhängende Änderung an mehreren Stellen.
- Klammer-/Anführungszeichen-Matching (`editor_find_matching_bracket`) kennt
  keine Strings/Kommentare - eine Klammer oder ein Anführungszeichen
  innerhalb eines String-Literals oder Kommentars kann in seltenen Fällen
  einen inhaltlich falschen (aber stets wohldefinierten) Treffer liefern.
  Für eine echte Lösung müsste das mit highlight.c's Tokenizer
  zusammenspielen.
- Anführungszeichen-Matching kennt zusätzlich kein Escaping - ein `\"`
  mitten in einem String zählt als eigenständiges Anführungszeichen statt
  ignoriert zu werden.
- Auto-Vervollständigung nur für `()`, `[]`, `{}`, `""`, `''` - kein `<>`
  (zu häufig ein Vergleichsoperator in Code, bräuchte Sprach-Kontext, um
  zwischen Vergleich und generischem/Tag-artigem Gebrauch zu unterscheiden).
- Tableiste schrumpft bei vielen Tabs proportional (bis 40pt Minimum), hat
  aber kein echtes horizontales Scrollen - bei sehr schmalen Fenstern
  kombiniert mit sehr vielen Tabs (nahe `MAX_TABS=20`) können Tabs trotzdem
  noch unerreichbar werden.
- Klick in ein Suchen/Ersetzen-Feld fokussiert es nur, positioniert den
  Cursor aber nicht an der Klickstelle (dafür bräuchte main.c die
  Zeichenbreite aus render.c, die dort bisher privat ist) - Tastatur-
  Navigation (Pfeiltasten, Pos1/Ende, Shift-Selektion, Copy/Paste) ist voll
  unterstützt, Mausklick mittendrin noch nicht.
- Eingebettete NUL-Bytes in einer Selektion können das Vorbefüllen des
  Suchfelds (Cmd+F übernimmt die aktuelle Selektion) an der Stelle des
  NUL-Bytes abschneiden (`strlen`/`strchr` auf einem Puffer, der theoretisch
  eingebettete NULs enthalten kann).
- Undo/Redo beim Klammern-Umschließen einer Selektion erzeugt 3 einzelne
  Undo-Schritte (öffnende Klammer, wiedereingefügter Text, schließende
  Klammer) statt einem zusammengefassten - ein Undo braucht dafür 3x Cmd+Z.

## Reuse/Efficiency-Findings aus Code-Reviews, nicht behoben (niedrige Priorität)

- Mehrere unabhängige "finde Zeilengrenzen"-Scanner in editor.c/render.c
  könnten sich eine gemeinsame Funktion teilen.
- `utf16_offset_for_byte_offset` (render.c) und `utf8_forward_len`/
  `utf8_backward_len` (editor.c) sind zwei unabhängige UTF-8-Implementierungen.
- `perform_replace_all()` kopiert bei jedem gefundenen Treffer das komplette
  Dokument neu (`editor_copy_all`) statt einmal vorab - O(Treffer × Länge)
  statt O(Länge). Für die Zielgröße "schlanker Editor" bisher unauffällig.
- render.c erzeugt Farben/`CFDictionaryRef`-Attribute für Tab-/Suchleiste bei
  jedem Redraw neu, statt sie wie `get_token_color()` es für Syntax-Farben tut
  zu cachen.
- `main.c`s `on_draw()` baut bei jedem Redraw (jeder Tastendruck) alle
  Tab-Labels neu auf, auch die von unveränderten Hintergrund-Tabs.
