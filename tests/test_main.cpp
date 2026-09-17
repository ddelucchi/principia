#include <exception>
#include <iostream>
#include <string_view>

void test_foundation();
void test_progression_and_audits();
void test_replay();
void test_reality_001();
void test_reality_002_contact();
void test_solver_contracts();

int main()
{
    struct TestCase {
        std::string_view name;
        void (*function)();
    };

    const TestCase tests[]{
        {"foundation", &test_foundation},
        {"progression_and_audits", &test_progression_and_audits},
        {"replay", &test_replay},
        {"reality_test_001", &test_reality_001},
        {"reality_test_002_contact", &test_reality_002_contact},
        {"solver_contracts", &test_solver_contracts},
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.function();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
