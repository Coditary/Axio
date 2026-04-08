/// @file
/// @brief CLI-oriented orchestration of source loading, front-end validation, and LLVM emission.

#include "CompilerWorkflow.h"

#include <iostream>

#include "../Module/ModuleLoader.h"
#include "axc/AST/ASTPrinter.h"
#include "axc/Lex/Lexer.h"
#include "axc/Parse/Parser.h"
#include "axc/Sema/Sema.h"
#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace axc {

CompilerWorkflow::CompilerWorkflow(const CompileOptions& options) : options_(options) {}

bool CompilerWorkflow::run() const {
    SourceManager sourceManager;
    std::string errorMessage;
    if (!loadSource(sourceManager, errorMessage)) {
        std::cerr << "error: " << errorMessage << "\n";
        return false;
    }

    DiagnosticEngine diagnostics(sourceManager);
    TranslationUnit unit;

    if (!runFrontEnd(unit, sourceManager, diagnostics)) {
        diagnostics.renderAll(std::cerr);
        return false;
    }

    if (shouldStopAfterAstDump(unit)) {
        ASTPrinter printer(std::cout);
        printer.print(unit);
        return !diagnostics.hasErrors();
    }

    if (!finalizeDiagnostics(diagnostics)) {
        return false;
    }

    if (options_.checkOnly || options_.dumpAst) {
        return !diagnostics.hasErrors();
    }

    auto emitOptions = buildEmitOptions();
    if (!emitOptions.has_value()) {
        return false;
    }

    LLVMEmitter emitter(sourceManager, diagnostics);
    const bool ok = emitter.emit(unit, *emitOptions);
    if (!ok || diagnostics.hasErrors()) {
        diagnostics.renderAll(std::cerr);
        return false;
    }

    if (!diagnostics.diagnostics().empty() && !diagnostics.hasRenderedAll()) {
        diagnostics.renderAll(std::cerr);
    }

    return true;
}

bool CompilerWorkflow::loadSource(SourceManager& sourceManager, std::string& errorMessage) const {
    return sourceManager.loadFromFile(options_.inputFile, errorMessage);
}

bool CompilerWorkflow::shouldStopAfterAstDump(const TranslationUnit& unit) const {
    (void)unit;
    return options_.dumpAst;
}

bool CompilerWorkflow::runFrontEnd(TranslationUnit& unit, SourceManager& sourceManager, DiagnosticEngine& diagnostics) const {
    ModuleLoader moduleLoader(diagnostics);
    moduleLoader.loadInto(unit, options_.inputFile);
    if (diagnostics.hasErrors() || options_.dumpAst) {
        return !diagnostics.hasErrors();
    }

    Sema sema(diagnostics);
    sema.analyze(unit);

    return !diagnostics.hasErrors();
}

bool CompilerWorkflow::finalizeDiagnostics(DiagnosticEngine& diagnostics) const {
    if (diagnostics.hasErrors()) {
        return false;
    }

    if (!diagnostics.diagnostics().empty() && !diagnostics.hasRenderedAll()) {
        diagnostics.renderAll(std::cerr);
    }

    if (options_.checkOnly && diagnostics.diagnostics().empty()) {
        std::cout << '\n';
    }

    return true;
}

std::optional<EmitOptions> CompilerWorkflow::buildEmitOptions() const {
    const std::filesystem::path baseOutput = options_.outputFile.empty() ? options_.inputFile.stem() : options_.outputFile;
    EmitOptions emitOptions;
    if (options_.emitLlvmIr) {
        emitOptions.llvmIrOutput = baseOutput;
        emitOptions.llvmIrOutput += ".ll";
    }
    if (options_.emitObject) {
        emitOptions.objectOutput = baseOutput;
        emitOptions.objectOutput += ".o";
    }
    if (options_.emitBinary) {
        emitOptions.binaryOutput = baseOutput;
        emitOptions.linkBinary = true;
    }
    return emitOptions;
}

}  // namespace axc
