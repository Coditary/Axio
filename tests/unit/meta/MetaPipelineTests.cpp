#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(MetaPipeline_WarnsOnUnknownAnnotations) {
    auto dir = axc::unit::makeTempDir("meta_annotations");
    const auto path = dir.path / "annotations.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "@Unknown\n"
                                    "fn main() int {\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeAndRunMeta(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "unknown annotation '@Unknown' ignored", axc::DiagnosticSeverity::Warning));
    return true;
}

AXC_TEST(MetaPipeline_ValidatesCompileCallArgumentsAndDialects) {
    auto dir = axc::unit::makeTempDir("meta_embed");
    const auto path = dir.path / "embed.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    $readfile(42)\n"
                                    "    let sql str = %%SQL{\nSELECT 1\n}%%\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeAndRunMeta(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "$readfile expects a leading string literal argument", axc::DiagnosticSeverity::Error));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "dialect blocks are parsed but not yet lowered", axc::DiagnosticSeverity::Warning));
    return true;
}

AXC_TEST(MetaPipeline_VisitsNestedIfBranchesForCompileCalls) {
    auto dir = axc::unit::makeTempDir("meta_nested_if");
    const auto path = dir.path / "nested.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    if 1 {\n"
                                    "        return 0\n"
                                    "    } else {\n"
                                    "        $unknown_meta(\"value\")\n"
                                    "    }\n"
                                    "    return 1\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeAndRunMeta(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics,
                                                  "unknown compile function '$unknown_meta' ignored",
                                                  axc::DiagnosticSeverity::Warning));
    return true;
}

AXC_TEST(MetaPipeline_ValidatesNestedBlockCompileCallsAfterEarlyStatements) {
    auto dir = axc::unit::makeTempDir("meta_nested_block");
    const auto path = dir.path / "nested_block.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let value int = 1\n"
                                    "    if value {\n"
                                    "        $readfile(123)\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeAndRunMeta(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics,
                                                  "$readfile expects a leading string literal argument",
                                                  axc::DiagnosticSeverity::Error));
    return true;
}
