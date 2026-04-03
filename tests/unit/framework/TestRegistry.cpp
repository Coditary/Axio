#include "framework/TestRegistry.h"

namespace axc::unit {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

TestRegistration::TestRegistration(const char* name, TestFunction function) {
    registry().push_back(TestCase {name, function});
}

bool expectTrue(bool condition, std::string_view expression, const char* file, int line) {
    if (condition) {
        return true;
    }
    std::cerr << file << ':' << line << ": FAIL: expected " << expression << '\n';
    return false;
}

bool expectContains(std::string_view haystack,
                    std::string_view needle,
                    std::string_view haystackExpr,
                    std::string_view needleExpr,
                    const char* file,
                    int line) {
    if (haystack.find(needle) != std::string_view::npos) {
        return true;
    }
    std::cerr << file << ':' << line << ": FAIL: expected " << haystackExpr << " to contain " << needleExpr << '\n';
    return false;
}

}  // namespace axc::unit
