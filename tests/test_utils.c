#include <stdio.h>
#include <stdlib.h>

#include "structs.h"
#include "throttle.h"
#include "utils.h"
#include "test_common.h"
#include "test_utils.h"

static test_suite_t g_suite;

static dataMessage_t make_msg(int32_t value, TickType_t ts, DataTimeStatus_t st)
{
    dataMessage_t m;
    m.id = wheelFRId;
    m.data = value;
    m.timestamp = ts;
    m.timeStatus = st;
    return m;
}

static void test_sort4(void)
{
    test_print_case("sort4");
    int32_t v[4] = {40, 10, 30, 20};
    sort4(v, 4);
    test_check_i32(&g_suite, "sort4: v[0]", v[0], 10);
    test_check_i32(&g_suite, "sort4: v[1]", v[1], 20);
    test_check_i32(&g_suite, "sort4: v[2]", v[2], 30);
    test_check_i32(&g_suite, "sort4: v[3]", v[3], 40);
}

static void test_update_time_status(void)
{
    test_print_case("updateDataTimeStatus");
    dataMessage_t d[3];
    d[0] = make_msg(1, 100, STATUS_TIME_OK);
    d[1] = make_msg(2, 190, STATUS_TIME_OK);
    d[2] = make_msg(3, 0, STATUS_NOT_READY);

    updateDataTimeStatus(d, 3, 50, 200);

    test_check_true(&g_suite, "updateDataTimeStatus: old data timeout", d[0].timeStatus == STATUS_TIMEOUT);
    test_check_true(&g_suite, "updateDataTimeStatus: fresh data stays OK", d[1].timeStatus == STATUS_TIME_OK);
    test_check_true(&g_suite, "updateDataTimeStatus: NOT_READY untouched", d[2].timeStatus == STATUS_NOT_READY);

    dataMessage_t single;
    single = make_msg(1, 100, STATUS_TIME_OK);
    updateDataTimeStatus(&single, 1, 50, 200);
    test_check_true(&g_suite, "updateDataTimeStatus: single data timeout", single.timeStatus == STATUS_TIMEOUT);

    single = make_msg(1, 170, STATUS_TIME_OK);
    updateDataTimeStatus(&single, 1, 50, 200);
    test_check_true(&g_suite, "updateDataTimeStatus: single data valid", single.timeStatus == STATUS_TIME_OK);
}

static void test_calculate_speed(void)
{
    test_print_case("calculateSpeed");
    dataMessage_t w[4];

    w[0] = make_msg(1000, 10, STATUS_TIME_OK);
    w[1] = make_msg(1020, 10, STATUS_TIME_OK);
    w[2] = make_msg(990, 10, STATUS_TIME_OK);
    w[3] = make_msg(3000, 10, STATUS_TIME_OK);

    test_check_i32(&g_suite, "calculateSpeed: 4 coherent wheels", calculateSpeed(w, 4), 1010);

    w[0] = make_msg(1000, 10, STATUS_TIME_OK);
    w[1] = make_msg(1020, 10, STATUS_TIME_OK);
    w[2] = make_msg(990, 10, STATUS_TIME_OK);
    w[3] = make_msg(3000, 10, STATUS_TIMEOUT);

    test_check_i32(&g_suite, "calculateSpeed: 3 coherent wheels", calculateSpeed(w, 4), 1003);

    w[0] = make_msg(1000, 10, STATUS_TIME_OK);
    w[1] = make_msg(1020, 10, STATUS_TIME_OK);
    w[2] = make_msg(2300, 10, STATUS_TIME_OK);
    w[3] = make_msg(3000, 10, STATUS_TIMEOUT);

    test_check_i32(&g_suite, "calculateSpeed: 3 wheels, first 2 coherant", calculateSpeed(w, 4), 1010);

    w[0] = make_msg(1200, 10, STATUS_TIME_OK);
    w[1] = make_msg(1210, 10, STATUS_TIME_OK);
    w[2] = make_msg(3000, 10, STATUS_TIMEOUT);
    w[3] = make_msg(3100, 10, STATUS_TIMEOUT);

    test_check_i32(&g_suite, "calculateSpeed: 2 close wheels", calculateSpeed(w, 4), 1205);

    w[0] = make_msg(1200, 10, STATUS_TIME_OK);
    w[1] = make_msg(1400, 10, STATUS_TIME_OK);
    w[2] = make_msg(3000, 10, STATUS_TIMEOUT);
    w[3] = make_msg(3100, 10, STATUS_TIMEOUT);

    test_check_i32(&g_suite, "calculateSpeed: 2 incoherent wheels -> 0", calculateSpeed(w, 4), 0);
}

static void test_get_user_throttle_command(void)
{
    test_print_case("GetUserThrottleCommand");

    potmin[0] = 100;
    potmax[0] = 900;
    potmin[1] = 100;
    potmax[1] = 900;

    throtdead = 0.0f;
    throtmax = 100.0f;
    throtmin = -100.0f;
    regenRpm = 1200.0f;
    regenendRpm = 200.0f;
    regenmax = -30.0f;
    regenBrake = -20.0f;
    ThrotRpmFilt = 1000.0f;

    dataMessage_t pot1 = make_msg(900, 10, STATUS_TIME_OK);
    dataMessage_t pot2_timeout = make_msg(900, 10, STATUS_TIMEOUT);
    dataMessage_t pot2_not_ready = make_msg(900, 10, STATUS_NOT_READY);

    test_check_float(
        &g_suite,
        "GetUserThrottleCommand: NULL pointer -> 0",
        GetUserThrottleCommand(NULL, &pot2_timeout, 0, false),
        0.0f,
        0.001f);

    test_check_float(
        &g_suite,
        "GetUserThrottleCommand: only pot1 valid -> limp 50%",
        GetUserThrottleCommand(&pot1, &pot2_timeout, 0, false),
        50.0f,
        0.001f);

    test_check_float(
        &g_suite,
        "GetUserThrottleCommand: both invalid -> 0",
        GetUserThrottleCommand(&pot2_not_ready, &pot2_timeout, 0, false),
        0.0f,
        0.001f);

    pot1 = make_msg(450, 10, STATUS_TIME_OK);
    dataMessage_t pot2 = make_msg(450, 10, STATUS_TIME_OK);
    test_check_float(
        &g_suite,
        "GetUserThrottleCommand: both valid -> 50%",
        GetUserThrottleCommand(&pot1, &pot2, 0, false),
        50.0f,
        0.001f);
}

int run_utils_tests(void)
{
    test_suite_init(&g_suite);
    test_print_banner("UTILS TEST SUITE");

    test_sort4();
    test_update_time_status();
    test_calculate_speed();
    test_get_user_throttle_command();

    test_print_summary(&g_suite);
    return g_suite.failed;
}

int main(void)
{
    return (run_utils_tests() == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
