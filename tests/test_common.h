#ifndef TEST_COMMON_H
#define TEST_COMMON_H

typedef struct
{
    int total;
    int failed;
} test_suite_t;

void test_suite_init(test_suite_t *suite);
void test_print_banner(const char *title);
void test_print_case(const char *func_name);
void test_check_true(test_suite_t *suite, const char *name, int cond);
void test_check_i32(test_suite_t *suite, const char *name, int got, int expected);
void test_check_float(test_suite_t *suite, const char *name, float got, float expected, float eps);
void test_print_summary(const test_suite_t *suite);

#endif
