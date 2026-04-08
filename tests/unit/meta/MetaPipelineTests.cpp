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

AXC_TEST(MetaPipeline_IgnoresRemovedMetaEmbeddingSyntax) {
    auto dir = axc::unit::makeTempDir("meta_removed_embedding");
    const auto path = dir.path / "removed.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::analyzeAndRunMeta(file, path));
    AXC_EXPECT(!file.diagnostics.hasErrors());
    return true;
}
