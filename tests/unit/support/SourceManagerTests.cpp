#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(SourceManager_LoadsFilesAndMapsLines) {
    auto dir = axc::unit::makeTempDir("source_manager_lines");
    const auto path = dir.path / "sample.ax";
    AXC_EXPECT(axc::unit::writeFile(path, "fn main() int {\n    return 7\n}\n"));

    axc::SourceManager sourceManager;
    std::string error;
    AXC_EXPECT(sourceManager.loadFromFile(path, error));

    const axc::LineColumn lineColumn = sourceManager.lineColumn(axc::SourceLocation {20});
    AXC_EXPECT_EQ(lineColumn.line, 2U);
    AXC_EXPECT(lineColumn.column > 1);

    const auto lineInfo = sourceManager.lineAt(axc::SourceLocation {20});
    AXC_EXPECT(lineInfo.has_value());
    AXC_EXPECT_EQ(lineInfo->lineNumber, 2U);
    AXC_EXPECT_CONTAINS(std::string(lineInfo->text), "return 7");
    return true;
}

AXC_TEST(SourceManager_ReportsMissingFiles) {
    axc::SourceManager sourceManager;
    std::string error;
    AXC_EXPECT(!sourceManager.loadFromFile(std::filesystem::path("/definitely/missing.ax"), error));
    AXC_EXPECT_CONTAINS(error, "failed to open source file");
    return true;
}

AXC_TEST(TokenKindName_UsesReadableSpellings) {
    AXC_EXPECT_EQ(std::string(axc::tokenKindName(axc::TokenKind::KwFn)), "fn");
    AXC_EXPECT_EQ(std::string(axc::tokenKindName(axc::TokenKind::RangeInclusive)), "..=");
    AXC_EXPECT_EQ(std::string(axc::tokenKindName(axc::TokenKind::QuestionDot)), "?.");
    return true;
}

AXC_TEST(SourceManager_TracksUtf8ColumnsByByteOffset) {
    auto dir = axc::unit::makeTempDir("source_manager_utf8");
    const auto path = dir.path / "utf8.ax";
    AXC_EXPECT(axc::unit::writeFile(path, "let grüße = 1\nreturn grüße\n"));

    axc::SourceManager sourceManager;
    std::string error;
    AXC_EXPECT(sourceManager.loadFromFile(path, error));

    const std::string text = std::string(sourceManager.source());
    const std::size_t returnOffset = text.find("return");
    const std::size_t identOffset = text.find("grüße", returnOffset);
    AXC_EXPECT(returnOffset != std::string::npos);
    AXC_EXPECT(identOffset != std::string::npos);

    const axc::LineColumn lineColumn = sourceManager.lineColumn(axc::SourceLocation {identOffset});
    AXC_EXPECT_EQ(lineColumn.line, 2U);
    AXC_EXPECT_EQ(lineColumn.column, 8U);

    const auto lineInfo = sourceManager.lineAt(axc::SourceLocation {identOffset});
    AXC_EXPECT(lineInfo.has_value());
    AXC_EXPECT_EQ(lineInfo->lineNumber, 2U);
    AXC_EXPECT_CONTAINS(std::string(lineInfo->text), "grüße");
    return true;
}

AXC_TEST(SourceManager_HandlesEndOfFileLineLookups) {
    auto dir = axc::unit::makeTempDir("source_manager_eof");
    const auto path = dir.path / "eof.ax";
    AXC_EXPECT(axc::unit::writeFile(path, "fn main() int {\n    return 0\n}"));

    axc::SourceManager sourceManager;
    std::string error;
    AXC_EXPECT(sourceManager.loadFromFile(path, error));

    const std::string text = std::string(sourceManager.source());
    const auto lineInfo = sourceManager.lineAt(axc::SourceLocation {text.size() - 1});
    AXC_EXPECT(lineInfo.has_value());
    AXC_EXPECT_EQ(lineInfo->lineNumber, 3U);
    AXC_EXPECT_CONTAINS(std::string(lineInfo->text), "}");
    return true;
}
