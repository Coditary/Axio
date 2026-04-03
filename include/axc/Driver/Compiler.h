#pragma once

#include <filesystem>

namespace axc {

struct CompileOptions {
    std::filesystem::path inputFile {};
    std::filesystem::path outputFile {};
    bool emitLlvmIr = true;
    bool emitObject = true;
    bool emitBinary = true;
    bool checkOnly = false;
    bool dumpAst = false;
};

class Compiler {
  public:
    bool compile(const CompileOptions& options) const;
};

}  // namespace axc
