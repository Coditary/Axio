#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Sema_AllowsRefParameterAddressPassing) {
    auto dir = axc::unit::makeTempDir("sema_ref_param_ok");
    const auto path = dir.path / "ref_param_ok.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "class Obj {\n"
                                    "    value int\n"
                                    "}\n"
                                    "fn read(x ref Obj) int {\n"
                                    "    return x.value\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let value = Obj(7)\n"
                                    "    return read(&value)\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!file.diagnostics.hasErrors());
    return true;
}

AXC_TEST(Sema_AllowsReturningRefParametersFromRefFunctions) {
    auto dir = axc::unit::makeTempDir("sema_ref_return_ok");
    const auto path = dir.path / "ref_return_ok.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "class Obj {\n"
                                    "}\n"
                                    "fn borrow(x ref Obj) ref Obj {\n"
                                    "    return x\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!file.diagnostics.hasErrors());
    return true;
}

AXC_TEST(Sema_AllowsUniqueToOwningClassBindings) {
    auto dir = axc::unit::makeTempDir("sema_unique_to_arc_ok");
    const auto path = dir.path / "unique_to_arc_ok.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "class Obj {\n"
                                    "    value int\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let owned Obj = *Obj(7)\n"
                                    "    return owned.value\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!file.diagnostics.hasErrors());
    return true;
}
