#include "axc/Support/SourceManager.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace axc {

bool SourceManager::loadFromFile(const std::filesystem::path& path, std::string& errorMessage) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        errorMessage = "failed to open source file";
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    source_ = buffer.str();
    path_ = path;

    lineOffsets_.clear();
    lineOffsets_.push_back(0);
    for (std::size_t i = 0; i < source_.size(); ++i) {
        if (source_[i] == '\n') {
            lineOffsets_.push_back(i + 1);
        }
    }

    return true;
}

const std::filesystem::path& SourceManager::path() const {
    return path_;
}

std::string_view SourceManager::source() const {
    return source_;
}

LineColumn SourceManager::lineColumn(SourceLocation location) const {
    if (lineOffsets_.empty()) {
        return {};
    }

    auto it = std::upper_bound(lineOffsets_.begin(), lineOffsets_.end(), location.offset);
    if (it == lineOffsets_.begin()) {
        return {};
    }

    const std::size_t lineIndex = static_cast<std::size_t>(std::distance(lineOffsets_.begin(), it - 1));
    const std::size_t lineStart = lineOffsets_[lineIndex];
    return {lineIndex + 1, location.offset - lineStart + 1};
}

std::optional<LineInfo> SourceManager::lineAt(SourceLocation location) const {
    if (lineOffsets_.empty()) {
        return std::nullopt;
    }

    auto it = std::upper_bound(lineOffsets_.begin(), lineOffsets_.end(), location.offset);
    if (it == lineOffsets_.begin()) {
        return std::nullopt;
    }

    const std::size_t lineIndex = static_cast<std::size_t>(std::distance(lineOffsets_.begin(), it - 1));
    const std::size_t lineStart = lineOffsets_[lineIndex];
    std::size_t lineEnd = source_.find('\n', lineStart);
    if (lineEnd == std::string::npos) {
        lineEnd = source_.size();
    }

    return LineInfo {lineIndex + 1, lineStart, std::string_view(source_).substr(lineStart, lineEnd - lineStart)};
}

SourceRange SourceManager::range(std::size_t begin, std::size_t end) const {
    return SourceRange {SourceLocation {begin}, SourceLocation {end}};
}

}  // namespace axc
