# Offene Punkte

Wird laufend aktualisiert - neue Punkte kommen dazu, erledigte werden entfernt
(nicht nur abgehakt liegen gelassen).

## Bekannte Einschränkungen (bewusst zurückgestellt, kein akuter Bug)

- Jedes Zeichen (gültige UTF-8-Sequenz, sonst ein einzelnes Byte als
  Latin-1, siehe `btn_utf8_char_len`) ist genau EINE Spalte - Cursor,
  Umbruch und Zeichnen sind sich darin einig, auch bei Latin-1-Dateien.
  Für tatsächlich **doppelbreite** Zeichen (CJK-Schriftzeichen, manche
  Emoji) stimmt das aber nicht exakt mit der von Menlo gerenderten Breite
  überein (die App kennt keine East-Asian-Width-Klassifizierung) - Cursor-
  Darstellung/Klick-Position kann bei solchem Text leicht abweichen.
- Ende/Cmd+Rechts, Auf/Ab und ein Klick hinter das Ende einer umgebrochenen
  Row setzen den Cursor vor das letzte Zeichen der Row (bei einem Umbruch an
  einem Leerzeichen also direkt hinter das letzte Wort). Bei einem
  erzwungenen Umbruch mitten in einem überlangen Wort steht er damit ein
  Zeichen vor dem Row-Ende; exakt wäre nur eine Cursor-"Affinität"
  (oberhalb/unterhalb der Umbruchgrenze).
- Regex-Suche arbeitet byteweise (C-Locale, kein `setlocale`): `.` oder
  `[^a]` treffen ein einzelnes Byte eines mehrbytigen Zeichens. Treffer
  werden deshalb auf ganze Zeichen erweitert (`regex_search_from`), damit
  Selektion und Cursor nie mitten in einem Zeichen landen; bei einem so
  erweiterten Treffer sind die Gruppen `$1..$9` beim Ersetzen leer, `$0` ist
  der ganze Treffer. `.` auf "ä" trifft also "ä", `(.)` → `$1` aber nichts.
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

- Sichern ist atomar (Tempdatei + `rename()`), damit ein Schreibfehler nie
  das Original zerstört. Nebenwirkung: die Datei bekommt eine neue Inode -
  Hardlinks auf die Datei werden getrennt, erweiterte Attribute (Finder-Tags,
  Quarantäne-Flag, ACLs) und das Erstellungsdatum gehen verloren. Symlinks
  werden aufgelöst (das Ziel wird geschrieben, der Link bleibt), und
  schreibgeschützte Dateien werden weiterhin abgelehnt.

- Dateien über 1 GB (`BTN_MAX_FILE_MB`) werden mit einer Meldung
  abgelehnt; der Inhalt liegt sonst mehrfach im Speicher (Gap-Buffer, Kopien
  für Suche/Sichern, Row-Layout). Geht beim Bearbeiten trotzdem der Speicher
  aus, bricht die App im Gap-Buffer bzw. Layout mit einer Meldung ab
  (`btn_xmalloc`) - dort ist ein sauberer Rückweg durch jede Bearbeitungs-
  funktion nicht vorgesehen. Nur der Undo-Verlauf behandelt es weich: er
  wird dann verworfen, die Bearbeitung selbst bleibt.
- Live-Suche im Regex-Modus fällt für Muster mit `{n,m}` über 64 oder
  verschachtelten/verketteten `{..}` aus (`regex_too_expensive_for_live_search`,
  Apples TRE kopiert den Teilbaum pro Wiederholung). Return sucht weiterhin
  ohne Deckel. Andere teure Muster (viele Alternativen o.ä.) sind nicht
  abgedeckt.
- Suchen-/Ersetzen-Felder scrollen nicht horizontal: sehr langer Text läuft
  über das 200pt-Feld hinaus in die Umschalter.

## Reuse/Efficiency-Findings aus Code-Reviews, nicht behoben (niedrige Priorität)

- Mehrere unabhängige "finde Zeilengrenzen"-Scanner in editor.c/render.c
  könnten sich eine gemeinsame Funktion teilen.
- render.c's `utf8_safe_cut`/`utf8_prefix_bytes` (Kürzen der Tab-
  Beschriftung, arbeiten auf C-Strings statt auf dem Editor-Puffer) prüfen
  nur Fortsetzungsbytes statt `btn_utf8_char_len` zu nutzen - für die
  gültigen UTF-8-Dateinamen, die dort ankommen, dasselbe Ergebnis.
