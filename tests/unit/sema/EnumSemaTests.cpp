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
