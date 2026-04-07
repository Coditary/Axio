#pragma once

#include <filesystem>
#include <string>

#include "axc/AST/AST.h"

namespace axc {

namespace detail {

/// @brief Helper for parsing one module file in isolation.
class ModuleFileParser {
  public:
    /// @brief Parse `path` into `unit` or return a rendered error message.
    bool parse(const std::filesystem::path& path, TranslationUnit& unit, std::string& errorMessage) const;
};

}  // namespace detail

}  // namespace axc
