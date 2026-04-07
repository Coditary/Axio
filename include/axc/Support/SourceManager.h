#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "axc/Support/SourceLocation.h"

namespace axc {

/// @brief Human-readable line/column position derived from a byte offset.
struct LineColumn {
    std::size_t line = 1;
    std::size_t column = 1;
};

/// @brief One source line plus its starting byte offset.
struct LineInfo {
    std::size_t lineNumber = 1;
    std::size_t lineStart = 0;
    std::string_view text {};
};

/// @brief Owns one loaded source file and maps offsets back to line data.
class SourceManager {
  public:
    /// @brief Load a source file from disk.
    /// @return `true` on success, `false` and an explanatory message on failure.
    bool loadFromFile(const std::filesystem::path& path, std::string& errorMessage);

    /// @brief Path of the currently loaded source file.
    [[nodiscard]] const std::filesystem::path& path() const;
    /// @brief Entire source buffer as a string view.
    [[nodiscard]] std::string_view source() const;
    /// @brief Convert a byte offset into line/column coordinates.
    [[nodiscard]] LineColumn lineColumn(SourceLocation location) const;
    /// @brief Return the source line containing `location`, if any.
    [[nodiscard]] std::optional<LineInfo> lineAt(SourceLocation location) const;
    /// @brief Construct a source range from raw begin/end offsets.
    [[nodiscard]] SourceRange range(std::size_t begin, std::size_t end) const;

  private:
    std::filesystem::path path_ {};
    std::string source_ {};
    std::vector<std::size_t> lineOffsets_ {};
};

}  // namespace axc
