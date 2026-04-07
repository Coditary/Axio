#include "ModuleFileParser.h"

#include <sstream>

#include "axc/Lex/Lexer.h"
#include "axc/Parse/Parser.h"
#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace axc::detail {

bool ModuleFileParser::parse(const std::filesystem::path& path, TranslationUnit& unit, std::string& errorMessage) const {
    SourceManager sourceManager;
    if (!sourceManager.loadFromFile(path, errorMessage)) {
        return false;
    }

    DiagnosticEngine localDiagnostics(sourceManager);
    Lexer lexer(sourceManager, localDiagnostics);
    std::vector<Token> tokens = lexer.lex();
    Parser parser(std::move(tokens), localDiagnostics);
    unit = parser.parseTranslationUnit();

    if (!localDiagnostics.hasErrors()) {
        return true;
    }

    std::ostringstream rendered;
    localDiagnostics.renderAll(rendered);
    errorMessage = rendered.str();
    return false;
}

}  // namespace axc::detail
