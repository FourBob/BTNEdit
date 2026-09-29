# Offene Punkte

Wird laufend aktualisiert - neue Punkte kommen dazu, erledigte werden entfernt
(nicht nur abgehakt liegen gelassen).

## Fehlende Features (nach Priorität)

1. **Code-Editor-Funktionen.** Wortumbruch an/aus.

## Bekannte Einschränkungen (bewusst zurückgestellt, kein akuter Bug)

- Kodierung: nur UTF-8 (mit/ohne BOM), UTF-16 LE/BE, ISO-8859-1 und
  Windows-1252 - kein Mac Roman, kein ISO-8859-15, keine osteuropäischen
  oder asiatischen Kodierungen (Shift-JIS, GB18030, ...). Erkannt wird nur
  per BOM, gültigem UTF-8 und sonst Windows-1252: UTF-16 ohne BOM gilt als
  Binärdatei (Neu öffnen als UTF-16 hilft), ISO-8859-1 wird als
  Windows-1252 erkannt (für Texte ohne die Steuerzeichen 0x80-0x9F
  dasselbe). Umwandeln beim Öffnen und Sichern kostet eine zusätzliche
  Kopie des Dokuments (nicht bei UTF-8). Kaputtes UTF-16 wird roh wie eine
  Binärdatei geöffnet.

- KI-Vervollständigung: Vorschläge sind einzeilig und kommen nur, wenn
  rechts vom Cursor höchstens Leerraum oder Schließendes steht. Der
  Geistertext verdeckt (wie der vorläufige Text einer Eingabemethode) was
  rechts vom Cursor steht und läuft bei langen Vorschlägen über den rechten
  Rand hinaus statt umzubrechen. Kein Schalter pro Sprache. Beim Tippen
  bleiben Fehler still (kein Vorschlag); gemeldet wird nur beim Einschalten,
  Aktualisieren und Testen. Server, Wartezeit und Länge nur über
  `~/.btnedit_ai`, das Modell auch im Menü. Ob ein Modell Fill-in-the-Middle
  kann, rät BTNEdit nur am Namen (`coder`, `codellama`, ...) - im Menü lässt
  sich trotzdem jedes wählen. Die Modellliste zeigt höchstens 100 Einträge und
  wird nur beim Start, Einschalten, Aktualisieren und Testen geholt (nicht nach
  einem `ollama pull` im Hintergrund). Nach einer fehlgeschlagenen Antwort wird für
  denselben Text nicht erneut gefragt (nach einem Abbruch schon). Tabs im
  Vorschlag werden ab Spalte 0 statt ab der Cursor-Spalte ausgerichtet.
  Anfragen und Antworten laufen ohne Größenbegrenzung beim Senden; eine
  Antwort über 1 MB gilt als Fehler. Fließtext oder Code entscheidet nur die
  Dateiendung (kein Schalter pro Dokument); Fließtext kennt nur den Text vor
  dem Cursor, das Satzende erkennt BTNEdit an ". ", "! ", "? ", "…" und 。！？;
  Abkürzungen erkennt es nur an einer festen Liste, einzelnen Buchstaben,
  Zahlen und Punkten im Wort ("z. B.", "3.", "Dr.", "e.g.") - andere
  Abkürzungen gefolgt von einem Großbuchstaben beenden den Vorschlag. Welche Modelle
  kein Fill-in-the-Middle können, merkt sich BTNEdit nur bis zum Beenden.

