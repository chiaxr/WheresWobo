#pragma once

// A deliberately tiny test harness: TEST(name) registers a function, CHECK
// records a failure and carries on, REQUIRE stops the test.

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace wobo_test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry();
void fail(const char* file, int line, const std::string& msg);

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct RequireFailed {};

}  // namespace wobo_test

#define WOBO_CAT2(a, b) a##b
#define WOBO_CAT(a, b) WOBO_CAT2(a, b)

#define TEST(name)                                                                     \
    static void WOBO_CAT(test_fn_, __LINE__)();                                        \
    static wobo_test::Registrar WOBO_CAT(test_reg_, __LINE__)(name, WOBO_CAT(test_fn_, __LINE__)); \
    static void WOBO_CAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) wobo_test::fail(__FILE__, __LINE__, #cond);        \
    } while (0)

#define REQUIRE(cond)                                                   \
    do {                                                                \
        if (!(cond)) {                                                  \
            wobo_test::fail(__FILE__, __LINE__, #cond);                 \
            throw wobo_test::RequireFailed{};                           \
        }                                                               \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                              \
    do {                                                                                   \
        const double wobo_a_ = (a), wobo_b_ = (b);                                         \
        if (!(std::fabs(wobo_a_ - wobo_b_) <= (tol)))                                      \
            wobo_test::fail(__FILE__, __LINE__,                                            \
                            std::string(#a " ~= " #b " (") + std::to_string(wobo_a_) + " vs " + \
                                std::to_string(wobo_b_) + ")");                            \
    } while (0)
