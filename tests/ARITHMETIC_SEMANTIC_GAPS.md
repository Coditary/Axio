## Arithmetic Semantic Gaps

Diese Notiz dokumentiert Stellen, die beim Umbau von `tests/language/semantic/arithmetic` auf echte `--check-only`-Pruefungen sichtbar wurden.

### Aktuell noch nicht als Sema-Regel vorhanden

- Division und Modulo durch Null werden im Frontend derzeit nicht diagnostiziert.
- Die bisherigen Dateien `division/**/*zero_error.ax` und `modulo/**/*zero_error.ax` pruefen deshalb momentan nur, dass `--check-only` keinen Fehler meldet.
- Gewuenschte spaetere Zielmeldungen waeren z. B. `error: division by zero` und `error: modulo by zero`.

### Aktuell noch nicht sauber im Lexer modelliert

- Hex- und Binaerliterale wie `0xff` und `0b1010` werden noch nicht als einzelne Integer-Literale erkannt.
- Im aktuellen Stand entstehen daraus Sema-Fehler wie `unknown symbol 'xff'`, `unknown symbol 'b1100'` und `unknown symbol 'b1010'`.
- Die betroffenen Literal-Tests pruefen deshalb exakt diese heutigen Diagnosen.

### Aktuell noch nicht sauber im Sema-Typsystem normalisiert

- Typalias-Namen wie `byte`, `short`, `long`, `ubyte`, `uchar`, `ushort`, `uint`, `ulong`, `u2` und `f8` sind in Lexer/Parser sichtbar, werden in `Sema` aber nicht vollstaendig wie ihre Kern-Skalartypen behandelt.
- Dadurch schlagen viele Initialisierungen wie `let a long = 3` derzeit mit `error: initializer type does not match declared type` fehl.
- Die entsprechenden Arithmetic-Tests pruefen deshalb heute auf genau diese existierende Diagnose.

### Teststrategie im aktuellen Umbau

- Erfolgsfaelle in `tests/language/semantic/arithmetic` verwenden jetzt `--check-only` plus `// CHECK-NOT: error:`.
- Fehlerfaelle pruefen die konkrete aktuelle Frontend-/Sema-Diagnose.
- Es wurde bewusst kein Testcode geaendert, sondern nur die `// RUN:`- und `// CHECK:`-Kommentarabfragen.
