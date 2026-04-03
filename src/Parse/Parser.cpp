#include "axc/Parse/Parser.h"

namespace axc {

SourceRange Parser::combine(SourceRange lhs, SourceRange rhs) const {
    return SourceRange {lhs.begin, rhs.end};
}

}  // namespace axc
