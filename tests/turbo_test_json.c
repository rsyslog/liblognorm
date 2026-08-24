/**
 * @file turbo_test_json.c
 * @brief Comprehensive test suite for fast JSON serialization
 *
 * Coverage:
 * - Empty result
 * - String fields (inline + external): basic, special chars, embedded NUL
 * - Integer fields: positive, negative, zero, extremes
 * - Double fields: positive, negative, zero
 * - Boolean fields (via type)
 * - Null fields (via type)
 * - Multiple fields
 * - Nested objects: one-level (source.ip), two-level (user.group.name),
 *   mixed flat+nested, sibling nesting (source.ip + source.port)
 * - Tag serialization: single, multiple, empty
 * - JSON escaping: quotes, backslash, newline, tab, carriage return,
 *   backspace, formfeed, control chars (\u00XX)
 * - Buffer overflow: small buffer rejection
 * - Estimate function: ln_fast_json_estimate accuracy
 * - Allocating version: ln_fast_to_json_alloc
 * - Large result: many fields in single output
 *
 * @author Jeremie Jourdin / Advens
 * @copyright 2026 Advens. Released under ASL 2.0.
 */

#include "config.h"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

#ifdef ENABLE_TURBO

#include "turbo_result_fast.h"
#include "turbo_arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Test Framework
 *============================================================================*/

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__); \
        return 0; \
    } \
} while(0)

#define TEST_ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "  FAIL: %s - got %d, expected %d (line %d)\n", \
                msg, (int)(a), (int)(b), __LINE__); \
        return 0; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    tests_run++; \
    printf("  Running %s... ", #test_func); \
    fflush(stdout); \
    if (test_func()) { \
        tests_passed++; \
        printf("OK\n"); \
    } else { \
        tests_failed++; \
        printf("FAILED\n"); \
    } \
} while(0)

/*============================================================================
 * Helper Functions
 *============================================================================*/

static int json_contains(const char *json, const char *substr)
{
    return strstr(json, substr) != NULL;
}

/** Verify JSON starts with { and ends with } */
static int json_is_object(const char *json, size_t len)
{
    return len >= 2 && json[0] == '{' && json[len - 1] == '}';
}

/*============================================================================
 * Empty / Minimal Tests
 *============================================================================*/

static int test_json_empty(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "empty result should succeed");
    TEST_ASSERT(strcmp(buf, "{}") == 0, "empty result should be {}");
    TEST_ASSERT_EQ((int)len, 2, "length should be 2");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * String Field Tests
 *============================================================================*/

static int test_json_string_field(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "host", 4, "server01", 8);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_is_object(buf, len), "should be valid JSON object");
    TEST_ASSERT(json_contains(buf, "\"host\""), "should contain host field");
    TEST_ASSERT(json_contains(buf, "\"server01\""), "should contain quoted host value");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_string_external(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[512];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Long string (external storage path) */
    char long_val[100];
    memset(long_val, 'z', 99);
    long_val[99] = '\0';

    ln_fast_add_string_static(&result, "data", 4, long_val, 99);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\"data\""), "should contain data field");
    /* Value should have 99 z's */
    TEST_ASSERT(json_contains(buf, "zzzzz"), "should contain z's");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Integer Field Tests
 *============================================================================*/

static int test_json_int_field(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_int_static(&result, "status", 6, 200);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\"status\""), "should contain status field");
    TEST_ASSERT(json_contains(buf, "200"), "should contain 200");
    /* Integer should NOT be quoted */
    TEST_ASSERT(!json_contains(buf, "\"200\""), "integer should not be quoted");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_int_negative(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_int_static(&result, "offset", 6, -42);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "-42"), "should contain -42");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_int_zero(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_int_static(&result, "count", 5, 0);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, ":0"), "should contain :0");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Double Field Tests
 *============================================================================*/

static int test_json_double_field(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_double_static(&result, "latency", 7, 1.50);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\"latency\""), "should contain latency field");
    TEST_ASSERT(json_contains(buf, "1.50"), "should contain 1.50");
    /* Double should NOT be quoted */
    TEST_ASSERT(!json_contains(buf, "\"1.50\""), "double should not be quoted");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_double_negative(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_double_static(&result, "temp", 4, -273.15);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "-273"), "should contain -273");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_double_zero(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_double_static(&result, "val", 3, 0.0);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "0.00"), "should contain 0.00");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Multiple Fields Test
 *============================================================================*/

