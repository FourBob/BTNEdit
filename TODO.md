# Offene Punkte

Wird laufend aktualisiert - neue Punkte kommen dazu, erledigte werden entfernt
(nicht nur abgehakt liegen gelassen).

## Von der ursprünglichen Feature-Liste noch nicht umgesetzt

- **Drucken** - `BTN_MENU_PRINT` ist aktuell nur ein Stub (druckt eine
  Debug-Meldung). Müsste über `NSPrintOperation` gehen (reine Chrome wie
  `NSWindow`, kein Content-Widget - passt zur bestehenden Architektur).
- **Hilfe/Shortcuts-Übersicht** - noch kein eigenes Panel/Fenster dafür.

## Bekannte Einschränkungen (bewusst zurückgestellt, kein akuter Bug)

- Cursor-/Selektions-/Hit-Testing-Spaltenrechnung (`editor_visual_column_in_range`
  & co.) zählt UTF-8-**Bytes**, nicht Codepoints - bei nicht-ASCII-Text
  (Umlaute, Emoji, CJK) kann die Cursor-Darstellung/Klick-Position von der
  visuellen Spalte leicht abweichen. Durchgängig auf Codepoints umzustellen
  wäre eine größere, zusammenhängende Änderung an mehreren Stellen.
- Klammer-Matching/-Hervorhebung (`editor_find_matching_bracket`) kennt keine
  Strings/Kommentare - eine Klammer innerhalb eines String-Literals oder
  Kommentars kann in seltenen Fällen einen inhaltlich falschen (aber stets
  wohldefinierten) Treffer liefern. Für eine echte Lösung müsste das mit
  highlight.c's Tokenizer zusammenspielen.
- Auto-Vervollständigung nur für `()`, `[]`, `{}` - keine Anführungszeichen
  (andere Regeln: z.B. nicht verdoppeln, wenn schon in einem String), kein
  `<>` (zu häufig ein Vergleichsoperator in Code, bräuchte Sprach-Kontext).
- Tableiste schrumpft bei vielen Tabs proportional (bis 40pt Minimum), hat
  aber kein echtes horizontales Scrollen - bei sehr schmalen Fenstern
  kombiniert mit sehr vielen Tabs (nahe `MAX_TABS=20`) können Tabs trotzdem
  noch unerreichbar werden.
- Die Suchen/Ersetzen-Felder sind bewusst einfache Anhängen/Löschen-Felder
  (kein Mini-Editor) - keine Cursor-Navigation, keine Selektion, kein
  Copy/Paste innerhalb der Felder selbst.
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
