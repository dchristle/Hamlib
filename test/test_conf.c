/*
 * Hamlib frontend configuration tests
 * Copyright (c) 2026 by Hamlib Team
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "acutest.h"
#include "test_debug.h"
#include <stdio.h>
#include <string.h>

static void check_ptt_config(RIG *rig, const char *expected)
{
    char actual[32];
    hamlib_token_t token = rig_token_lookup(rig, "ptt_type");

    TEST_ASSERT(token != RIG_CONF_END);
    TEST_ASSERT(rig_get_conf2(rig, token, actual, sizeof(actual)) == RIG_OK);
    TEST_CHECK_(strcmp(actual, expected) == 0,
                "PTT configuration: expected %s, got %s", expected, actual);
}

static void test_ptt_overrides_are_instance_local(void)
{
    const char *values[] =
    {
        "None", "DTR", "RTS", "Parallel", "CM108",
        "GPIO", "GPION", "RIGMICDATA", "RIG"
    };
    RIG *configured = rig_init(RIG_MODEL_DUMMY);
    RIG *existing = rig_init(RIG_MODEL_DUMMY);

    TEST_ASSERT(configured != NULL && existing != NULL);
    hamlib_token_t token = rig_token_lookup(configured, "ptt_type");
    TEST_ASSERT(token != RIG_CONF_END);
    TEST_ASSERT(configured->caps->ptt_type == RIG_PTT_RIG);

    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        TEST_CASE(values[i]);
        TEST_ASSERT(rig_set_conf(configured, token, values[i]) == RIG_OK);
        check_ptt_config(configured, values[i]);
        check_ptt_config(existing, "RIG");
        TEST_CHECK(configured->caps->ptt_type == RIG_PTT_RIG);
        TEST_CHECK(rig_get_caps_int(RIG_MODEL_DUMMY, RIG_CAPS_PTT_TYPE)
                   == RIG_PTT_RIG);

        RIG *subsequent = rig_init(RIG_MODEL_DUMMY);
        TEST_ASSERT(subsequent != NULL);
        check_ptt_config(subsequent, "RIG");
        TEST_CHECK(rig_cleanup(subsequent) == RIG_OK);
    }

    TEST_CHECK(rig_set_conf(configured, token, "invalid") == -RIG_EINVAL);
    check_ptt_config(configured, "RIG");
    TEST_CHECK(rig_cleanup(configured) == RIG_OK);
    TEST_CHECK(rig_cleanup(existing) == RIG_OK);
}

static void test_ptt_none_then_cat(void)
{
    RIG *configured = rig_init(RIG_MODEL_DUMMY);
    RIG *existing = rig_init(RIG_MODEL_DUMMY);
    ptt_t ptt;

    TEST_ASSERT(configured != NULL && existing != NULL);
    hamlib_token_t token = rig_token_lookup(configured, "ptt_type");
    TEST_ASSERT(token != RIG_CONF_END);
    TEST_ASSERT(rig_set_conf(configured, token, "None") == RIG_OK);
    TEST_ASSERT(rig_set_cache_timeout_ms(configured, HAMLIB_CACHE_ALL, 0)
                == RIG_OK);
    TEST_ASSERT(rig_open(configured) == RIG_OK);
    TEST_CHECK(rig_get_ptt(configured, RIG_VFO_CURR, &ptt) == -RIG_ENAVAIL);

    RIG *subsequent = rig_init(RIG_MODEL_DUMMY);
    TEST_ASSERT(subsequent != NULL);
    RIG *cat_rigs[] = { existing, subsequent };

    for (size_t i = 0; i < sizeof(cat_rigs) / sizeof(cat_rigs[0]); ++i)
    {
        TEST_ASSERT(rig_set_cache_timeout_ms(cat_rigs[i], HAMLIB_CACHE_ALL, 0)
                    == RIG_OK);
        TEST_ASSERT(rig_open(cat_rigs[i]) == RIG_OK);
        TEST_CHECK(rig_get_ptt(cat_rigs[i], RIG_VFO_CURR, &ptt) == RIG_OK);
        TEST_CHECK(rig_close(cat_rigs[i]) == RIG_OK);
        TEST_CHECK(rig_cleanup(cat_rigs[i]) == RIG_OK);
    }

    TEST_CHECK(rig_close(configured) == RIG_OK);
    TEST_ASSERT(rig_set_conf(configured, token, "RIG") == RIG_OK);
    TEST_ASSERT(rig_open(configured) == RIG_OK);
    TEST_CHECK(rig_get_ptt(configured, RIG_VFO_CURR, &ptt) == RIG_OK);
    TEST_CHECK(rig_close(configured) == RIG_OK);
    TEST_CHECK(rig_cleanup(configured) == RIG_OK);
}

struct config_log_capture
{
    char text[4096];
    int truncated;
};

static const char *config_backend_value;
static hamlib_token_t config_backend_token;
static int config_backend_result;
static int config_backend_calls;

static int capture_config_log(enum rig_debug_level_e level, rig_ptr_t arg,
                              const char *format, va_list args)
{
    struct config_log_capture *capture = arg;
    size_t used = strlen(capture->text);
    int length;

    (void)level;
    length = vsnprintf(capture->text + used, sizeof(capture->text) - used,
                       format, args);

    if (length < 0 || (size_t)length >= sizeof(capture->text) - used)
    {
        capture->truncated = 1;
    }

    return length;
}

static int record_config_value(RIG *rig, hamlib_token_t token, const char *value)
{
    (void)rig;
    config_backend_value = value;
    config_backend_token = token;
    ++config_backend_calls;
    return config_backend_result;
}

static void test_configuration_values_stay_out_of_debug_logs(void)
{
    const struct confparams params[] =
    {
        { .token = 1, .name = "net_password", .type = RIG_CONF_STRING },
        { .token = 2, .name = "access_key", .type = RIG_CONF_STRING },
        { .token = RIG_CONF_END }
    };
    const struct rig_caps caps =
    {
        .cfgparams = params,
        .set_conf = record_config_value
    };
    RIG rig = { .caps = &caps };
    const char *values[] = { "synthetic-accepted-secret", "synthetic-rejected-secret" };
    const int results[] = { RIG_OK, -RIG_EINVAL };
    enum rig_debug_level_e previous_level;

    rig_get_debug(&previous_level);
    rig_set_debug(RIG_DEBUG_TRACE);

    for (size_t parameter = 0; parameter < 2; ++parameter)
    {
        for (size_t result = 0; result < 2; ++result)
        {
            struct config_log_capture capture = { { 0 }, 0 };
            config_backend_value = NULL;
            config_backend_token = RIG_CONF_END;
            config_backend_calls = 0;
            config_backend_result = results[result];
            rig_set_debug_callback(capture_config_log, &capture);

            int actual = rig_set_conf(&rig, params[parameter].token, values[result]);

            rig_set_debug_callback(NULL, NULL);
            TEST_CASE(params[parameter].name);
            TEST_CHECK(actual == results[result]);
            TEST_CHECK(config_backend_calls == 1);
            TEST_CHECK(config_backend_token == params[parameter].token);
            TEST_CHECK(config_backend_value == values[result]);
            TEST_CHECK(!capture.truncated);
            TEST_CHECK(strstr(capture.text, params[parameter].name) != NULL);
            TEST_CHECK(strstr(capture.text, values[result]) == NULL);
        }
    }

    rig_set_debug(previous_level);
}

TEST_LIST =
{
    { "ptt_overrides_are_instance_local", test_ptt_overrides_are_instance_local },
    { "ptt_none_then_cat", test_ptt_none_then_cat },
    { "configuration_values_stay_out_of_debug_logs", test_configuration_values_stay_out_of_debug_logs },
    { NULL, NULL }
};
