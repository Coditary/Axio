#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Sema_AllowsFlagEnumConstEvalInitializers) {
    auto dir = axc::unit::makeTempDir("sema_enum_flags");
    const auto path = dir.path / "enum_flags.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "enum ActorState as Flags {\n"
                                    "    IsAlive,\n"
                                    "    IsVisible,\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let state ActorState = ActorState{IsAlive, IsVisible}\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!file.diagnostics.hasErrors());
    return true;
}

AXC_TEST(Sema_PermitsPartialEnumParameterPayloadMetadata) {
    auto dir = axc::unit::makeTempDir("sema_enum_payloads");
    const auto path = dir.path / "enum_payloads.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "enum DistanceBand(label int, mult int) {\n"
                                    "    Near(1),\n"
                                    "    Mid(2, 2),\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!file.diagnostics.hasErrors());
    return true;
}

AXC_TEST(Sema_RejectsNonExhaustiveEnumSwitchWithoutDefault) {
    auto dir = axc::unit::makeTempDir("sema_enum_switch_missing_default");
    const auto path = dir.path / "enum_switch_missing_default.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "enum Color() {\n"
                                    "    Red,\n"
                                    "    Green,\n"
                                    "    Blue,\n"
                                    "}\n"
                                    "fn score(color Color) int {\n"
                                    "    switch color {\n"
                                    "        case Color.Red {\n"
                                    "            return 1\n"
                                    "        }\n"
                                    "        case Color.Green {\n"
                                    "            return 2\n"
                                    "        }\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics,
                                                  "switch over enum 'Color' must cover all cases or provide a default",
                                                  axc::DiagnosticSeverity::Error));
    return true;
}

AXC_TEST(Sema_AllowsEnumSwitchWithDefaultFallback) {
    auto dir = axc::unit::makeTempDir("sema_enum_switch_default");
    const auto path = dir.path / "enum_switch_default.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "enum Color() {\n"
                                    "    Red,\n"
                                    "    Green,\n"
                                    "    Blue,\n"
                                    "}\n"
                                    "fn score(color Color) int {\n"
                                    "    switch color {\n"
                                    "        case Color.Red {\n"
                                    "            return 1\n"
                                    "        }\n"
                                    "        default {\n"
                                    "            return 0\n"
                                    "        }\n"
                                    "    }\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!file.diagnostics.hasErrors());
    return true;
}

AXC_TEST(Sema_AllowsExhaustiveEnumSwitchWithoutDefault) {
    auto dir = axc::unit::makeTempDir("sema_enum_switch_exhaustive");
    const auto path = dir.path / "enum_switch_exhaustive.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "enum Color() {\n"
                                    "    Red,\n"
                                    "    Green,\n"
                                    "    Blue,\n"
                                    "}\n"
                                    "fn score(color Color) int {\n"
                                    "    switch color {\n"
                                    "        case Color.Red {\n"
                                    "            return 1\n"
                                    "        }\n"
                                    "        case Color.Green {\n"
                                    "            return 2\n"
                                    "        }\n"
                                    "        case Color.Blue {\n"
                                    "            return 3\n"
                                    "        }\n"
                                    "    }\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!file.diagnostics.hasErrors());
    return true;
}

AXC_TEST(Sema_ReportsConcreteMissingEnumSwitchCases) {
    auto dir = axc::unit::makeTempDir("sema_enum_switch_missing_cases");
    const auto path = dir.path / "enum_switch_missing_cases.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "enum Color() {\n"
                                    "    Red,\n"
                                    "    Green,\n"
                                    "    Blue,\n"
                                    "    Yellow,\n"
                                    "}\n"
                                    "fn score(color Color) int {\n"
                                    "    switch color {\n"
                                    "        case Color.Red, Color.Green {\n"
                                    "            return 1\n"
                                    "        }\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "missing: Color.Blue, Color.Yellow", axc::DiagnosticSeverity::Error));
    return true;
}

AXC_TEST(Sema_RejectsOverlappingSwitchCases) {
    auto dir = axc::unit::makeTempDir("sema_switch_overlap");
    const auto path = dir.path / "switch_overlap.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "enum Color() {\n"
                                    "    Red,\n"
                                    "    Green,\n"
                                    "    Blue,\n"
                                    "}\n"
                                    "fn score(color Color) int {\n"
                                    "    switch color {\n"
                                    "        case Color.Red, Color.Green {\n"
                                    "            return 1\n"
                                    "        }\n"
                                    "        case Color.Green {\n"
                                    "            return 2\n"
                                    "        }\n"
                                    "        default {\n"
                                    "            return 0\n"
                                    "        }\n"
                                    "    }\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics,
                                                  "switch case overlaps a previous case: Color.Green",
                                                  axc::DiagnosticSeverity::Error));
    return true;
}

AXC_TEST(Sema_RejectsNonConstantSwitchPatterns) {
    auto dir = axc::unit::makeTempDir("sema_switch_nonconstant_pattern");
    const auto path = dir.path / "switch_nonconstant_pattern.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn score(value int, limit int) int {\n"
                                    "    switch value {\n"
                                    "        case limit {\n"
                                    "            return 1\n"
                                    "        }\n"
                                    "        default {\n"
                                    "            return 0\n"
                                    "        }\n"
                                    "    }\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics,
                                                  "switch case values must be compile-time constants",
                                                  axc::DiagnosticSeverity::Error));
    return true;
}
