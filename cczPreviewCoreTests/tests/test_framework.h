#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

struct TestCase {
    const char* name;
    void (*function)();
};

std::vector<TestCase>& test_registry();

struct TestRegistrar {
    TestRegistrar(const char* name, void (*function)()) {
        test_registry().push_back({name, function});
    }
};

#define CCZ_TEST(name) \
    static void name(); \
    static TestRegistrar registrar_##name(#name, &name); \
    static void name()

#define CCZ_REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error(std::string("requirement failed: ") + #condition); \
        } \
    } while (false)

#define CCZ_REQUIRE_EQ(actual, expected) \
    do { \
        const auto& ccz_actual_value = (actual); \
        const auto& ccz_expected_value = (expected); \
        if (!(ccz_actual_value == ccz_expected_value)) { \
            throw std::runtime_error(std::string("equality failed: ") + #actual + " == " + #expected); \
        } \
    } while (false)


