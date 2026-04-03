#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace axc::unit {

using TestFunction = bool (*)();

struct TestCase {
    std::string name {};
    TestFunction function = nullptr;
};

std::vector<TestCase>& registry();

struct TestRegistration {
    TestRegistration(const char* name, TestFunction function);
};

bool expectTrue(bool condition, std::string_view expression, const char* file, int line);

template <typename Lhs, typename Rhs>
bool expectEqual(const Lhs& lhs, const Rhs& rhs, std::string_view lhsExpr, std::string_view rhsExpr, const char* file, int line) {
    if (lhs == rhs) {
        return true;
    }
    std::cerr << file << ':' << line << ": FAIL: expected " << lhsExpr << " == " << rhsExpr << '\n';
    return false;
}

bool expectContains(std::string_view haystack,
                    std::string_view needle,
                    std::string_view haystackExpr,
                    std::string_view needleExpr,
                    const char* file,
                    int line);

}  // namespace axc::unit

#define AXC_TEST(Name)                                                                                                                     \
    static bool Name();                                                                                                                    \
    static ::axc::unit::TestRegistration registration_##Name(#Name, &Name);                                                               \
    static bool Name()

#define AXC_EXPECT(Condition)                                                                                                              \
    do {                                                                                                                                   \
        if (!::axc::unit::expectTrue((Condition), #Condition, __FILE__, __LINE__)) {                                                      \
            return false;                                                                                                                  \
        }                                                                                                                                  \
    } while (false)

#define AXC_EXPECT_EQ(Lhs, Rhs)                                                                                                            \
    do {                                                                                                                                   \
        if (!::axc::unit::expectEqual((Lhs), (Rhs), #Lhs, #Rhs, __FILE__, __LINE__)) {                                                    \
            return false;                                                                                                                  \
        }                                                                                                                                  \
    } while (false)

#define AXC_EXPECT_CONTAINS(Haystack, Needle)                                                                                              \
    do {                                                                                                                                   \
        if (!::axc::unit::expectContains((Haystack), (Needle), #Haystack, #Needle, __FILE__, __LINE__)) {                                \
            return false;                                                                                                                  \
        }                                                                                                                                  \
    } while (false)
