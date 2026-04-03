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
