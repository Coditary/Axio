#include "axc/Parse/Parser.h"

#include "../../Internal/ParserInternal.h"

namespace axc {

std::unique_ptr<Expr> Parser::parseExpression() {
    return detail::ExpressionParser(*this).parseExpression();
}

}  // namespace axc
