#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

#include <sstream>

AXC_TEST(DiagnosticEngine_StoresAndRendersDiagnostics) {
    auto dir = axc::unit::makeTempDir("diagnostics");
    const auto path = dir.path / "diag.ax";
    AXC_EXPECT(axc::unit::writeFile(path, "let x = 1\n"));

    axc::SourceManager sourceManager;
    std::string error;
    AXC_EXPECT(sourceManager.loadFromFile(path, error));

    axc::DiagnosticEngine diagnostics(sourceManager);
    diagnostics.warning(sourceManager.range(0, 3), "sample warning");
    diagnostics.error(sourceManager.range(4, 5), "sample error");

    AXC_EXPECT_EQ(diagnostics.diagnostics().size(), 2U);
    AXC_EXPECT(diagnostics.hasErrors());

    const std::string rendered = axc::unit::renderDiagnostics(diagnostics);
    AXC_EXPECT_CONTAINS(rendered, "warning: sample warning");
    AXC_EXPECT_CONTAINS(rendered, "error: sample error");
    AXC_EXPECT_CONTAINS(rendered, "^\n");
    return true;
}

AXC_TEST(DiagnosticEngine_MarksRenderedOutput) {
    auto dir = axc::unit::makeTempDir("diagnostics_rendered");
    const auto path = dir.path / "diag.ax";
    AXC_EXPECT(axc::unit::writeFile(path, "return 0\n"));

    axc::SourceManager sourceManager;
    std::string error;
    AXC_EXPECT(sourceManager.loadFromFile(path, error));

    axc::DiagnosticEngine diagnostics(sourceManager);
    diagnostics.note(sourceManager.range(0, 6), "render me");
    AXC_EXPECT(!diagnostics.hasRenderedAll());
    const std::string rendered = axc::unit::renderDiagnostics(diagnostics);
    AXC_EXPECT(diagnostics.hasRenderedAll());
    AXC_EXPECT_CONTAINS(rendered, "note: render me");
    return true;
}

AXC_TEST(DiagnosticEngine_RendersMultiCharacterRangesWithExpectedWidth) {
    auto dir = axc::unit::makeTempDir("diagnostics_width");
    const auto path = dir.path / "diag.ax";
    AXC_EXPECT(axc::unit::writeFile(path, "abcdef\n"));

    axc::SourceManager sourceManager;
    std::string error;
    AXC_EXPECT(sourceManager.loadFromFile(path, error));

    axc::DiagnosticEngine diagnostics(sourceManager);
    diagnostics.error(sourceManager.range(1, 4), "highlight width");

    const std::string rendered = axc::unit::renderDiagnostics(diagnostics);
    AXC_EXPECT_CONTAINS(rendered, "error: highlight width");
    AXC_EXPECT_CONTAINS(rendered, "^^^");
    return true;
}

AXC_TEST(DiagnosticEngine_RendersTabAlignedCarets) {
    auto dir = axc::unit::makeTempDir("diagnostics_tabs");
    const auto path = dir.path / "diag.ax";
    AXC_EXPECT(axc::unit::writeFile(path, "\tvalue\n"));

    axc::SourceManager sourceManager;
    std::string error;
    AXC_EXPECT(sourceManager.loadFromFile(path, error));

    axc::DiagnosticEngine diagnostics(sourceManager);
    diagnostics.error(sourceManager.range(1, 6), "tab aligned");

    const std::string rendered = axc::unit::renderDiagnostics(diagnostics);
    AXC_EXPECT_CONTAINS(rendered, "error: tab aligned");
    AXC_EXPECT_CONTAINS(rendered, "^\n");
    return true;
}
