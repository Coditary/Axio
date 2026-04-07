# Umsetzungsstand

Diese Datei beschreibt den aktuell umgesetzten Ist-Zustand der Sprache und des Compilers. Sie beschreibt bewusst nicht die langfristige Zielvision, sondern nur das, was momentan bereits parsebar, semantisch validiert oder codegenerierbar ist.

## Compiler allgemein

- Der Compiler kann `.ax`-Dateien einlesen und zu LLVM-IR, Objektdateien und nativen Binaries verarbeiten.
- Es gibt einen `--check-only` Modus fuer Frontend-Pruefung ohne Codegen.
- Es gibt `--emit-llvm-only` zum Erzeugen von `.ll` Dateien.
- Diagnostics mit Dateipfad, Zeile, Spalte und Markierung sind implementiert.

## Projektstruktur

- Lexer, Parser, Modul-Loading, Sema, Meta und Codegen sind als getrennte Verantwortungsbereiche organisiert.
- Das Importsystem ist nicht mehr header-artig modelliert, sondern als Modul-Loader mit expliziten Interfaces.

## Top-Level Deklarationen

Aktuell unterstuetzt die Sprache auf Top-Level:

- `fn`
- `extern fn`
- `struct`
- `enum`
- `class`
- `import`
- `let`
- `const`
- `pub` vor Deklarationen
- Annotationen wie `@inline`

Beispiele:

```axio
pub const answer int = 42

pub fn add(a int, b int) int {
    return a + b
}

struct Point {
    x int
    y int
}
```

## Funktionen

Bereits implementiert:

- normale Funktionsdefinitionen mit `fn`
- `extern fn`
- Parameter mit Typen
- `const` Parameter
- einzelne Rueckgabewerte
- mehrere Rueckgabewerte
- leere Rueckgabe mit `return`
- Compile-Parameter in `{}`
- Methoden in Klassen
- impliziter `self` Parameter bei Methoden

Beispiele:

```axio
fn add(a int, b int) int {
    return a + b
}

fn pair() (int, int) {
    return 1, 2
}

fn add{lhs int}(rhs int) int {
    return lhs + rhs
}
```

## Variablen und Bindings

Bereits implementiert:

- lokale `let` Bindings
- lokale `const` Bindings
- globale `let` Deklarationen
- globale `const` Deklarationen
- Typangabe optional, wenn ein Initializer vorhanden ist
- Destructuring fuer Multi-Return

Beispiele:

```axio
let x int = 1
const y int = 2

let left int, right int = pair()
```

Semantik:

- `const` lokale Variablen duerfen nicht neu zugewiesen werden
- `const` Parameter duerfen nicht neu zugewiesen werden
- globale `const` Werte duerfen nicht neu zugewiesen werden

## Importsystem und Module

Aktuell umgesetztes Modell:

- jede Datei deklariert ihren Paketpfad explizit:

```axio
package math.ops
```

- `import foo.bar`
  - importiert das Paket
  - exportierte Namen stehen danach direkt lokal zur Verfuegung
  - z. B. kann `add(...)` direkt verwendet werden
- der Paketpfad bleibt zusaetzlich fuer qualifizierte Nutzung verfuegbar
  - z. B. `geom.point.Point`
- Import-Block-Syntax ist implementiert:

```axio
import (
  math.ops
  pt geom.point
)
```

- Aliase sind implementiert
  - `pt geom.point`
- `import foo.bar{Name}` ist weiterhin vorhanden
  - fuer gezielte selektive Imports
- `pub import foo.bar{Name}`
  - reexportiert explizit importierte oeffentliche Namen weiter nach oben
- nur `pub` Deklarationen gehoeren zur exportierten Paket-Schnittstelle
- private Symbole koennen nicht von aussen importiert werden

Beispiele:

```axio
package app.main

import (
  math.ops
  pt geom.point
)

fn main() int {
    let p pt.Point = pt.Point(20, 22)
    return add(p.x, p.y)
}
```

```axio
pub import math.ops{add}
```

## `pub` Sichtbarkeit

Bereits implementiert fuer:

- `fn`
- `struct`
- `enum`
- `class`
- `let`
- `const`
- `import` als Reexport-Fall

Semantik:

- `pub` Symbole koennen von anderen Modulen ueber das Interface gesehen werden
- nicht-`pub` Symbole bleiben privat im Modul

## Ausdruecke

Aktuell parsebar und weitgehend semantisch/codegen-seitig unterstuetzt:

- Integer-Literale
- Float-Literale
- Bool-Literale
- Char-Literale
- String-Literale
- `null`
- Variablenreferenzen
- Memberzugriff `a.b`
- null-sicherer Memberzugriff `a?.b`
- Funktionsaufrufe
- Methodenaufrufe
- Compile-Calls mit `$name(...)`
- Initializer-Aufrufe fuer Typen
- Array-Literale als AST-Form
- Pipe-Syntax `x->f`
- Zuweisung `=`
- Vergleichsketten
- Bereichsausdruecke `a..b` und `a..=b`

## Operatoren

Bereits implementiert im Parser, grossenteils auch in Sema und Codegen:

- Arithmetik: `+ - * / %`
- Bitweise Operatoren: `& | ^ ~ << >>`
- Logik: `&& || !`
- Vergleiche: `== != < <= > >=`
- Bereichsmitgliedschaft: `in`
- Enum-Operationen: `set`, `unset`, `toggle`, `is`, `isnot`
- Adressoperator: `&value`
- Dereferenzierung: `*ptr`

