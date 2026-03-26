#include <stdarg.h>
#include <stdio.h>

#include "test_common.h"

#define CLR_RED "\033[1;31m"
#define CLR_GRN "\033[1;32m"
#define CLR_YEL "\033[1;33m"
#define CLR_CYN "\033[1;36m"
#define CLR_RST "\033[0m"

void console_print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void test_suite_init(test_suite_t *suite)
{
    suite->total = 0;
    suite->failed = 0;
}

void test_print_banner(const char *title)
{
    printf("\n%s====================================================%s\n", CLR_CYN, CLR_RST);
    printf("%s%s%s\n", CLR_CYN, title, CLR_RST);
    printf("%s====================================================%s\n", CLR_CYN, CLR_RST);
}

void test_print_case(const char *func_name)
{
    printf("\n%s[TEST]%s Function: %s\n", CLR_CYN, CLR_RST, func_name);
}

static int float_close(float a, float b, float eps)
{
    float d = a - b;
    if (d < 0.0f)
    {
        d = -d;
    }
    return d <= eps;
}

void test_check_true(test_suite_t *suite, const char *name, int cond)
{
    suite->total++;
    if (cond)
    {
        printf("%s[PASS]%s %s\n", CLR_GRN, CLR_RST, name);
    }
    else
    {
        suite->failed++;
        printf("%s[FAIL]%s %s\n", CLR_RED, CLR_RST, name);
    }
}

void test_check_i32(test_suite_t *suite, const char *name, int got, int expected)
{
    suite->total++;
    if (got == expected)
    {
        printf("%s[PASS]%s %s -> got=%d expected=%d\n", CLR_GRN, CLR_RST, name, got, expected);
    }
    else
    {
        suite->failed++;
        printf("%s[FAIL]%s %s -> got=%d expected=%d\n", CLR_RED, CLR_RST, name, got, expected);
    }
}

void test_check_float(test_suite_t *suite, const char *name, float got, float expected, float eps)
{
    suite->total++;
    if (float_close(got, expected, eps))
    {
        printf("%s[PASS]%s %s -> got=%.3f expected=%.3f\n", CLR_GRN, CLR_RST, name, got, expected);
    }
    else
    {
        suite->failed++;
        printf("%s[FAIL]%s %s -> got=%.3f expected=%.3f\n", CLR_RED, CLR_RST, name, got, expected);
    }
}

void test_print_summary(const test_suite_t *suite)
{
    printf("\n%sResult:%s %d tests, %d failed\n", CLR_YEL, CLR_RST, suite->total, suite->failed);
}
