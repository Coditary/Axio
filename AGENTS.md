# AGENTS.md

Dieses Dokument beschreibt die verbindliche Zielrichtung fuer Axio. Es richtet sich an Menschen und AI-Agents, die an Parser, AST, Semantik, Metaprogrammierung, ARC, LLVM-Backend, LSP oder Tooling arbeiten.

Der aktuelle Code ist ein frueher Prototyp. Wenn bestehender Code von diesen Regeln abweicht, gilt fuer neue Arbeit dieses Dokument als Zielzustand.

## 1. Mission

Axio soll eine schnelle, modular aufgebaute Sprache mit LLVM-Backend werden, mit:

- sehr guten Fehlermeldungen
- UTF-8- und Unicode-Unterstuetzung im ganzen Frontend
- klar getrennten Compiler-Phasen
- starker Metaprogrammierung
- C-naher Lesbarkeit bei gleichzeitig moderneren Sprachfeatures
- automatischem Memory Management per ARC

Compiler-Features muessen so implementiert werden, dass neue Features in eigenen Modulen landen koennen und nicht immer dieselben zentralen Dateien anfassen.

## 2. Harte Architekturregeln

- Neue Sprachfeatures niemals direkt nur im LLVM-Emitter "hineinmogeln".
- Jede Sprachfunktion soll, wenn sie nicht rein lexikalisch ist, mindestens diese Stationen sauber haben:
  - Token/Grammar
  - AST
  - Semantik/Typpruefung
  - konstante Auswertung falls sinnvoll
  - Codegen
- Parser, Sema, Meta-Passes und Codegen bleiben getrennt.
- Annotationen, Compile-Funktionen, Preprocessor und Dialekte muessen als eigene erweiterbare Systeme gebaut werden, nicht als Sonderfaelle mitten im Parser.
- UTF-8 ist Standard. Identifier, Strings, Kommentare und Diagnostics muessen Unicode-faehig sein.
- Error-Recovery ist Pflicht. Neue Syntax darf nicht nur beim ersten Fehler abbrechen, sondern soll Mehrfachdiagnosen pro Lauf ermoeglichen.

## 3. Zielsyntax der Sprache

### 3.1 Funktionen

Funktionsdefinitionen beginnen mit `fn`.

Grundform:

```axio
fn add(a int, b int) int {
    return a + b
}
```

Mehrere Rueckgabewerte sind Teil der Sprache:

```axio
fn parse(x str) (error, int) {
    return nil, 0
}
```

Regeln:

- Rueckgabetypen sind entweder ein einzelner Typ oder eine Tupel-Liste in `(...)`.
- `return` darf mehrere Werte zurueckgeben.
- Funktionsaufrufe und Signaturen muessen dieses Mehrfach-Return-Modell nativ unterstuetzen.

### 3.2 Funktionsaufrufe mit Compile-Argumenten in `{}`

Axio unterstuetzt eine zusaetzliche Aufrufsyntax:

```axio
log{3}(x)
log{3, x}()
log(3, x)
log{}(3, x)
```

Regeln:

- Werte in `{}` sind die logisch ersten Argumente.
- Diese Form gilt sowohl fuer Definition als auch fuer Aufruf.
- Der Compiler darf fuer solche Werte spezialisierte Varianten generieren, wenn das Performance bringt.
- Diese Spezialisierung ist eine Optimierung, keine semantische Aenderung.

### 3.3 Variablen

Deklarationen verwenden `let`:

```axio
let x int
let y int = 3
```

Objekt-/Heap-Formen:

```axio
let x = obj()           // stack value
let x = new obj()       // heap ARC
let x = *obj()          // heap unique/ref
let x = new weak obj()  // weak pointer
```

### 3.4 Operatoren

Arithmetisch:

- `+ - * / %`

Bitweise:

- `& | ^ ~ << >>`

Logisch / Vergleich:

- `== != < > <= >= && || !`

Bereichsoperatoren:

- `x in 1..10` exklusives Ende
- `x in 1..=10` inklusives Ende

Chained comparisons sind erlaubt und semantisch ein grosses `&&`:

```axio
x < y >= 5 != 9 < z
```

Diese Ketten duerfen nicht als linksassoziative bool-vs-int Fehlinterpretation implementiert werden. Der Parser/Sema muessen daraus eine Folge sinnvoller Vergleichsknoten machen.

