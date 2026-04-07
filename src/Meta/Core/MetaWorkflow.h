#pragma once

#include "axc/AST/AST.h"

namespace axc {

class MetaPipeline;

namespace detail {

/// @brief Internal workflow adapter used by `MetaPipeline`.
class MetaWorkflow {
  public:
    /// @brief Create the workflow from a configured meta pipeline.
    explicit MetaWorkflow(const MetaPipeline& pipeline);

    /// @brief Execute the meta pipeline over one translation unit.
    void run(TranslationUnit& translationUnit) const;

  private:
    /// Immutable pipeline configuration being executed.
    const MetaPipeline& pipeline_;
};

}  // namespace detail

}  // namespace axc
