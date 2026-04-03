#include "framework/TestRegistry.h"

#include <memory>

#include "axc/Support/QualifiedName.h"

AXC_TEST(QualifiedName_BuildsFromMemberChains) {
    const axc::SourceRange range {axc::SourceLocation {0}, axc::SourceLocation {1}};
    auto root = std::make_unique<axc::DeclRefExpr>("math", range);
    auto middle = std::make_unique<axc::MemberExpr>(std::move(root), "ops", false, range);
    axc::MemberExpr full(std::move(middle), "add", false, range);

    const auto qualified = axc::qualifiedNameFromExpr(full);
    AXC_EXPECT(qualified.has_value());
    AXC_EXPECT_EQ(*qualified, std::string("math.ops.add"));
    AXC_EXPECT_EQ(axc::lastQualifiedSegment(*qualified), std::string("add"));
    return true;
}

AXC_TEST(QualifiedName_StopsAtNullSafeMembers) {
    const axc::SourceRange range {axc::SourceLocation {0}, axc::SourceLocation {1}};
    auto root = std::make_unique<axc::DeclRefExpr>("user", range);
    axc::MemberExpr member(std::move(root), "name", true, range);
    AXC_EXPECT(!axc::qualifiedNameFromExpr(member).has_value());
    return true;
}