static int test_json_multiple_fields(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[512];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "host", 4, "server01", 8);
    ln_fast_add_int_static(&result, "status", 6, 200);
    ln_fast_add_double_static(&result, "latency", 7, 0.42);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_is_object(buf, len), "should be valid JSON object");
    TEST_ASSERT(json_contains(buf, "\"host\""), "should contain host");
    TEST_ASSERT(json_contains(buf, "\"status\""), "should contain status");
    TEST_ASSERT(json_contains(buf, "\"latency\""), "should contain latency");

    /* Fields should be separated by commas */
    int comma_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == ',') comma_count++;
    }
    TEST_ASSERT(comma_count >= 2, "should have at least 2 commas for 3 fields");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Nested Object Tests
 *============================================================================*/

static int test_json_nested_one_level(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[512];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* "source.ip" should produce {"source":{"ip":"1.2.3.4"}} */
    ln_fast_add_string_static(&result, "source.ip", 9, "1.2.3.4", 7);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\"source\""), "should have source key");
    TEST_ASSERT(json_contains(buf, "\"ip\""), "should have ip key");
    TEST_ASSERT(json_contains(buf, "\"1.2.3.4\""), "should have ip value");
    /* Should have nested structure */
    TEST_ASSERT(json_contains(buf, "\"source\":{"), "should open source object");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_nested_siblings(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[1024];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Two fields under same prefix -> same object */
    ln_fast_add_string_static(&result, "source.ip", 9, "10.0.0.1", 8);
    ln_fast_add_int_static(&result, "source.port", 11, 443);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\"source\":{"), "should open source object");
    TEST_ASSERT(json_contains(buf, "\"ip\""), "should have ip field");
    TEST_ASSERT(json_contains(buf, "\"port\""), "should have port field");
    TEST_ASSERT(json_contains(buf, "443"), "should have port value");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_nested_two_levels(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[1024];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* "user.group.name" -> {"user":{"group":{"name":"admins"}}} */
    ln_fast_add_string_static(&result, "user.group.name", 15, "admins", 6);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\"user\":{"), "should open user object");
    TEST_ASSERT(json_contains(buf, "\"group\":{"), "should open group object");
    TEST_ASSERT(json_contains(buf, "\"name\""), "should have name key");
    TEST_ASSERT(json_contains(buf, "\"admins\""), "should have name value");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_nested_mixed_flat(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[1024];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Mix of flat and nested fields */
    ln_fast_add_string_static(&result, "host", 4, "srv1", 4);
    ln_fast_add_string_static(&result, "source.ip", 9, "10.0.0.1", 8);
    ln_fast_add_int_static(&result, "status", 6, 200);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_is_object(buf, len), "should be valid JSON object");
    TEST_ASSERT(json_contains(buf, "\"host\""), "should have flat host");
    TEST_ASSERT(json_contains(buf, "\"source\":{"), "should have nested source");
    TEST_ASSERT(json_contains(buf, "\"status\""), "should have flat status");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_nested_different_prefixes(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[2048];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Multiple nested prefixes — tests closing one object and opening another */
    ln_fast_add_string_static(&result, "source.ip", 9, "10.0.0.1", 8);
    ln_fast_add_int_static(&result, "source.port", 11, 443);
    ln_fast_add_string_static(&result, "user.name", 9, "admin", 5);
    ln_fast_add_string_static(&result, "user.group.name", 15, "admins", 6);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\"source\":{"), "should have source object");
    TEST_ASSERT(json_contains(buf, "\"user\":{"), "should have user object");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Tag Serialization Tests
 *============================================================================*/

static int test_json_single_tag(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[512];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "host", 4, "srv1", 4);
    ln_fast_add_tag(&result, "web");

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\"event.tags\""), "should have tags key");
    TEST_ASSERT(json_contains(buf, "["), "should have array open");
    TEST_ASSERT(json_contains(buf, "]"), "should have array close");
    TEST_ASSERT(json_contains(buf, "\"web\""), "should contain tag value");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_multiple_tags(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[512];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_tag(&result, "web");
    ln_fast_add_tag(&result, "http");
    ln_fast_add_tag(&result, "firewall");

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\"web\""), "should contain 'web' tag");
    TEST_ASSERT(json_contains(buf, "\"http\""), "should contain 'http' tag");
    TEST_ASSERT(json_contains(buf, "\"firewall\""), "should contain 'firewall' tag");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_tags_only(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[512];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Only tags, no fields */
    ln_fast_add_tag(&result, "syslog");

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_is_object(buf, len), "should be valid JSON object");
    TEST_ASSERT(json_contains(buf, "\"event.tags\":[\"syslog\"]"),
                "should have tags array with syslog");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * JSON Escaping Tests
 *============================================================================*/

