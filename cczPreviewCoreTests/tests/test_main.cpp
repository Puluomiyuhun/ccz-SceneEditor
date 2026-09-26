#include "test_framework.h"

#include <algorithm>
#include <iostream>
#include <string>

std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> tests;
    return tests;
}

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : std::string();
    std::size_t selected = 0;
    std::size_t failed = 0;

    for (const TestCase& test : test_registry()) {
        if (!filter.empty() && std::string(test.name).find(filter) == std::string::npos) {
            continue;
        }
        ++selected;
        try {
            test.function();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }

    if (selected == 0) {
        std::cerr << "[FAIL] no tests selected\n";
        return 1;
    }
    return failed == 0 ? 0 : 1;
}

