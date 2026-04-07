#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Sema_RejectsMultiReturnInIfConditions) {
    auto dir = axc::unit::makeTempDir("sema_multi_if");
    const auto path = dir.path / "multi_if.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn pair() (int, int) {\n"
                                    "    return 1, 2\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    if pair() { return 1 }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "if conditions must be single values", axc::DiagnosticSeverity::Error));
    return true;
}

AXC_TEST(Sema_AllowsMultiReturnForwarding) {
    auto dir = axc::unit::makeTempDir("sema_multi_forward");
    const auto path = dir.path / "multi_forward.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn pair() (int, int) {\n"
                                    "    return 1, 2\n"
                                    "}\n"
                                    "fn copy() (int, int) {\n"
                                    "    return pair()\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!file.diagnostics.hasErrors());
    AXC_EXPECT(!axc::unit::hasDiagnosticContaining(file.diagnostics, "multi-return is parsed but not yet lowered to LLVM", axc::DiagnosticSeverity::Warning));
    return true;
}

AXC_TEST(Sema_RejectsBreakAndContinueOutsideValidContexts) {
    auto dir = axc::unit::makeTempDir("sema_break_continue_invalid");
    const auto path = dir.path / "break_continue_invalid.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    break\n"
                                    "    continue\n"
                                    "    switch 1 {\n"
                                    "        case 1 {\n"
                                    "            continue\n"
                                    "        }\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "break can only appear inside loops or switch cases", axc::DiagnosticSeverity::Error));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "continue can only appear inside loops", axc::DiagnosticSeverity::Error));
    return true;
}
