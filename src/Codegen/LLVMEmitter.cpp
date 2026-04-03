#include "axc/Codegen/LLVMEmitter.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMEmitterInternal.h"
#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace axc {

namespace {

std::filesystem::path runtimeObjectPath() {
    return std::filesystem::path("/home/leodora/Documents/Dev/AI/Axio/build/CMakeFiles/axc_core.dir/src/Runtime/AxioRuntime.cpp.o");
}

}

std::string shellQuote(const std::filesystem::path& path) {
    std::string value = path.string();
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted += "'";
    return quoted;
}

bool emitObjectFile(llvm::Module& module, const std::filesystem::path& objectPath, DiagnosticEngine& diagnostics, SourceRange errorRange) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    module.setTargetTriple(triple);

    std::string error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple.getTriple(), error);
    if (target == nullptr) {
        diagnostics.error(errorRange, "failed to look up native target: " + error);
        return false;
    }

    llvm::TargetOptions options;
    auto targetMachine = std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(triple, "generic", "", options, std::nullopt));
    module.setDataLayout(targetMachine->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream dest(objectPath.string(), ec, llvm::sys::fs::OF_None);
    if (ec) {
        diagnostics.error(errorRange, "failed to create object file '" + objectPath.string() + "'");
        return false;
    }

    llvm::legacy::PassManager pass;
    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        diagnostics.error(errorRange, "native target cannot emit object files");
        return false;
    }

    pass.run(module);
    dest.flush();
    return true;
}

LLVMEmitter::LLVMEmitter(const SourceManager& sourceManager, DiagnosticEngine& diagnostics)
    : sourceManager_(sourceManager), diagnostics_(diagnostics) {}

bool LLVMEmitter::emit(const TranslationUnit& translationUnit, const EmitOptions& options) {
    ModuleEmitter moduleEmitter(sourceManager_, diagnostics_);
    std::unique_ptr<llvm::Module> module = moduleEmitter.emit(translationUnit);
    if (module == nullptr || diagnostics_.hasErrors()) {
        return false;
    }

    const SourceRange syntheticRange {SourceLocation {0}, SourceLocation {0}};

    if (!options.llvmIrOutput.empty()) {
        std::error_code ec;
        llvm::raw_fd_ostream out(options.llvmIrOutput.string(), ec, llvm::sys::fs::OF_Text);
        if (ec) {
            diagnostics_.error(syntheticRange, "failed to write LLVM IR file '" + options.llvmIrOutput.string() + "'");
            return false;
        }
        module->print(out, nullptr);
    }

    if (!options.objectOutput.empty()) {
        if (!emitObjectFile(*module, options.objectOutput, diagnostics_, syntheticRange)) {
            return false;
        }
    }

    if (options.linkBinary) {
        const std::filesystem::path linker = "clang++";
        std::string command = linker.string() + " " + shellQuote(options.objectOutput) + " " + shellQuote(runtimeObjectPath()) + " -o " + shellQuote(options.binaryOutput);
        if (std::system(command.c_str()) != 0) {
            diagnostics_.error(syntheticRange, "linker invocation failed: " + command);
            return false;
        }
    }

    return !diagnostics_.hasErrors();
}

}  // namespace axc
