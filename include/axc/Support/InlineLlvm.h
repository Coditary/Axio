#pragma once

#include <string>

#include "axc/AST/AST.h"

namespace axc {

bool buildInlineLlvmModuleText(const FunctionDecl& function,
                               const std::string& functionName,
                               std::string& moduleText,
                               std::string& errorMessage);

bool verifyInlineLlvmModuleText(const std::string& moduleText,
                                const std::string& functionName,
                                std::string& errorMessage);

}  // namespace axc
