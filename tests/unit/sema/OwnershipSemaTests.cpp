#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Sema_RejectsDuplicateUniqueReturns) {
    auto dir = axc::unit::makeTempDir("sema_unique_return");
    const auto path = dir.path / "unique_return.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "class Obj { }\n"
                                    "fn clone(x *Obj) (*Obj, *Obj) {\n"
                                    "    return x, x\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics,
                                                  "unique values cannot be returned more than once from the same statement",
                                                  axc::DiagnosticSeverity::Error));
    return true;
}

AXC_TEST(Sema_RejectsWeakBindingsFromValueInitializers) {
    auto dir = axc::unit::makeTempDir("sema_weak_binding");
    const auto path = dir.path / "weak_binding.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "struct Obj { }\n"
                                    "fn main() int {\n"
                                    "    let value Obj = Obj()\n"
                                    "    let weakRef weak Obj = value\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics,
                                                  "weak values must originate from ARC or weak references",
                                                  axc::DiagnosticSeverity::Error));
    return true;
}

AXC_TEST(Sema_RejectsAssignmentsToConstBindingsAndParams) {
    auto dir = axc::unit::makeTempDir("sema_const_assign");
    const auto path = dir.path / "const_assign.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "const globalCount int = 1\n"
                                    "fn bump(const value int) int {\n"
                                    "    value = value + 1\n"
                                    "    return value\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    const local int = 7\n"
                                    "    local = 9\n"
                                    "    globalCount = 2\n"
                                    "    return bump(local)\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics,
                                                  "cannot assign to const storage 'value'",
                                                  axc::DiagnosticSeverity::Error));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics,
                                                  "cannot assign to const storage 'local'",
                                                  axc::DiagnosticSeverity::Error));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics,
                                                  "cannot assign to const storage 'globalCount'",
                                                  axc::DiagnosticSeverity::Error));
    return true;
}
