#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Sema_ReportsMissingClassMembers) {
    auto dir = axc::unit::makeTempDir("sema_missing_member");
    const auto path = dir.path / "missing_member.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "class User {\n"
                                    "    name str\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let user = User()\n"
                                    "    return user.age\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "has no member 'age'", axc::DiagnosticSeverity::Error));
    return true;
}

AXC_TEST(Sema_AllowsStructIncludedMembers) {
    auto dir = axc::unit::makeTempDir("sema_include_member");
    const auto path = dir.path / "include_member.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "struct Base {\n"
                                    "    age int\n"
                                    "}\n"
                                    "class User {\n"
                                    "    Base\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let user = User()\n"
                                    "    return user.age\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!axc::unit::hasDiagnosticContaining(file.diagnostics, "has no member 'age'", axc::DiagnosticSeverity::Error));
    return true;
}

AXC_TEST(Sema_AllowsSelfMethodCallsWithoutMissingMethodDiagnostics) {
    auto dir = axc::unit::makeTempDir("sema_self_method_call");
    const auto path = dir.path / "self_method.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "class User {\n"
                                    "    fn age() int {\n"
                                    "        return 7\n"
                                    "    }\n"
                                    "\n"
                                    "    fn doubled() int {\n"
                                    "        return self.age() + self.age()\n"
                                    "    }\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let user = User()\n"
                                    "    return user.doubled()\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!axc::unit::hasDiagnosticContaining(file.diagnostics, "has no method 'age'", axc::DiagnosticSeverity::Error));
    AXC_EXPECT(!axc::unit::hasDiagnosticContaining(file.diagnostics, "has no method 'doubled'", axc::DiagnosticSeverity::Error));
    return true;
}

AXC_TEST(Sema_RejectsClassMemberMethodNameConflicts) {
    auto dir = axc::unit::makeTempDir("sema_member_method_conflict");
    const auto path = dir.path / "member_method_conflict.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "class User {\n"
                                    "    greet int\n"
                                    "\n"
                                    "    fn greet() int {\n"
                                    "        return 1\n"
                                    "    }\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics,
                                                  "class member and method share the name 'greet'",
                                                  axc::DiagnosticSeverity::Error));
    return true;
}
