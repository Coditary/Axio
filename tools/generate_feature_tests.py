#!/usr/bin/env python3

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests" / "generated"


def write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def make_runtime_test(name: str, expr: str, expected: int) -> None:
    write(
        OUT / "runtime" / f"{name}.ax",
        f"""// RUN: %axc %s -o %tbin
// RUN: %tbin; test \"$?\" = \"{expected}\"

fn main() int {{
    return {expr}
}}
""",
    )


def make_runtime_if_test(
    name: str, condition: str, when_true: int, when_false: int, expected: int
) -> None:
    write(
        OUT / "runtime" / f"{name}.ax",
        f"""// RUN: %axc %s -o %tbin
// RUN: %tbin; test \"$?\" = \"{expected}\"

fn main() int {{
    if {condition} {{
        return {when_true}
    }} else {{
        return {when_false}
    }}
}}
""",
    )


def make_parse_type_test(type_name: str) -> None:
    safe = (
        type_name.replace("[", "_")
        .replace("]", "_")
        .replace("*", "ptr")
        .replace(" ", "_")
    )
    write(
        OUT / "parse" / f"type_{safe}.ax",
        f"""// RUN: %axc --dump-ast %s | %filecheck %s

fn main(value {type_name}) int {{
    return 0
}}

// CHECK: Param value:{type_name}
""",
    )


def make_literal_parse_test(
    name: str, declared_type: str, initializer: str, check: str
) -> None:
    write(
        OUT / "parse" / f"literal_{name}.ax",
        f"""// RUN: %axc --dump-ast %s | %filecheck %s

fn main() int {{
    let value {declared_type} = {initializer}
    return 0
}}

// CHECK: {check}
""",
    )


def make_negative_let_test(index: int) -> None:
    write(
        OUT / "sema" / f"let_missing_init_{index:03}.ax",
        f"""// RUN: not %axc --check-only %s 2>&1 | %filecheck %s

fn main() int {{
    let missing_{index}
    return 0
}}

// CHECK: error: let declaration needs either a type or an initializer
""",
    )


def make_negative_compile_call_test(index: int) -> None:
    write(
        OUT / "sema" / f"compile_call_bad_arg_{index:03}.ax",
        f"""// RUN: not %axc --check-only %s 2>&1 | %filecheck %s

fn main() int {{
    let value str = $readfile({index})
    return 0
}}

// CHECK: error: $readfile expects a leading string literal argument
""",
    )


def make_codegen_type_test(name: str, return_type: str, expr: str, needle: str) -> None:
    write(
        OUT / "codegen" / f"{name}.ax",
        f"""// RUN: %axc --emit-llvm-only %s -o %t 2>/dev/null
// RUN: %filecheck %s < %t.ll

fn main() {return_type} {{
    return {expr}
}}

// CHECK: {needle}
""",
    )