Beispiele:

```axio
let sum int = a + b
if x < y >= 1 {
    return 1
}
if value in 1..=5 {
    return 1
}
```

## Nullability

Bereits vorhanden:

- `null` Literal
- `if ptr? { ... }`
- `ptr?.member`
- `ptr?.call()`

Die Sema warnt bereits in einigen Faellen, wenn null-sichere oder nullable Formen auf nicht-nullable Werte angewandt werden.

## Structs

Bereits implementiert:

- `struct Name { ... }`
- Felder mit Typ
- Feld-Defaultwerte im AST/Parser
- Initialisierung per `Type(...)`
- Feldzugriff per `value.field`
- LLVM-Lowering fuer Struct-Layouts

Beispiel:

```axio
struct Point {
    x int
    y int
}
```

## Enums

Bereits implementiert:

- normale Enums
- Enum-Parameter-Metadaten
- Flag-Enums `as Flags`
- konstante Enum-Werte in Sema
- Flag-Initialisierung ueber `EnumType{...}`
- Enum-Mitgliedszugriff wie `Color.Green`
- Enum-Bereiche in `in`-Ausdruecken

Beispiele:

```axio
enum Color() {
    Red,
    Green,
    Blue,
}

enum ActorState as Flags {
    IsAlive,
    IsVisible,
}
```

## Klassen

Bereits implementiert:

- `class Name { ... }`
- normale Felder
- dynamische Felder mit `=>`
- Methoden
- Struct-Includes in Klassen
- Memberzugriff auf Felder
- Methodenaufruf
- implizite Sichtbarkeit von Klassenfeldern in Methoden und dynamischen Feldern

Beispiele:

```axio
class User {
    name str
    display str => "Hi " + name

    fn greet() str {
        return display
    }
}
```

## Ownership-Formen und Objektinitialisierung

Bereits parsebar und teilweise semantisch/codegen-seitig unterstuetzt:

- Stack/Value-Initializer: `Type(...)`
- ARC-Heap-Initializer: `new Type(...)`
- Weak-Initializer: `new weak Type(...)`
- Unique-Initializer: `*Type(...)`
- `ref`, `weak`, `*` in Typen

Es gibt bereits Ownership-Pruefungen fuer mehrere problematische Faelle, z. B.:

- doppelte Rueckgabe eines Unique-Wertes
- ungueltige Zuweisung von `ref` in owning/value storage
- ungueltige Weak-Bindings aus Value-Initializern

## Statements

Bereits implementiert:

- Block `{ ... }`
- `return`
- `if`
- `else if`
- `else`
- lokale `let`/`const` Statements
- Expression-Statements

## Metaprogrammierung und Annotationen

Bereits vorhanden:

- Annotationen wie `@inline`
- unbekannte Annotationen werden diagnostiziert
- Compile-Funktionen mit `$...`
- aktuell validiert:
  - `$readfile(...)`
  - `$generate_open_api(...)` als erkannte Form im Compilerpfad
- Dialektbloecke wie `%%SQL{ ... }%%` werden geparsed

Wichtig:

- Compile-Funktionen und Dialektbloecke sind noch nicht voll ausgebaut
- Dialektbloecke werden aktuell diagnostisch erkannt, aber noch nicht zu Runtime-Werten lowered

## Codegen-Stand

Aktuell vorhanden:

- LLVM-IR fuer Funktionen
- LLVM-IR fuer Structs und Klassenlayouts
- LLVM-IR fuer globale Variablen und globale `const`
- LLVM-IR fuer Funktionsaufrufe, Methodenaufrufe, Memberzugriffe und viele Operatoren
- Linking ueber `clang++`

Einschraenkungen des aktuellen Prototyps:

- manche Sprachformen sind bereits parsebar oder semantisch erkannt, aber noch nicht in allen Faellen vollstaendig lowered
- besonders Dialektbloecke und einige fortgeschrittene Meta-/Sprachfeatures sind noch nicht vollstaendig umgesetzt

## Was derzeit bewusst noch unvollstaendig oder prototypisch ist

- das Sprachdesign geht weiter als die aktuelle Implementierung
- einige Features sind parserseitig schon sichtbar, aber noch nicht komplett bis ins Backend getragen
- Diagnostics fuer mehrere Dateien verwenden noch kein vollstaendig globales Source-Mapping-Modell
- Teile von `Sema/Expr` und `Codegen/Expr` sind funktional, aber noch groessere Dateien und damit weitere Refactor-Kandidaten

## Kurzfazit

Momentan kann man bereits mit Axio:

- Funktionen definieren und aufrufen
- Multi-Return verwenden
- `let` und `const` nutzen
- Module importieren, selektiv importieren und reexportieren
- `pub` Sichtbarkeit verwenden
- Structs, Enums und Klassen definieren
- Methoden, Memberzugriffe und null-sichere Zugriffe nutzen
- Ownership-nahe Initializer wie `new`, `new weak` und `*Type()` schreiben
- Compile-Argument-Aufrufe und Pipe-Ausdruecke verwenden
- Code bis zu LLVM-IR und nativen Artefakten kompilieren

Fuer einen Prototyp ist der Frontend- und Modulstand bereits recht weit, auch wenn noch nicht jede Zielidee der Sprache vollstaendig bis zur finalen Backend-Semantik umgesetzt ist.