### 3.5 Pipe-Syntax

Funktionspipelines sind Teil der Sprache:

```axio
1->func3->func2->func1
```

Das entspricht semantisch verschachtelten Funktionsaufrufen. Pipeline-Syntax soll auch in Ausdruecken und Zuweisungen funktionieren.

### 3.6 Nullability

- Null-Pointer-Literal ist `null`
- Nullcheck-Kurzform:

```axio
if ptr? {
}
```

- Nullsafe method dispatch:

```axio
ptr?.call()
```

Semantik:

- `ptr?` ist true genau dann, wenn `ptr != null`.
- `ptr?.call()` fuehrt den Aufruf nur aus, wenn `ptr` nicht null ist.

## 4. Structs

Grundform:

```axio
struct Vec3 {
    x float = 0
    y float = 0
    z float = 0
}
```

Regeln:

- Syntax: `struct Name { field Type [= default] }`
- Initialisierung: `Vec3(...)`
- Feldzugriff: `value.field`
- Default-Werte muessen compile-time auswertbar sein.
- Bitgroessenangaben wie `bits 3` sollen spaeter Teil des Typsystems fuer kompakte Layouts sein.
- `align(64)` hinter `struct Name` bestimmt Layout-Anforderungen.

Beispiel Zielsyntax:

```axio
struct Header align(64) {
    kind bits 3
    flags bits 5
}
```

Das braucht spaeter ein eigenes Layout-/ABI-Modul und darf nicht ad hoc im Parser fest verdrahtet werden.

## 5. Enums

### 5.1 Normale Enums

Grundform:

```axio
enum Color() {
    Red,
    Green,
    Blue,
}
```

Regeln:

- Der erste Wert ist `0`, dann `1`, dann `2`, usw.
- Zugriff per Name: `Color.Red`
- Zugriff per Index: `Color(1)`

### 5.2 Parametrisierte Enums

Beispiel:

```axio
enum Test(para1 Meter, para2 Meter) {
    A(Meter, Tar),
    B(Ter, Met),
}
```

Zielverhalten:

- Zugriff auf Metadaten per `Test.A.para1`
- Zugriff ueber Index per `Test(1).para1`
- Parameter gehoeren zum Enum-Metamodell und muessen statisch auswertbar sein
- Speicherlayout soll moeglichst kompakt sein

### 5.3 Enum-Ranges

Ranges muessen auch mit Enum-Werten funktionieren:

```axio
a in Test.A..Test.B
```

Zyklische Bereiche sind erlaubt:

```axio
a in Test.B..Test.A
```

Semantik:

- Nicht rueckwaerts iterieren
- Stattdessen inkrementieren und nach dem letzten Wert bei `0` weitermachen

### 5.4 Flag-Enums

Syntax:

```axio
enum ActorState as Flags {
    IsAlive,
    Team as Flag {
        Neutral,
        Red,
        Blue,
        Green,
    },
    IsVisible,
}
```

Regeln:

- In `Flags`-Enums steht jeder Eintrag fuer mindestens ein Bitsegment
- Verschachtelte `as Flag`-Bereiche sind selbst wieder Flag-Masken
- Verschachtelte normale Enums in einem Flag-Enum sind exklusive Werte innerhalb eines reservierten Segments
- Das Enum-Modell muss Bitbereiche, Breite, Offset und semantische Art speichern

### 5.5 Enum-Arithmetik und Helpers

Der Compiler soll Enum-Operationen soweit wie moeglich zur Compile-Zeit auswerten.

Beispiel:

```axio
let a = ActorState{IsVisible, IsAlive}
```

Das soll zu einer finalen Zahl bzw. Bitmaske gefaltet werden.

Gewuenschte semantische Builtins/Operationen:

- `set`
- `unset`
- `toggle`
- `is`
- `isnot`

Abbildungsidee:

- `unset` -> `& ~mask`
- `set` -> `| mask`
- `toggle` -> `^ mask`
- Flag-Check -> `value & mask == mask`
- Normal-Enum-Setzen -> Segment loeschen und Zielwert setzen

Feldartige Kurzformen sollen moeglich sein:

```axio
if user.Fast {
}

if user == User.Green {
}
```

## 6. Klassen und OOP-Grundlagen

