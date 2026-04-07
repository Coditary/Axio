#pragma once

#include <filesystem>
#include <string>

#include "axc/AST/AST.h"

namespace axc {

namespace detail {

class ModuleFileParser {
  public:
    bool parse(const std::filesystem::path& path, TranslationUnit& unit, std::string& errorMessage) const;
};

}  // namespace detail

}  // namespace axc
