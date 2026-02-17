#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>

// ============================================================================
// Minimal test framework — colored output, pass/fail summary
// ============================================================================

static int _test_pass_count = 0;
static int _test_fail_count = 0;
static const char* _current_test_name = nullptr;

#define GREEN  "\033[32m"
#define RED    "\033[31m"
#define RESET  "\033[0m"

#define TEST(name) \
    static void test_##name(); \
    struct _test_reg_##name { \
        _test_reg_##name() { \
            _current_test_name = #name; \
            test_##name(); \
        } \
    }; \
    static void test_##name()

#define RUN_TEST(name) \
    do { \
        _current_test_name = #name; \
        test_##name(); \
    } while(0)

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            printf(RED "  [FAIL] " RESET "%s — %s (line %d)\n", _current_test_name, #expr, __LINE__); \
            _test_fail_count++; return; \
        } \
    } while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(expected, actual) \
    do { \
        auto _e = (expected); auto _a = (actual); \
        if (_e != _a) { \
            printf(RED "  [FAIL] " RESET "%s — expected != actual (line %d)\n", _current_test_name, __LINE__); \
            _test_fail_count++; return; \
        } \
    } while(0)

#define ASSERT_FLOAT_EQ(expected, actual, epsilon) \
    do { \
        double _e = (expected); double _a = (actual); \
        if (std::fabs(_e - _a) > (epsilon)) { \
            printf(RED "  [FAIL] " RESET "%s — expected %f, got %f (line %d)\n", _current_test_name, _e, _a, __LINE__); \
            _test_fail_count++; return; \
        } \
    } while(0)

#define ASSERT_STR_EQ(expected, actual) \
    do { \
        String _e = (expected); String _a = (actual); \
        if (!(_e == _a)) { \
            printf(RED "  [FAIL] " RESET "%s — expected \"%s\", got \"%s\" (line %d)\n", \
                   _current_test_name, _e.c_str(), _a.c_str(), __LINE__); \
            _test_fail_count++; return; \
        } \
    } while(0)

#define ASSERT_NAN(val) \
    do { \
        if (!std::isnan(val)) { \
            printf(RED "  [FAIL] " RESET "%s — expected NaN, got %f (line %d)\n", _current_test_name, (double)(val), __LINE__); \
            _test_fail_count++; return; \
        } \
    } while(0)

#define TEST_PASS() \
    do { \
        printf(GREEN "  [PASS] " RESET "%s\n", _current_test_name); \
        _test_pass_count++; \
    } while(0)

#define TEST_SUMMARY() \
    do { \
        printf("\n  %d passed, %d failed\n", _test_pass_count, _test_fail_count); \
        return _test_fail_count > 0 ? 1 : 0; \
    } while(0)

#define TEST_SUITE(name) \
    printf("\n=== %s ===\n", name)

#endif // TEST_FRAMEWORK_H