def main() -> None:
    runtime_cases = []

    for lhs, rhs in [(0, 0), (0, 7), (7, 0), (5, 4), (7, 7)]:
        runtime_cases.append((f"add_{lhs}_{rhs}", f"{lhs} + {rhs}", lhs + rhs))
        runtime_cases.append(
            (
                f"sub_{lhs}_{rhs}",
                f"{lhs} - {rhs}",
                max(0, lhs - rhs) if lhs - rhs >= 0 else (lhs - rhs) & 255,
            )
        )

    mul_pairs = [(1, 1), (2, 3), (3, 4), (4, 5), (6, 7)]
    for lhs, rhs in mul_pairs:
        runtime_cases.append((f"mul_{lhs}_{rhs}", f"{lhs} * {rhs}", lhs * rhs))

    div_pairs = [(8, 2), (9, 2), (15, 3), (21, 7)]
    for lhs, rhs in div_pairs:
        runtime_cases.append((f"div_{lhs}_{rhs}", f"{lhs} / {rhs}", lhs // rhs))

    mod_pairs = [(8, 3), (9, 2), (15, 4), (21, 5)]
    for lhs, rhs in mod_pairs:
        runtime_cases.append((f"mod_{lhs}_{rhs}", f"{lhs} % {rhs}", lhs % rhs))

    bit_pairs = [(0, 0), (1, 0), (6, 3), (7, 2), (15, 4)]
    for lhs, rhs in bit_pairs:
        runtime_cases.append((f"and_{lhs}_{rhs}", f"{lhs} & {rhs}", lhs & rhs))
        runtime_cases.append((f"or_{lhs}_{rhs}", f"{lhs} | {rhs}", lhs | rhs))
        runtime_cases.append((f"xor_{lhs}_{rhs}", f"{lhs} ^ {rhs}", lhs ^ rhs))

    shift_pairs = [(1, 0), (1, 1), (2, 2), (3, 1), (7, 2)]
    for lhs, rhs in shift_pairs:
        runtime_cases.append((f"shl_{lhs}_{rhs}", f"{lhs} << {rhs}", lhs << rhs))
        runtime_cases.append((f"shr_{lhs}_{rhs}", f"{lhs} >> {rhs}", lhs >> rhs))

    comparisons = [
        ("eq", "==", lambda a, b: int(a == b)),
        ("ne", "!=", lambda a, b: int(a != b)),
        ("lt", "<", lambda a, b: int(a < b)),
        ("le", "<=", lambda a, b: int(a <= b)),
        ("gt", ">", lambda a, b: int(a > b)),
        ("ge", ">=", lambda a, b: int(a >= b)),
    ]
    for prefix, op, fn in comparisons:
        for lhs, rhs in [(0, 0), (1, 2), (2, 1), (3, 3), (5, 7)]:
            runtime_cases.append(
                (f"{prefix}_{lhs}_{rhs}", f"{lhs} {op} {rhs}", fn(lhs, rhs))
            )

    logic_cases = [
        ("logic_and_1", "1 && 1", 1),
        ("logic_and_2", "1 && 0", 0),
        ("logic_or_1", "1 || 0", 1),
        ("logic_or_2", "0 || 0", 0),
        ("logic_not_1", "!0", 1),
        ("logic_not_2", "!1", 0),
    ]
    runtime_cases.extend(logic_cases)

    range_cases = [
        ("range_exclusive_low", "0 in 1..5", 0),
        ("range_exclusive_mid", "3 in 1..5", 1),
        ("range_exclusive_high", "5 in 1..5", 0),
        ("range_inclusive_low", "1 in 1..=5", 1),
        ("range_inclusive_high", "5 in 1..=5", 1),
    ]
    runtime_cases.extend(range_cases)

    precedence_cases = [
        ("precedence_1", "1 + 2 * 3", 7),
        ("precedence_2", "(1 + 2) * 3", 9),
        ("precedence_3", "8 >> 1 + 1", 2),
        ("precedence_4", "(8 >> 1) + 1", 5),
    ]
    runtime_cases.extend(precedence_cases)

    for name, expr, expected in runtime_cases:
        make_runtime_test(name, expr, expected)

    if_cases = [
        ("if_eq_true", "2 == 2", 7, 9, 7),
        ("if_eq_false", "2 == 3", 7, 9, 9),
        ("if_range_true", "3 in 1..=5", 1, 0, 1),
        ("if_range_false", "8 in 1..=5", 1, 0, 0),
    ]
    for args in if_cases:
        make_runtime_if_test(*args)

    type_names = [
        "i2",
        "i8",
        "i16",
        "i32",
        "i64",
        "u8",
        "u16",
        "u32",
        "u64",
        "bool",
        "short",
        "int",
        "long",
        "double",
        "float",
        "f8",
        "f16",
        "f32",
        "f64",
        "char",
        "str",
        "i32[4]",
        "i8[2][3]",
        "str[]",
        "i32*",
        "char*",
        "ref i32",
        "weak str",
    ]
    for type_name in type_names:
        make_parse_type_test(type_name)

    literal_cases = [
        ("float", "f64", "3.25", "Float 3.25"),
        ("bool_true", "bool", "true", "Bool true"),
        ("bool_false", "bool", "false", "Bool false"),
        ("char_a", "char", "'a'", "Char"),
        ("array_ints", "int", "[1, 2, 3]", "Init []"),
    ]
    for args in literal_cases:
        make_literal_parse_test(*args)

    for index in range(1, 9):
        make_negative_let_test(index)

    for index in range(1, 9):
        make_negative_compile_call_test(index)

    codegen_cases = [
        ("ret_i8", "i8", "5", "ret i8 5"),
        ("ret_i16", "i16", "5", "ret i16 5"),
        ("ret_i32", "i32", "5", "ret i32 5"),
        ("ret_i64", "i64", "5", "ret i64 5"),
        ("ret_bool", "bool", "true", "ret i1 true"),
        ("ret_char", "char", "'A'", "ret i8 65"),
        ("ret_f32", "f32", "1.5", "ret float 1.500000e+00"),
        ("ret_f64", "f64", "1.5", "ret double 1.500000e+00"),
    ]
    for args in codegen_cases:
        make_codegen_type_test(*args)


if __name__ == "__main__":
    main()
