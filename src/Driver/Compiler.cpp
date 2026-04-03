#include "axc/Driver/Compiler.h"

#include <filesystem>
#include <iostream>
#include <string>

#include "axc/AST/ASTPrinter.h"
#include "axc/Codegen/LLVMEmitter.h"
#include "ModuleLoader.h"
#include "axc/Lex/Lexer.h"
#include "axc/Meta/MetaPipeline.h"
#include "axc/Parse/Parser.h"
#include "axc/Sema/Sema.h"
#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace axc {

bool Compiler::compile(const CompileOptions& options) const {
    SourceManager sourceManager;
    std::string errorMessage;
    if (!sourceManager.loadFromFile(options.inputFile, errorMessage)) {
        std::cerr << "error: " << errorMessage << "\n";
        return false;
    }

    DiagnosticEngine diagnostics(sourceManager);
    Lexer lexer(sourceManager, diagnostics);
    std::vector<Token> tokens = lexer.lex();

    Parser parser(std::move(tokens), diagnostics);
    TranslationUnit unit = parser.parseTranslationUnit();

    ModuleLoader moduleLoader(diagnostics);
    moduleLoader.loadInto(unit, options.inputFile);

    Sema sema(diagnostics);
    sema.analyze(unit);

    MetaPipeline meta(sourceManager, diagnostics);
    meta.run(unit);

    if (options.dumpAst) {
        ASTPrinter printer(std::cout);
        printer.print(unit);
    }

    if (diagnostics.hasErrors()) {
        diagnostics.renderAll(std::cerr);
        return false;
    }

    if (!diagnostics.diagnostics().empty() && !diagnostics.hasRenderedAll()) {
        diagnostics.renderAll(std::cerr);
    }

    if (options.checkOnly || options.dumpAst) {
        return !diagnostics.hasErrors();
    }

    const std::filesystem::path baseOutput = options.outputFile.empty() ? options.inputFile.stem() : options.outputFile;
    EmitOptions emitOptions;
    if (options.emitLlvmIr) {
        emitOptions.llvmIrOutput = baseOutput;
        emitOptions.llvmIrOutput += ".ll";
    }
    if (options.emitObject) {
        emitOptions.objectOutput = baseOutput;
        emitOptions.objectOutput += ".o";
    }
    if (options.emitBinary) {
        emitOptions.binaryOutput = baseOutput;
        emitOptions.linkBinary = true;
    }

    LLVMEmitter emitter(sourceManager, diagnostics);
    const bool ok = emitter.emit(unit, emitOptions);
    if (!ok || diagnostics.hasErrors()) {
        diagnostics.renderAll(std::cerr);
        return false;
    }

    if (!diagnostics.diagnostics().empty() && !diagnostics.hasRenderedAll()) {
        diagnostics.renderAll(std::cerr);
    }

    return true;
}

}  // namespace axc
