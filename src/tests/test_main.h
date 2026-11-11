// Tiny test harness.
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase {
    const char* name;
    std::function<void()> fn;
};
std::vector<TestCase>& allTests();
int& failureCount();

inline std::string testToString(const std::string& s) { return s; }
inline std::string testToString(const char* s) { return s; }
template <class T> std::string testToString(const T& v) { return std::to_string(v); }

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> fn) { allTests().push_back({name, std::move(fn)}); }
};

#define TEST(name)                                                   \
    static void test_##name();                                       \
    static TestRegistrar reg_##name(#name, test_##name);             \
    static void test_##name()

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
            ++failureCount();                                                             \
        }                                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                                                  \
    do {                                                                                                \
        auto va_ = (a);                                                                                 \
        auto vb_ = (b);                                                                                 \
        if (!(va_ == vb_)) {                                                                            \
            std::printf("  FAIL %s:%d: %s == %s (%s vs %s)\n", __FILE__, __LINE__, #a, #b,             \
                        testToString(va_).c_str(), testToString(vb_).c_str());                      \
            ++failureCount();                                                                           \
        }                                                                                               \
    } while (0)
