#include <stdio.h>
#include <stdlib.h>

#include "throttle.h"
#include "test_common.h"
#include "test_throttle.h"

extern float throttleRamped;

static test_suite_t g_suite;

static void setup_defaults(void)
{
    potmin[0] = 100;
    potmax[0] = 900;
    potmin[1] = 100;
    potmax[1] = 900;

    regenRpm = 1200.0f;
    regenendRpm = 200.0f;
    regenmax = -30.0f;
    regenBrake = -20.0f;

    throtmax = 100.0f;
    throtmin = -100.0f;
    throtdead = 0.0f;

    ThrotRpmFilt = 200.0f;

    regenRamp = 5.0f;
    actualThrottleRamp = 10.0f;
    speedLimit = 3000;
}

static void test_check_and_limit_range(void)
{
    test_print_case("CheckAndLimitRange");
    int v1 = 500;
    int v2 = -500;
    int v3 = 5000;

    test_check_true(&g_suite, "CheckAndLimitRange: value in range", CheckAndLimitRange(&v1, 0));
    test_check_true(&g_suite, "CheckAndLimitRange: keep in-range value", v1 == 500);

    test_check_true(&g_suite, "CheckAndLimitRange: too low becomes min + false", !CheckAndLimitRange(&v2, 0));
    test_check_true(&g_suite, "CheckAndLimitRange: clipped low", v2 == 100);

    test_check_true(&g_suite, "CheckAndLimitRange: too high becomes min + false", !CheckAndLimitRange(&v3, 1));
    test_check_true(&g_suite, "CheckAndLimitRange: clipped high fault to min", v3 == 200);
}

static void test_normalize(void)
{
    test_print_case("NormalizeThrottle");
    test_check_float(&g_suite, "NormalizeThrottle: lower bound", NormalizeThrottle(100, 0), 0.0f, 0.001f);
    test_check_float(&g_suite, "NormalizeThrottle: midpoint", NormalizeThrottle(500, 0), 50.0f, 0.001f);
    test_check_float(&g_suite, "NormalizeThrottle: upper bound", NormalizeThrottle(900, 0), 100.0f, 0.001f);
    test_check_float(&g_suite, "NormalizeThrottle: invalid idx", NormalizeThrottle(500, 2), 0.0f, 0.001f);
}

static void test_calc_throttle(void)
{
    test_print_case("CalcThrottle");
    float out;

    out = CalcThrottle(0.0f, 50, false);
    test_check_float(&g_suite, "CalcThrottle: below deadzone -> 0", out, 0.0f, 0.001f);

    out = CalcThrottle(100.0f, 1500, false);
    test_check_float(&g_suite, "CalcThrottle: full accel stays positive", out, 100.0f, 0.001f);

    out = CalcThrottle(40.0f, 100, true);
    test_check_float(&g_suite, "CalcThrottle: brake + very low speed -> 0", out, 0.0f, 0.001f);
}

static void test_temperature_derate(void)
{
    test_print_case("TemperatureDerate");
    float cmd = 80.0f;
    test_check_true(&g_suite, "TemperatureDerate: below max no derate", !TemperatureDerate(40, 60, &cmd));
    test_check_float(&g_suite, "TemperatureDerate: keep value below max", cmd, 80.0f, 0.001f);

    cmd = 80.0f;
    test_check_true(&g_suite, "TemperatureDerate: slight overtemp derates", TemperatureDerate(61, 60, &cmd));
    test_check_float(&g_suite, "TemperatureDerate: clamps to 50", cmd, 50.0f, 0.001f);
}

static void test_ramp_throttle(void)
{
    test_print_case("RampThrottle");
    float out;

    throttleRamped = 0.0f;
    actualThrottleRamp = 10.0f;
    regenRamp = 5.0f;

    out = RampThrottle(40.0f);
    test_check_float(&g_suite, "RampThrottle: ramps up by actualThrottleRamp", out, 10.0f, 0.001f);

    out = RampThrottle(40.0f);
    test_check_float(&g_suite, "RampThrottle: second step", out, 20.0f, 0.001f);

    out = RampThrottle(5.0f);
    test_check_float(&g_suite, "RampThrottle: lower positive command no ramp", out, 5.0f, 0.001f);
}

int run_throttle_tests(void)
{
    setup_defaults();
    test_suite_init(&g_suite);
    test_print_banner("THROTTLE TEST SUITE");

    test_check_and_limit_range();
    test_normalize();
    test_calc_throttle();
    test_temperature_derate();
    test_ramp_throttle();

    test_print_summary(&g_suite);
    return g_suite.failed;
}

int main(void)
{
    return (run_throttle_tests() == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
