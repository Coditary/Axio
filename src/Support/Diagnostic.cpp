#include "axc/Support/Diagnostic.h"

#include <algorithm>
#include <sstream>

#include "axc/Support/SourceManager.h"

namespace axc {

DiagnosticEngine::DiagnosticEngine(const SourceManager& sourceManager) : sourceManager_(sourceManager) {}

void DiagnosticEngine::error(SourceRange range, std::string message) {
    report(DiagnosticSeverity::Error, range, std::move(message));
}

void DiagnosticEngine::warning(SourceRange range, std::string message) {
    report(DiagnosticSeverity::Warning, range, std::move(message));
}

void DiagnosticEngine::note(SourceRange range, std::string message) {
    report(DiagnosticSeverity::Note, range, std::move(message));
}

bool DiagnosticEngine::hasErrors() const {
    return std::any_of(diagnostics_.begin(), diagnostics_.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

const std::vector<Diagnostic>& DiagnosticEngine::diagnostics() const {
    return diagnostics_;
}

bool DiagnosticEngine::hasRenderedAll() const {
    return renderedAll_;
}

void DiagnosticEngine::renderAll(std::ostream& out) const {
    renderedAll_ = true;
    for (const Diagnostic& diagnostic : diagnostics_) {
        const LineColumn lc = sourceManager_.lineColumn(diagnostic.range.begin);
        out << sourceManager_.path().string() << ':' << lc.line << ':' << lc.column << ": " << severityName(diagnostic.severity)
            << ": " << diagnostic.message << '\n';

        const auto lineInfo = sourceManager_.lineAt(diagnostic.range.begin);
        if (!lineInfo.has_value()) {
            continue;
        }

        out << "  " << lineInfo->lineNumber << " | " << lineInfo->text << '\n';
        out << "    | ";

        const std::size_t column = lc.column > 0 ? lc.column - 1 : 0;
        for (std::size_t i = 0; i < column; ++i) {
            out << (lineInfo->text[i] == '\t' ? '\t' : ' ');
        }

        std::size_t width = diagnostic.range.end.offset > diagnostic.range.begin.offset
            ? diagnostic.range.end.offset - diagnostic.range.begin.offset
            : 1;
        width = std::max<std::size_t>(1, width);
        for (std::size_t i = 0; i < width; ++i) {
            out << '^';
        }
        out << '\n';
    }
}

void DiagnosticEngine::report(DiagnosticSeverity severity, SourceRange range, std::string message) {
    diagnostics_.push_back(Diagnostic {severity, range, std::move(message)});
}

std::string DiagnosticEngine::severityName(DiagnosticSeverity severity) const {
    switch (severity) {
        case DiagnosticSeverity::Error:
            return "error";
        case DiagnosticSeverity::Warning:
            return "warning";
        case DiagnosticSeverity::Note:
            return "note";
    }
    return "diagnostic";
}

}  // namespace axc
