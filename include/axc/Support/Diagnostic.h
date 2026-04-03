#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "axc/Support/SourceLocation.h"

namespace axc {

class SourceManager;

enum class DiagnosticSeverity {
    Error,
    Warning,
    Note,
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    SourceRange range {};
    std::string message {};
};

class DiagnosticEngine {
  public:
    explicit DiagnosticEngine(const SourceManager& sourceManager);

    void error(SourceRange range, std::string message);
    void warning(SourceRange range, std::string message);
    void note(SourceRange range, std::string message);

    [[nodiscard]] bool hasErrors() const;
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const;
    [[nodiscard]] bool hasRenderedAll() const;
    void renderAll(std::ostream& out) const;

  private:
    void report(DiagnosticSeverity severity, SourceRange range, std::string message);
    std::string severityName(DiagnosticSeverity severity) const;

    const SourceManager& sourceManager_;
    std::vector<Diagnostic> diagnostics_ {};
    mutable bool renderedAll_ = false;
};

}  // namespace axc
