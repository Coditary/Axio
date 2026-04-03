#include "framework/TestRegistry.h"

#include <exception>
#include <iostream>

int main() {
    int failures = 0;
    for (const auto& test : axc::unit::registry()) {
        try {
            if (!test.function()) {
                ++failures;
            }
        } catch (const std::exception& exception) {
            std::cerr << "EXCEPTION in " << test.name << ": " << exception.what() << '\n';
            ++failures;
        } catch (...) {
            std::cerr << "EXCEPTION in " << test.name << ": unknown exception\n";
            ++failures;
        }
    }

    if (failures != 0) {
        std::cerr << failures << " unit test(s) failed\n";
        return 1;
    }
    return 0;
}
