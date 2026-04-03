#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "axc/Support/SourceLocation.h"

namespace axc {

struct LineColumn {
    std::size_t line = 1;
    std::size_t column = 1;
};

struct LineInfo {
    std::size_t lineNumber = 1;
    std::size_t lineStart = 0;
    std::string_view text {};
};

class SourceManager {
  public:
    bool loadFromFile(const std::filesystem::path& path, std::string& errorMessage);

    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] std::string_view source() const;
    [[nodiscard]] LineColumn lineColumn(SourceLocation location) const;
    [[nodiscard]] std::optional<LineInfo> lineAt(SourceLocation location) const;
    [[nodiscard]] SourceRange range(std::size_t begin, std::size_t end) const;

  private:
    std::filesystem::path path_ {};
    std::string source_ {};
    std::vector<std::size_t> lineOffsets_ {};
};

}  // namespace axc
