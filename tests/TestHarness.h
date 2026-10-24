#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <chrono>

namespace TestFramework {

struct TestCase {
    std::string name;
    std::function<void()> func;
};

class TestRegistry {
public:
    static TestRegistry& Instance() {
        static TestRegistry s_instance;
        return s_instance;
    }

    void Register(const std::string& name, std::function<void()> func) {
        m_tests.push_back({ name, std::move(func) });
    }

    int RunAll() {
        int passed = 0;
        int failed = 0;
        std::cout << "=========================================================\n";
        std::cout << "  Windows Game Explorer \u2014 Dedicated C++ Test Suite\n";
        std::cout << "=========================================================\n\n";

        for (const auto& t : m_tests) {
            std::cout << " RUN  " << t.name << " ... ";
            auto start = std::chrono::high_resolution_clock::now();
            try {
                t.func();
                auto end = std::chrono::high_resolution_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                std::cout << "[PASS] (" << ms << " ms)\n";
                ++passed;
            } catch (const std::exception& ex) {
                std::cout << "[FAIL]\n";
                std::cerr << "       Error: " << ex.what() << "\n";
                ++failed;
            } catch (...) {
                std::cout << "[FAIL]\n";
                std::cerr << "       Unknown non-standard exception caught.\n";
                ++failed;
            }
        }

        std::cout << "\n---------------------------------------------------------\n";
        std::cout << "Test Summary: " << passed << " passed, " << failed << " failed (Total " << m_tests.size() << ")\n";
        std::cout << "=========================================================\n";
        return (failed == 0) ? 0 : 1;
    }

private:
    std::vector<TestCase> m_tests;
};

struct AutoRegister {
    AutoRegister(const std::string& name, std::function<void()> func) {
        TestRegistry::Instance().Register(name, std::move(func));
    }
};

#define TEST_CASE(name) \
    static void Test_##name(); \
    static ::TestFramework::AutoRegister s_reg_##name(#name, Test_##name); \
    static void Test_##name()

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            throw std::runtime_error("Assertion failed: " #expr " at " __FILE__ ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            throw std::runtime_error("Assertion failed: (" #a " == " #b ") at " __FILE__ ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if (!((a) != (b))) { \
            throw std::runtime_error("Assertion failed: (" #a " != " #b ") at " __FILE__ ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

} // namespace TestFramework
