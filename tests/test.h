// A test harness small enough to read in a minute, so the tests need nothing
// the project does not already have.

#pragma once

#include <iostream>
#include <string>
#include <vector>

namespace hawser_tests {

struct TestCase {
    const char* name;
    void (*run)();
};

std::vector<TestCase>& test_cases();
void record_failure(const char* file, int line, const std::string& what);

struct TestRegistration {
    TestRegistration(const char* name, void (*run)()) { test_cases().push_back(TestCase{name, run}); }
};

}  // namespace hawser_tests

#define TEST(name)                                                              \
    static void name();                                                         \
    static const hawser_tests::TestRegistration registration_of_##name(#name, name); \
    static void name()

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) hawser_tests::record_failure(__FILE__, __LINE__, #condition); \
    } while (false)

#define CHECK_THROWS(expression)                                                \
    do {                                                                        \
        bool did_throw = false;                                                 \
        try {                                                                   \
            (void)(expression);                                                 \
        } catch (const hawser::HawserError&) {                                  \
            did_throw = true;                                                   \
        }                                                                       \
        if (!did_throw) hawser_tests::record_failure(__FILE__, __LINE__, "expected a HawserError from " #expression); \
    } while (false)
