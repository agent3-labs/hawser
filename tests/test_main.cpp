#include "test.h"

namespace hawser_tests {

namespace {
int failure_count = 0;
}

std::vector<TestCase>& test_cases() {
    static std::vector<TestCase> cases;
    return cases;
}

void record_failure(const char* file, int line, const std::string& what) {
    ++failure_count;
    std::cerr << "  FAILED " << file << ":" << line << ": " << what << "\n";
}

}  // namespace hawser_tests

int main() {
    using namespace hawser_tests;
    int failed_test_count = 0;
    for (const TestCase& test_case : test_cases()) {
        const int failures_before = failure_count;
        test_case.run();
        if (failure_count != failures_before) {
            ++failed_test_count;
            std::cerr << "FAILED " << test_case.name << "\n";
        }
    }
    std::cout << test_cases().size() - failed_test_count << " of " << test_cases().size() << " tests passed\n";
    return failed_test_count == 0 ? 0 : 1;
}