Grundsyntax:

```axio
class User {
    name str

    fn greet() str {
        return "Hi"
    }
}
```

Regeln:

- `class Name { ... }`
- Klassen duerfen Attribute und Methoden enthalten
- Klassen koennen spaeter Struct-Felder uebernehmen
- Wenn bei einer Klasse ein Struct referenziert wird, muessen dessen Felder als Klassenattribute sichtbar sein

### Dynamische Variablen

Syntax:

```axio
class Msg {
    msg str
    print str => "Hallo" + msg
}
```

Semantik:

- `=>` definiert ein abgeleitetes/dynamisches Feld
- Dieses Feld ist keine normale gespeicherte Variable, sondern ein berechneter Wert oder abhaengiges Attribut
- Sema muss Zyklen erkennen

Polymorphie ist noch offen. Agents sollen dafuer noch keine vorschnelle finale Architektur festzurren, aber Erweiterungspunkte offenlassen.

## 7. Memory Management

Axio verwendet kein `malloc` und kein `free` in der Sprache.

Das Modell ist:

- `new T()` -> ARC Heap-Objekt
- `new weak T()` -> Weak Pointer
- `*T()` -> unique/ref/move-basiertes Heap-Objekt
- `T()` ohne `new` -> Stack/Value-Form

Funktionsparameter:

```axio
fn test(x obj)      // ARC
fn test(x ref obj)  // read-only, nicht speichern
fn test(x *obj)     // unique move
```

Regeln:

- ARC ist die Standard-Heap-Semantik
- Weak Pointer duerfen ARC-Zyklen nicht am Leben halten
- `ref` ist nicht besitzend und darf nicht persistiert werden
- `*obj` ist move-only und wird nach Verbrauch freigegeben
- Sema braucht dafuer spaeter Ownership-, Escape- und Store-Regeln

Diese Regeln muessen in einer eigenen Semantik-/Ownership-Schicht modelliert werden, nicht nur im Parser.

## 8. Preprocessor und Compile-Time-System

### 8.1 Preprocessor

Mit `#` markiert:

- `#define`
- `#ifndef`
- weitere klassische Build-/Architektur-Steuerung

Der Preprocessor ist eine eigene Phase vor Parser und Comp-Funktionen.

### 8.2 Compile Functions

Mit `$` markiert:

```axio
$generate_open_api("filepath")
$readfile("file")
```

Ziele:

- zur Compile-Time ausfuehren
- Code, Daten oder AST-Fragmente erzeugen
- Ergebnisse entweder inline einfuegen oder in weitere Compile-Time-Verarbeitung geben
- gute LSP-Integration sicherstellen

Wichtig:

- Compile-Funktionen muessen typisiert und diagnostizierbar sein
- generierter Code braucht Herkunftsinformationen fuer gute Fehlermeldungen
- Ausgabe kann Token, AST, konstante Daten oder Compiler-internes IR sein, aber das muss formal modelliert werden

### 8.3 Annotationen

Beispiele:

- `@ThreadSafe`
- LLVM-nahe Optimierungsannotation
- nutzerdefinierte Annotationen

Regeln:

- Annotationen duerfen AST, Sema oder LLVM beeinflussen
- Annotationen muessen als registrierbare Passes/Handler implementierbar sein
- Jede Annotation braucht klaren Wirkungsbereich und klare Phase

### 8.4 Inline-Dialekte

Syntaxidee:

```axio
%%SQL{
    SELECT * FROM users
}%%
```

Regeln:

- Inline-Dialekte sind geparste eingebettete Fremdsprachen
- Jeder Dialekt braucht einen eigenen Parser/Handler
- Diagnostics muessen sowohl Dialekt-Kontext als auch Ursprungsdatei korrekt referenzieren
- Dialekte duerfen nicht als simple String-Literale mit Sonderlogik modelliert werden

## 9. Unicode und UTF-8

Unicode-Unterstuetzung ist Pflicht.

Agents muessen beachten:

- Source-Text ist UTF-8
- Identifier duerfen Unicode verwenden
- Diagnostics muessen mit Multi-Byte-Zeichen korrekt umgehen
- Lexer darf nicht byte-basierte ASCII-Annahmen fuer Identifier-Grenzen hart verdrahten
- spaeter sind Grapheme fuer Editor/LSP wichtig, intern duerfen Offsets trotzdem bytebasiert bleiben, wenn sauber gemappt wird

