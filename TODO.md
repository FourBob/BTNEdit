# Offene Punkte

Wird laufend aktualisiert - neue Punkte kommen dazu, erledigte werden entfernt
(nicht nur abgehakt liegen gelassen).

## Bekannte Einschränkungen (bewusst zurückgestellt, kein akuter Bug)

- Cursor-/Selektions-/Hit-Testing-Spaltenrechnung zählt seit dem Codepoint-Fix
  (`editor_visual_column_in_range`) jedes Zeichen als genau EINE Spalte -
  korrekt für Umlaute/Akzente & die meisten Emoji. Für tatsächlich
  **doppelbreite** Zeichen (CJK-Schriftzeichen, manche Emoji) stimmt das aber
  weiterhin nicht exakt mit der von Menlo tatsächlich gerenderten Breite
  überein (die App kennt keine East-Asian-Width-Klassifizierung) - Cursor-
  Darstellung/Klick-Position kann bei solchem Text leicht abweichen.
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
