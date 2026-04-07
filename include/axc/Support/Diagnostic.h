#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "axc/Support/SourceLocation.h"

namespace axc {

class SourceManager;

/// @brief Severity class for diagnostics emitted by all compiler phases.
enum class DiagnosticSeverity {
    Error,
    Warning,
    Note,
};

/// @brief Single diagnostic record with severity, range, and message text.
struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    SourceRange range {};
    std::string message {};
};

/// @brief Collects and renders diagnostics against one source file.
class DiagnosticEngine {
  public:
    /// @brief Create a diagnostic engine for one loaded source file.
    explicit DiagnosticEngine(const SourceManager& sourceManager);

    /// @brief Emit an error diagnostic.
    void error(SourceRange range, std::string message);
    /// @brief Emit a warning diagnostic.
    void warning(SourceRange range, std::string message);
    /// @brief Emit a note diagnostic.
    void note(SourceRange range, std::string message);

    /// @brief Returns whether any error-level diagnostic was emitted.
    [[nodiscard]] bool hasErrors() const;
    /// @brief Returns the collected diagnostic list.
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const;
    /// @brief Returns whether diagnostics were already rendered via `renderAll`.
    [[nodiscard]] bool hasRenderedAll() const;
    /// @brief Render all diagnostics with source context.
    void renderAll(std::ostream& out) const;

  private:
    void report(DiagnosticSeverity severity, SourceRange range, std::string message);
    std::string severityName(DiagnosticSeverity severity) const;

    const SourceManager& sourceManager_;
    std::vector<Diagnostic> diagnostics_ {};
    mutable bool renderedAll_ = false;
};

}  // namespace axc