- Zeilen-Befehle: "Kommentar ein/aus" kennt nur Zeilenkommentare (`//`,
  `#`, `;`) und die Sprache nur über die Dateiendung - unbenannte Dokumente,
  Markdown, SVG, STL, DXF, `.config` (oft XML) und unbekannte Endungen
  bekommen einen Signalton statt Blockkommentaren (`/* */`, `<!-- -->`).
  Eine Zeile, die nur aus dem Kommentarzeichen besteht (`//`), ist nach dem
  Entkommentieren leer und wird beim nächsten ⌘/ als Leerzeile übersprungen. Endet eine Auswahl genau am
  Anfang einer Zeile, gehört diese Zeile nicht dazu (wie beim Einrücken);
  schiebt man so einen Block ans Dokumentende, kann der Rückweg eine
  angehängte Leerzeile anders zuordnen. Die Markierungen für unsichtbare
  Zeichen folgen dem Spaltenraster (ein Zeichen = eine Spalte, siehe unten):
  hinter doppelt breiten CJK-Zeichen, Emoji oder kombinierenden Zeichen
  stehen sie so versetzt wie der Cursor dort. Die unsichtbaren Zeichen zeigen
  kein `\r` (in roh geladenen Dateien mit gemischten Zeilenenden) und kein
  geschütztes Leerzeichen gesondert an.

- Schutz der Arbeit: Änderungen von außen werden beim Aktivieren der App
  (nicht, solange ein Dialog offen ist), alle paar Sekunden (nur Tabs ohne
  eigene Änderungen, still; große Dateien seltener) und vor dem Sichern
  erkannt - nicht sofort per Dateisystem-Benachrichtigung (FSEvents). Weicht
  nur der Datei-Stempel ab (Zeiten, Attribute), liest BTNEdit die Datei
  einmal ganz, um den Inhalt zu vergleichen (bei sehr großen Dateien kurz
  spürbar). Die
  Wiederherstellungsdatei wird im 5-Sekunden-Takt geschrieben und ist bis
  zu etwa 10 Sekunden alt (große Dokumente seltener: pro 20 MB eine Sekunde
  mehr Abstand); was danach getippt wurde, fehlt nach einem Absturz.
  Wiederhergestellt wird nur der Text samt Pfad und Zeilenenden - nicht
  Undo-Verlauf, Cursor oder Scrollposition. Es gibt kein macOS-"Autosave in
  place" und keine Versionen (Ablage > Zurück zu). Ein still neu geladener
  Tab verliert seinen Undo-Verlauf. Lesen, Prüfen und Sichern laufen im
  Haupt-Thread: eine sehr große Datei oder ein hängendes Netzlaufwerk kann
  die Oberfläche kurz blockieren. Fragt BTNEdit nach einem Hintergrund-Tab,
  wird er dafür angezeigt und die Suchleiste geschlossen. "Sichern unter"
  auf eine Datei, die in einem anderen Tab offen ist, wird nicht verhindert
  (Sichern im anderen Tab fragt dann nach).

- Maus: Der Scrollbalken ist immer sichtbar, sobald das Dokument länger als
  das Fenster ist (kein Ein-/Ausblenden wie bei macOS-Overlay-Scrollbars),
  und ein Klick daneben blättert eine Seite (eine Zeile bleibt zur
  Orientierung stehen) - Gedrückthalten
  wiederholt nicht, und die Systemeinstellung "Klicken in die Rollleiste:
  an die angeklickte Stelle springen" wird nicht beachtet. Nach Doppel-/
  Dreifachklick erweitert Ziehen die Auswahl nicht wort- bzw. zeilenweise.
  Text (statt Dateien) lässt sich nicht ins Fenster ziehen, und markierter
  Text lässt sich nicht per Maus verschieben.

- Eingabemethoden: Der vorläufige Text (z.B. Pinyin vor der Auswahl) wird
  als Overlay am Cursor gezeichnet und verdeckt so lange den Text dahinter,
  statt ihn wie in TextEdit zur Seite zu schieben. Positionen gibt der
  Editor macOS nur relativ zur aktuellen Zeile (höchstens 1024 Zeichen vor
  dem Cursor) - reicht für Tottasten, Kandidaten, Emoji und das Akzent-Menü,
  aber Funktionen, die weiter entfernten Text brauchen (Rückumwandlung
  bereits eingefügter Kanji), gehen nicht. Gedrückthalten eines Buchstabens
  öffnet (macOS-Standard) das Akzent-Menü statt die Taste zu wiederholen;
  wer Wiederholung will: `defaults write <bundle-id> ApplePressAndHoldEnabled
  -bool false`.
