#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Sema_AllowsTypedAddressOfAndDereference) {
    auto dir = axc::unit::makeTempDir("sema_pointer_exprs");
    const auto path = dir.path / "pointer_exprs.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let value int = 7\n"
                                    "    let ptr int* = &value\n"
                                    "    return *ptr\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeSource(file, path));
    AXC_EXPECT(!file.diagnostics.hasErrors());
    return true;
}