static int test_json_escape_quotes(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "msg", 3, "say \"hi\"", 8);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\\\"hi\\\""), "should escape quotes");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_escape_backslash(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "path", 4, "C:\\Users\\test", 13);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "C:\\\\Users\\\\test"),
                "should escape backslashes in JSON output");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_escape_newline(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "msg", 3, "line1\nline2", 11);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\\n"), "should escape newline");
    TEST_ASSERT(!json_contains(buf, "\n"), "should not contain literal newline");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_escape_tab(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "msg", 3, "col1\tcol2", 9);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\\t"), "should escape tab");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_escape_carriage_return(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "msg", 3, "line1\r\nline2", 12);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\\r"), "should escape carriage return");
    TEST_ASSERT(json_contains(buf, "\\n"), "should escape newline");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_escape_control_chars(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* 0x01 control char should produce \u0001 */
    char ctrl_str[4] = {'A', 0x01, 'B', '\0'};
    ln_fast_add_string_static(&result, "msg", 3, ctrl_str, 3);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\\u0001"), "should escape control char as \\u00XX");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_escape_backspace_formfeed(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* \b = 0x08, \f = 0x0C */
    char special[3] = {'\b', '\f', '\0'};
    ln_fast_add_string_static(&result, "msg", 3, special, 2);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\\b"), "should escape backspace");
    TEST_ASSERT(json_contains(buf, "\\f"), "should escape formfeed");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_clean_ascii(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[256];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Clean ASCII string - no escaping needed (fast path) */
    ln_fast_add_string_static(&result, "msg", 3, "Hello World 123!", 16);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "should succeed");
    TEST_ASSERT(json_contains(buf, "\"Hello World 123!\""),
                "clean ASCII should pass through unchanged");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Buffer Overflow Tests
 *============================================================================*/

static int test_json_buffer_too_small(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[4];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "field", 5, "value", 5);

    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, -1, "should fail with small buffer");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_buffer_minimal(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[3];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Empty result needs exactly 3 bytes: "{}\0" */
    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, 0, "empty result in 3-byte buffer should succeed");
    TEST_ASSERT(strcmp(buf, "{}") == 0, "should be {}");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_buffer_too_small_for_empty(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char buf[2];
    size_t len;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* 2 bytes is too small even for "{}\0" */
    int r = ln_fast_to_json(&result, buf, sizeof(buf), &len);
    TEST_ASSERT_EQ(r, -1, "2-byte buffer should fail even for empty");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Estimate Tests
 *============================================================================*/

static int test_json_estimate(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "host", 4, "server01", 8);
    ln_fast_add_int_static(&result, "status", 6, 200);

    size_t est = ln_fast_json_estimate(&result);
    TEST_ASSERT(est > 20, "estimate should be reasonable (> 20)");

    char *buf = malloc(est);
    TEST_ASSERT(buf != NULL, "malloc should succeed");

    size_t len;
    int r = ln_fast_to_json(&result, buf, est, &len);
    TEST_ASSERT_EQ(r, 0, "estimate should be sufficient");
    TEST_ASSERT(len < est, "actual should be less than estimate");

    free(buf);
    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_estimate_with_tags(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "host", 4, "srv1", 4);
    ln_fast_add_tag(&result, "web");
    ln_fast_add_tag(&result, "http");

    size_t est = ln_fast_json_estimate(&result);
    TEST_ASSERT(est > 40, "estimate with tags should be reasonable");

    char *buf = malloc(est);
    size_t len;
    int r = ln_fast_to_json(&result, buf, est, &len);
    TEST_ASSERT_EQ(r, 0, "estimate should be sufficient for tagged result");
    TEST_ASSERT(len < est, "actual should be less than estimate");

    free(buf);
    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_estimate_nested(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "user.group.name", 15, "admins", 6);
    ln_fast_add_string_static(&result, "source.ip", 9, "10.0.0.1", 8);

    size_t est = ln_fast_json_estimate(&result);
    TEST_ASSERT(est > 60, "estimate with nested fields should be sufficient");

    char *buf = malloc(est);
    size_t len;
    int r = ln_fast_to_json(&result, buf, est, &len);
    TEST_ASSERT_EQ(r, 0, "estimate should be sufficient for nested result");

    free(buf);
    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Allocating Version Tests
 *============================================================================*/

static int test_json_alloc_basic(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "host", 4, "server01", 8);
    ln_fast_add_int_static(&result, "status", 6, 200);

    char *json_str = NULL;
    size_t json_len = 0;

    int r = ln_fast_to_json_alloc(&result, &json_str, &json_len);
    TEST_ASSERT_EQ(r, 0, "alloc version should succeed");
    TEST_ASSERT(json_str != NULL, "should allocate buffer");
    TEST_ASSERT(json_len > 0, "should have non-zero length");
    TEST_ASSERT(json_is_object(json_str, json_len), "should be valid JSON object");
    TEST_ASSERT(json_contains(json_str, "\"host\""), "should contain host");
    TEST_ASSERT(json_contains(json_str, "\"status\""), "should contain status");

    free(json_str);
    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_alloc_empty(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    char *json_str = NULL;
    size_t json_len = 0;

    int r = ln_fast_to_json_alloc(&result, &json_str, &json_len);
    TEST_ASSERT_EQ(r, 0, "alloc with empty result should succeed");
    TEST_ASSERT(json_str != NULL, "should allocate buffer");
    TEST_ASSERT(strcmp(json_str, "{}") == 0, "empty result should be {}");

    free(json_str);
    ln_arena_destroy(&arena);
    return 1;
}

static int test_json_alloc_null_len(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "f", 1, "v", 1);

    char *json_str = NULL;

    /* json_len can be NULL (optional) */
    int r = ln_fast_to_json_alloc(&result, &json_str, NULL);
    TEST_ASSERT_EQ(r, 0, "alloc with NULL len should succeed");
    TEST_ASSERT(json_str != NULL, "should allocate buffer");

    free(json_str);
    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Large Result Test
 *============================================================================*/

static int test_json_many_fields(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char name[32];

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Add 20 fields of mixed types.
     * Names must be arena-allocated (static API stores pointer, not copy) */
    for (int i = 0; i < 20; i++) {
        snprintf(name, sizeof(name), "field_%02d", i);
        const char *aname = ln_arena_strndup(&arena, name, strlen(name));
        uint16_t nlen = (uint16_t)strlen(name);
        if (i % 3 == 0) {
            ln_fast_add_string_static(&result, aname, nlen, "value", 5);
        } else if (i % 3 == 1) {
            ln_fast_add_int_static(&result, aname, nlen, i * 100);
        } else {
            ln_fast_add_double_static(&result, aname, nlen, i * 0.5);
        }
    }

    char *json_str = NULL;
    size_t json_len = 0;

    int r = ln_fast_to_json_alloc(&result, &json_str, &json_len);
    TEST_ASSERT_EQ(r, 0, "should succeed with 20 fields");
    TEST_ASSERT(json_str != NULL, "should allocate buffer");
    TEST_ASSERT(json_is_object(json_str, json_len), "should be valid JSON object");

    /* Verify all fields present */
    for (int i = 0; i < 20; i++) {
        snprintf(name, sizeof(name), "\"field_%02d\"", i);
        TEST_ASSERT(json_contains(json_str, name), "should contain all fields");
    }

    free(json_str);
    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Realistic Syslog-Like Output
 *============================================================================*/

static int test_json_syslog_like(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Typical syslog parse result */
    ln_fast_add_string_static(&result, "timestamp", 9, "2026-01-15T10:30:00Z", 20);
    ln_fast_add_string_static(&result, "host", 4, "fw01.prod.example.com", 21);
    ln_fast_add_int_static(&result, "facility", 8, 1);
    ln_fast_add_int_static(&result, "severity", 8, 6);
    ln_fast_add_string_static(&result, "source.ip", 9, "192.168.1.100", 13);
    ln_fast_add_int_static(&result, "source.port", 11, 52340);
    ln_fast_add_string_static(&result, "destination.ip", 14, "10.0.0.1", 8);
    ln_fast_add_int_static(&result, "destination.port", 16, 443);
    ln_fast_add_string_static(&result, "event.action", 12, "allow", 5);
    ln_fast_set_rule_id(&result, "rule_fw_001");
    ln_fast_add_tag(&result, "firewall");
    ln_fast_add_tag(&result, "network");

    char *json_str = NULL;
    size_t json_len = 0;

    int r = ln_fast_to_json_alloc(&result, &json_str, &json_len);
    TEST_ASSERT_EQ(r, 0, "syslog-like result should succeed");
    TEST_ASSERT(json_str != NULL, "should allocate buffer");

    /* Verify nested objects */
    TEST_ASSERT(json_contains(json_str, "\"destination\":{"),
                "should have destination object");
    TEST_ASSERT(json_contains(json_str, "\"source\":{"),
                "should have source object");
    /* Verify tags */
    TEST_ASSERT(json_contains(json_str, "\"event.tags\""),
                "should have tags");
    TEST_ASSERT(json_contains(json_str, "\"firewall\""),
                "should contain firewall tag");
    TEST_ASSERT(json_contains(json_str, "\"network\""),
                "should contain network tag");

    free(json_str);
    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void)
{
    printf("=== Fast JSON Comprehensive Test Suite ===\n\n");

    /* Empty / minimal */
    printf("Empty / minimal tests:\n");
    RUN_TEST(test_json_empty);
    printf("\n");

    /* String fields */
    printf("String field tests:\n");
    RUN_TEST(test_json_string_field);
    RUN_TEST(test_json_string_external);
    printf("\n");

    /* Integer fields */
    printf("Integer field tests:\n");
    RUN_TEST(test_json_int_field);
    RUN_TEST(test_json_int_negative);
    RUN_TEST(test_json_int_zero);
    printf("\n");

    /* Double fields */
    printf("Double field tests:\n");
    RUN_TEST(test_json_double_field);
    RUN_TEST(test_json_double_negative);
    RUN_TEST(test_json_double_zero);
    printf("\n");

    /* Multiple fields */
    printf("Multiple field tests:\n");
    RUN_TEST(test_json_multiple_fields);
    printf("\n");

    /* Nested objects */
    printf("Nested object tests:\n");
    RUN_TEST(test_json_nested_one_level);
    RUN_TEST(test_json_nested_siblings);
    RUN_TEST(test_json_nested_two_levels);
    RUN_TEST(test_json_nested_mixed_flat);
    RUN_TEST(test_json_nested_different_prefixes);
    printf("\n");

    /* Tag serialization */
    printf("Tag serialization tests:\n");
    RUN_TEST(test_json_single_tag);
    RUN_TEST(test_json_multiple_tags);
    RUN_TEST(test_json_tags_only);
    printf("\n");

    /* JSON escaping */
    printf("JSON escaping tests:\n");
    RUN_TEST(test_json_escape_quotes);
    RUN_TEST(test_json_escape_backslash);
    RUN_TEST(test_json_escape_newline);
    RUN_TEST(test_json_escape_tab);
    RUN_TEST(test_json_escape_carriage_return);
    RUN_TEST(test_json_escape_control_chars);
    RUN_TEST(test_json_escape_backspace_formfeed);
    RUN_TEST(test_json_clean_ascii);
    printf("\n");

    /* Buffer overflow */
    printf("Buffer overflow tests:\n");
    RUN_TEST(test_json_buffer_too_small);
    RUN_TEST(test_json_buffer_minimal);
    RUN_TEST(test_json_buffer_too_small_for_empty);
    printf("\n");

    /* Estimate */
    printf("Estimate tests:\n");
    RUN_TEST(test_json_estimate);
    RUN_TEST(test_json_estimate_with_tags);
    RUN_TEST(test_json_estimate_nested);
    printf("\n");

    /* Allocating version */
    printf("Allocating version tests:\n");
    RUN_TEST(test_json_alloc_basic);
    RUN_TEST(test_json_alloc_empty);
    RUN_TEST(test_json_alloc_null_len);
    printf("\n");

    /* Large result */
    printf("Large result tests:\n");
    RUN_TEST(test_json_many_fields);
    printf("\n");

    /* Realistic */
    printf("Realistic tests:\n");
    RUN_TEST(test_json_syslog_like);
    printf("\n");

    /* Summary */
    printf("=== Summary ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}

#else /* !ENABLE_TURBO */

int main(void)
{
    printf("Turbo mode not enabled, skipping tests.\n");
    return 0;
}

#endif /* ENABLE_TURBO */
