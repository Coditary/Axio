#include "axc/Driver/Compiler.h"

#include "CompilerWorkflow.h"

namespace axc {

bool Compiler::compile(const CompileOptions& options) const {
    return CompilerWorkflow(options).run();
}

}  // namespace axc
