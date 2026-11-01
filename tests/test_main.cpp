#include <cstring>

#include "test.h"

namespace wobo_test {

namespace {
int g_failures = 0;
}

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

void fail(const char* file, int line, const std::string& msg) {
    ++g_failures;
    std::printf("    FAIL %s:%d: %s\n", file, line, msg.c_str());
}

}  // namespace wobo_test

// Usage: wobo_tests [substring]  -- runs tests whose name contains it.
int main(int argc, char** argv) {
    using namespace wobo_test;
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    int failed_tests = 0;
    for (const TestCase& t : registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        const int before = g_failures;
        std::printf("[ RUN  ] %s\n", t.name);
        try {
            t.fn();
        } catch (const RequireFailed&) {
        } catch (const std::exception& e) {
            fail(__FILE__, __LINE__, std::string("exception: ") + e.what());
        }
        ++run;
        if (g_failures != before) {
            ++failed_tests;
            std::printf("[ FAIL ] %s\n", t.name);
        }
    }
    std::printf("\n%d tests, %d failed\n", run, failed_tests);
    return failed_tests == 0 ? 0 : 1;
}
