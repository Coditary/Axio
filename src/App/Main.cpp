#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "axc/Driver/Compiler.h"

namespace {

struct CliConfig {
    axc::CompileOptions options;
    bool showHelp = false;
};

void printUsage() {
    std::cout << "axc - Axio compiler prototype\n"
              << "usage: axc <input.ax> [-o output-stem] [--emit-llvm-only] [--emit-object-only] [--check-only] [--dump-ast]\n"
              << "options:\n"
              << "  -h, --help          Show this help\n"
              << "  -o <path>           Output stem or file base\n"
              << "  --emit-llvm-only    Emit LLVM IR only\n"
              << "  --emit-object-only  Emit object file only\n"
              << "  --check-only        Run frontend checks only\n"
              << "  --dump-ast          Print parsed AST only\n";
}

std::optional<CliConfig> parseArgs(int argc, char** argv) {
    CliConfig config;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            config.showHelp = true;
            return config;
        }
        if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "error: expected path after -o\n";
                return std::nullopt;
            }
            config.options.outputFile = argv[++i];
            continue;
        }
        if (arg == "--emit-llvm-only") {
            config.options.emitLlvmIr = true;
            config.options.emitObject = false;
            config.options.emitBinary = false;
            continue;
        }
        if (arg == "--emit-object-only") {
            config.options.emitLlvmIr = false;
            config.options.emitObject = true;
            config.options.emitBinary = false;
            continue;
        }
        if (arg == "--check-only") {
            config.options.checkOnly = true;
            config.options.emitLlvmIr = false;
            config.options.emitObject = false;
            config.options.emitBinary = false;
            continue;
        }
        if (arg == "--dump-ast") {
            config.options.dumpAst = true;
            config.options.emitLlvmIr = false;
            config.options.emitObject = false;
            config.options.emitBinary = false;
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            std::cerr << "error: unknown argument '" << arg << "'\n";
            return std::nullopt;
        }
        positional.push_back(arg);
    }

    if (positional.empty()) {
        std::cerr << "error: no input file provided\n";
        return std::nullopt;
    }
    if (positional.size() > 1) {
        std::cerr << "error: unexpected positional argument '" << positional.back() << "'\n";
        return std::nullopt;
    }

    config.options.inputFile = std::filesystem::path(positional.front());
    return config;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    auto config = parseArgs(argc, argv);
    if (!config.has_value()) {
        return 1;
    }

    if (config->showHelp) {
        printUsage();
        return 0;
    }

    axc::Compiler compiler;
    return compiler.compile(config->options) ? 0 : 1;
}