- Einrücken: Die Klammern vor dem Cursor werden ohne Sprachwissen gezählt -
  eine `{` in einem String oder Kommentar rückt beim Return ebenfalls ein,
  und ein `}` sucht seine `{` auch durch Strings/Kommentare (höchstens 1 MB
  zurück). Python rückt nach `return`/`pass` nicht automatisch aus, `else:`
  und `except:` rücken nicht automatisch zurück. Die Stil-Erkennung (Tab
  oder Leerzeichen) liest nur das erste MB der Datei.
- Zeilenenden: Dateien mit gemischten Zeilenenden (z.B. Logs mit
  Fortschrittszeilen, Patches mit einzelnen CRLF-Zeilen) und Binärdateien
  bleiben Byte für Byte, wie sie sind - ein `\r` darin wird dann wie bisher
  als eigenes (unsichtbares) Zeichen angezeigt, Statuszeile "gemischt". Erst
  eine Wahl in Ablage > Zeilenenden vereinheitlicht (ein Undo-Schritt; ein
  Undo stellt den Text wieder her, gesichert wird aber im gewählten Format).
  Bei Binärdateien ist das Menü gesperrt.
- Eine Datei knapp unter 1 GB, als CRLF gesichert, kann über die
  Öffnen-Grenze (`BTN_MAX_FILE_MB`) wachsen und lässt sich dann nicht mehr
  öffnen; es gibt keine Warnung beim Sichern.
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
- Ein NUL-Byte im Suchbegriff (z.B. per Cmd+E aus einer Binärdatei
  übernommen) steht zwar vollständig im Suchfeld, die Suche selbst endet aber
  dort: `regcomp` arbeitet mit C-Strings.

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
  funktion nicht vorgesehen (die letzte Wiederherstellungsdatei wird beim
  nächsten Start angeboten). Nur der Undo-Verlauf behandelt es weich: er
  wird dann verworfen, die Bearbeitung selbst bleibt.
- Live-Suche im Regex-Modus fällt aus, wenn verschachtelte/verkettete
  `{n,m}` zusammen mehr als 1000 Kopien eines Teilausdrucks ergäben
  (`regex_too_expensive_for_live_search`, Apples TRE kopiert den Teilbaum pro
  Wiederholung). Return sucht weiterhin ohne Deckel. Andere teure Muster
  (sehr viele Alternativen o.ä.) sind nicht abgedeckt.
- Alert-Titel mit einem Dateinamen ohne gültiges UTF-8 fallen als Ganzes auf
  Latin-1 zurück - die übersetzten Anführungszeichen erscheinen dann als
  `â€œ`. Nur bei Dateinamen von SMB/NFS/FAT-Volumes.
- Suche über alle Tabs: nur offene Tabs (kein Ordner/Projekt). Die Summe
  im Status zeigt die Live-Suche nur, solange die anderen Tabs zusammen
  höchstens 2 MB haben (sonst erst nach Return); das erste Return mit einem
  neuen Suchbegriff kopiert und durchsucht alle Tabs (danach gemerkt, bis
  sich Begriff oder Tab ändern). Wie im einzelnen Tab zählen und erreicht
  die Navigation höchstens die ersten 5000 Treffer je Tab. Der Umschalter
  wird nicht über Neustarts gemerkt; Treffer werden nur im aktiven Tab
  hervorgehoben.
- Suchen-/Ersetzen-Felder scrollen nicht horizontal: sehr langer Text läuft
  über das 200pt-Feld hinaus in die Umschalter.

## Reuse/Efficiency-Findings aus Code-Reviews, nicht behoben (niedrige Priorität)

- Mehrere unabhängige "finde Zeilengrenzen"-Scanner in editor.c/render.c
  könnten sich eine gemeinsame Funktion teilen.
- render.c's `utf8_safe_cut`/`utf8_prefix_bytes` (Kürzen der Tab-
  Beschriftung, arbeiten auf C-Strings statt auf dem Editor-Puffer) prüfen
  nur Fortsetzungsbytes statt `btn_utf8_char_len` zu nutzen - für die
  gültigen UTF-8-Dateinamen, die dort ankommen, dasselbe Ergebnis.
