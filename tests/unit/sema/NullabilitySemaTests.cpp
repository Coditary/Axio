#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Sema_AllowsGuardedNullableMemberAccess) {
    auto dir = axc::unit::makeTempDir("sema_guarded_nullable");
    const auto path = dir.path / "guarded.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "class Box {\n"
                                    "    value int\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let ptr Box = new Box()\n"
                                    "    if ptr? {\n"
                                    "        return ptr.value\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!axc::unit::hasDiagnosticContaining(file.diagnostics, "member access may dereference a nullable value", axc::DiagnosticSeverity::Warning));
    return true;
}

AXC_TEST(Sema_WarnsOnNullSafeMemberAccessForNonNullableValues) {
    auto dir = axc::unit::makeTempDir("sema_nullsafe_member");
    const auto path = dir.path / "nullsafe_member.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "class Box {\n"
                                    "    value int\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let box = Box()\n"
                                    "    box?.value\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "null-safe member access used on a value that is not known to be nullable", axc::DiagnosticSeverity::Warning));
    return true;
}

AXC_TEST(Sema_AllowsChainedGuardedNullableMemberAccess) {
    auto dir = axc::unit::makeTempDir("sema_guarded_nullable_chain");
    const auto path = dir.path / "guarded_chain.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "class Node {\n"
                                    "    next Node\n"
                                    "    value int\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let ptr Node = new Node(new Node(null, 7), 3)\n"
                                    "    if ptr? {\n"
                                    "        return ptr.next.value\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!axc::unit::hasDiagnosticContaining(file.diagnostics, "member access may dereference a nullable value", axc::DiagnosticSeverity::Warning));
    return true;
}
