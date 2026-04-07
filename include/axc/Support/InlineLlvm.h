#pragma once

#include <string>

#include "axc/AST/AST.h"

namespace axc {

/// @brief Build a standalone LLVM module text wrapper for one inline-LLVM function.
bool buildInlineLlvmModuleText(const FunctionDecl& function,
                               const std::string& functionName,
                               std::string& moduleText,
                               std::string& errorMessage);

/// @brief Parse and verify inline LLVM text for one function body.
bool verifyInlineLlvmModuleText(const std::string& moduleText,
                                 const std::string& functionName,
                                 std::string& errorMessage);

}  // namespace axc
