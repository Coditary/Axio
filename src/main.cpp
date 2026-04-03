#include <filesystem>
#include <iostream>
#include <string>

#include "axc/Driver/Compiler.h"

namespace {

void printUsage() {
    std::cout << "axc - Axio compiler prototype\n"
              << "usage: axc <input.ax> [-o output-stem] [--emit-llvm-only] [--emit-object-only] [--check-only] [--dump-ast]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    axc::CompileOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        }
        if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "error: expected path after -o\n";
                return 1;
            }
            options.outputFile = argv[++i];
            continue;
        }
        if (arg == "--emit-llvm-only") {
            options.emitLlvmIr = true;
            options.emitObject = false;
            options.emitBinary = false;
            continue;
        }
        if (arg == "--emit-object-only") {
            options.emitLlvmIr = false;
            options.emitObject = true;
            options.emitBinary = false;
            continue;
        }
        if (arg == "--check-only") {
            options.checkOnly = true;
            options.emitLlvmIr = false;
            options.emitObject = false;
            options.emitBinary = false;
            continue;
        }
        if (arg == "--dump-ast") {
            options.dumpAst = true;
            options.emitLlvmIr = false;
            options.emitObject = false;
            options.emitBinary = false;
            continue;
        }
        if (options.inputFile.empty()) {
            options.inputFile = std::filesystem::path(arg);
            continue;
        }

        std::cerr << "error: unknown argument '" << arg << "'\n";
        return 1;
    }

    if (options.inputFile.empty()) {
        std::cerr << "error: no input file provided\n";
        return 1;
    }

    axc::Compiler compiler;
    return compiler.compile(options) ? 0 : 1;
}