## 10. Diagnostics-Regeln

Axio soll bessere Fehler liefern als typische C++-Compiler und mehr Folgediagnosen liefern als ein Compiler, der nach jedem einzelnen Problem sofort stoppt.

Pflicht fuer neue Features:

- praezise Ranges
- klare Hauptbotschaft
- hilfreiche Notes/Follow-ups
- Recovery ohne Diagnosen zu unterdruecken
- wenn moeglich Fix-it-Hinweise

Keine kryptischen Fehlermeldungen. Keine generischen "expected token"-Lawinen ohne Kontext, wenn es vermeidbar ist.

## 11. Performance-Regeln

Ziel ist hohe Compile-Geschwindigkeit. Deshalb:

- re2c fuer den finalen Lexer nutzen
- spaeter Parsing/Sema dateiweise parallelisieren
- konstante Auswertung aggressiv nutzen
- Enum-Arithmetik und Compile-Funktionen so weit wie sinnvoll vorfalten
- String/Identifier-Interning und Arenas frueh einplanen
- IR-/Pass-System fuer inkrementelles Compiling vorbereiten

GPU-Nutzung ist optional und aktuell kein Primärziel. Erst Frontend-Architektur, Caching und Parallelisierung sauber bauen.

## 12. Empfohlene Modulstruktur fuer den naechsten Ausbau

- `Lex/`
  - UTF-8 Lexer
  - re2c grammar
- `Parse/`
  - Expressions
  - declarations
  - dialect entry points
- `AST/`
  - language AST
  - enum metadata nodes
  - ownership/type nodes
- `Sema/`
  - name resolution
  - type checking
  - enum semantics
  - ARC/ownership rules
  - nullability
- `ConstEval/`
  - enum folding
  - compile-time function execution
  - defaults and layout expressions
- `Meta/`
  - preprocessor
  - annotations
  - compile functions
  - dialect registry
- `IR/`
  - optional compiler-eigenes Mid-Level-IR
- `Codegen/`
  - LLVM lowering
  - ARC runtime hooks
  - debug info

## 13. Prioritaetsreihenfolge fuer die Umsetzung

Agents sollen neue Features moeglichst in dieser Reihenfolge ausbauen:

1. neue Grammatik und AST fuer `fn`, `let`, multi-return, Operatoren, `in`-Ranges
2. vollwertige Sema-Schicht einfuehren
3. Enum-System inklusive Flag-Segmente und konstante Faltung
4. Nullability und ARC/ownership-Regeln
5. Klassen und dynamische Felder
6. Preprocessor und Compile-Funktionen
7. Annotation- und Dialekt-System
8. aggressive Performance-Arbeit und inkrementelles Compiling

## 14. Was Agents vermeiden sollen

- Keine Logik nur im Parser verstecken
- Keine Sonderfaelle direkt in `main.cpp`
- Keine semantischen Entscheidungen nur ueber String-Vergleiche im Codegen
- Keine Vermischung von ARC-, ref- und unique-Regeln ohne formale Typ-/Ownership-Darstellung
- Keine Unicode-Scheinunterstuetzung, die intern doch nur ASCII kann
- Keine Feature-Implementierung ohne Tests oder Beispielsyntax

## 15. Mindestanforderung fuer jede groessere Sprach-Erweiterung

Wenn ein Agent ein neues Sprachfeature implementiert, soll die Aenderung idealerweise enthalten:

- Grammatikupdate
- AST-Modell
- Semantikregel
- mindestens einen positiven Beispieltest
- mindestens einen negativen Diagnosetest
- kurze Doku-Aktualisierung

## 16. Kurzfassung

Axio entwickelt sich von einem aktuell C-nahen Prototypen in Richtung einer Sprache mit:

- `fn`-Syntax
- `let`-Deklarationen
- Multi-Return
- starkem Enum- und Flag-System
- ARC + weak + unique/ref
- Compile-Time-Funktionen
- Annotationen
- Inline-Dialekten
- Klassen und dynamischen Feldern
- Unicode by default

Wenn unklar ist, wie ein neues Feature integriert werden soll, dann immer die modularere, besser diagnostizierbare und spaeter parallelisierbare Architektur waehlen.
