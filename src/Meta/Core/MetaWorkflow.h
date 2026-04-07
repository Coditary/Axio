#pragma once

#include "axc/AST/AST.h"

namespace axc {

class MetaPipeline;

namespace detail {

class MetaWorkflow {
  public:
    explicit MetaWorkflow(const MetaPipeline& pipeline);

    void run(TranslationUnit& translationUnit) const;

  private:
    const MetaPipeline& pipeline_;
};

}  // namespace detail

}  // namespace axc
