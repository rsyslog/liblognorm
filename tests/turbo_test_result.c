/**
 * @file turbo_test_result.c
 * @brief Comprehensive test suite for ln_fast_result_t and snapshot
 *
 * Coverage:
 * - Lifecycle: init, clear, re-init
 * - String fields: inline (< 48B), external (>= 48B), boundary (47/48)
 * - Integer fields: positive, negative, zero, INT64_MIN, INT64_MAX
 * - Double fields: positive, negative, zero, fractional
 * - Tag management: add, dedup (same pointer), has_tag, overflow
 * - Capacity: max fields, overflow rejection
 * - Metadata: rule_id, original message, flags
 * - Nested field detection: dotted names, no dots
 * - Hash functions: ln_fast_hash, ln_fast_hash_n, consistency
 * - Snapshot: create, get, free, pointer rebasing, NULL safety
 * - Field flags: STATIC_NAME, STATIC_VAL, NESTED
 *
 * @author Jeremie Jourdin / Advens
 * @copyright 2026 Advens. Released under ASL 2.0.
 */

#include "config.h"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

#ifdef ENABLE_TURBO

#include "turbo_result_fast.h"
#include "turbo_arena.h"
#include "turbo_snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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
        fprintf(stderr, "  FAIL: %s - got %lld, expected %lld (line %d)\n", \
                msg, (long long)(a), (long long)(b), __LINE__); \
        return 0; \
    } \
} while(0)

#define TEST_ASSERT_STR_EQ(a, b, msg) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  FAIL: %s - got \"%s\", expected \"%s\" (line %d)\n", \
                msg, (a), (b), __LINE__); \
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
 * Lifecycle Tests
 *============================================================================*/

static int test_init(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    TEST_ASSERT_EQ(result.n_fields, 0, "fields should be 0");
    TEST_ASSERT_EQ(result.n_tags, 0, "tags should be 0");
    TEST_ASSERT_EQ(result.flags, 0, "flags should be 0");
    TEST_ASSERT(result.rule_id == NULL, "rule_id should be NULL");
    TEST_ASSERT(result.original == NULL, "original should be NULL");
    TEST_ASSERT_EQ(result.original_len, 0, "original_len should be 0");
    TEST_ASSERT(result.arena == &arena, "arena pointer should be set");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_clear(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Populate with data */
    ln_fast_add_string_static(&result, "field1", 6, "value1", 6);
    ln_fast_add_tag(&result, "tag1");
    ln_fast_set_rule_id(&result, "rule123");
    ln_fast_set_original(&result, "test msg", 8);

    TEST_ASSERT_EQ(result.n_fields, 1, "should have 1 field");
    TEST_ASSERT_EQ(result.n_tags, 1, "should have 1 tag");
    TEST_ASSERT(result.flags != 0, "flags should be set");

    /* Clear */
    ln_fast_result_clear(&result);

    TEST_ASSERT_EQ(result.n_fields, 0, "fields should be 0 after clear");
    TEST_ASSERT_EQ(result.n_tags, 0, "tags should be 0 after clear");
    TEST_ASSERT_EQ(result.flags, 0, "flags should be 0 after clear");
    TEST_ASSERT(result.rule_id == NULL, "rule_id should be NULL after clear");
    TEST_ASSERT(result.original == NULL, "original should be NULL after clear");
    TEST_ASSERT_EQ(result.original_len, 0, "original_len should be 0 after clear");

    /* Arena pointer should still be valid */
    TEST_ASSERT(result.arena == &arena, "arena should survive clear");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_clear_and_reuse(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* First message */
    ln_fast_add_string_static(&result, "host", 4, "srv1", 4);
    ln_fast_set_rule_id(&result, "rule_a");
    TEST_ASSERT_EQ(result.n_fields, 1, "first msg: 1 field");

    /* Clear for next message */
    ln_fast_result_clear(&result);

    /* Second message */
    ln_fast_add_int_static(&result, "status", 6, 200);
    ln_fast_add_int_static(&result, "bytes", 5, 4096);
    ln_fast_set_rule_id(&result, "rule_b");
    TEST_ASSERT_EQ(result.n_fields, 2, "second msg: 2 fields");
    TEST_ASSERT_STR_EQ(result.rule_id, "rule_b", "rule_id should be rule_b");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * String Field Tests
 *============================================================================*/

static int test_add_string_inline(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Short string (8 < 48) should be stored inline */
    int r = ln_fast_add_string_static(&result, "host", 4, "server01", 8);
    TEST_ASSERT_EQ(r, 0, "add_string should succeed");
    TEST_ASSERT_EQ(result.n_fields, 1, "should have 1 field");

    const ln_fast_field_t *f = &result.fields[0];
    TEST_ASSERT_EQ(f->type, LN_FTYPE_STRING_INLINE, "short string should be inline");
    TEST_ASSERT(memcmp(f->v.inl, "server01", 8) == 0, "value should match");
    TEST_ASSERT_EQ(f->v.inl[8], '\0', "should be null-terminated");
    TEST_ASSERT_STR_EQ(f->name, "host", "name should be 'host'");
    TEST_ASSERT_EQ(f->name_len, 4, "name_len should be 4");
    TEST_ASSERT(f->flags & LN_FFIELD_STATIC_NAME, "should have STATIC_NAME flag");
    TEST_ASSERT(f->flags & LN_FFIELD_STATIC_VAL, "should have STATIC_VAL flag");
    TEST_ASSERT(!(f->flags & LN_FFIELD_NESTED), "should not have NESTED flag");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_add_string_external(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* String >= 48 bytes should use external storage */
    char long_value[100];
    memset(long_value, 'x', sizeof(long_value));

    int r = ln_fast_add_string_static(&result, "data", 4, long_value, sizeof(long_value));
    TEST_ASSERT_EQ(r, 0, "add_string should succeed");

    const ln_fast_field_t *f = &result.fields[0];
    TEST_ASSERT_EQ(f->type, LN_FTYPE_STRING, "long string should be external");
    TEST_ASSERT_EQ(f->v.str.len, sizeof(long_value), "length should match");
    TEST_ASSERT(f->v.str.ptr == long_value, "pointer should reference original");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_add_string_boundary_47(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* 47 bytes: just under LN_FAST_INLINE_SIZE (48), should be inline */
    char val47[47];
    memset(val47, 'A', 47);

    int r = ln_fast_add_string_static(&result, "f47", 3, val47, 47);
    TEST_ASSERT_EQ(r, 0, "add should succeed");

    const ln_fast_field_t *f = &result.fields[0];
    TEST_ASSERT_EQ(f->type, LN_FTYPE_STRING_INLINE, "47-byte string should be inline");
    TEST_ASSERT(memcmp(f->v.inl, val47, 47) == 0, "value should match");
    TEST_ASSERT_EQ(f->v.inl[47], '\0', "should be null-terminated");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_add_string_boundary_48(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* 48 bytes: exactly LN_FAST_INLINE_SIZE, should be external */
    char val48[48];
    memset(val48, 'B', 48);

    int r = ln_fast_add_string_static(&result, "f48", 3, val48, 48);
    TEST_ASSERT_EQ(r, 0, "add should succeed");

    const ln_fast_field_t *f = &result.fields[0];
    TEST_ASSERT_EQ(f->type, LN_FTYPE_STRING, "48-byte string should be external");
    TEST_ASSERT_EQ(f->v.str.len, 48, "length should be 48");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_add_string_empty(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Empty string should be inline */
    int r = ln_fast_add_string_static(&result, "empty", 5, "", 0);
    TEST_ASSERT_EQ(r, 0, "add empty string should succeed");

    const ln_fast_field_t *f = &result.fields[0];
    TEST_ASSERT_EQ(f->type, LN_FTYPE_STRING_INLINE, "empty string should be inline");
    TEST_ASSERT_EQ(f->v.inl[0], '\0', "should be null-terminated");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Integer Field Tests
 *============================================================================*/

static int test_add_int_positive(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    int r = ln_fast_add_int_static(&result, "status", 6, 200);
    TEST_ASSERT_EQ(r, 0, "add_int should succeed");
    TEST_ASSERT_EQ(result.n_fields, 1, "should have 1 field");

    const ln_fast_field_t *f = &result.fields[0];
    TEST_ASSERT_EQ(f->type, LN_FTYPE_INT, "type should be int");
    TEST_ASSERT_EQ(f->v.i, 200, "value should be 200");
    TEST_ASSERT(f->flags & LN_FFIELD_STATIC_NAME, "should have STATIC_NAME flag");
    TEST_ASSERT(!(f->flags & LN_FFIELD_STATIC_VAL), "int should not have STATIC_VAL");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_add_int_negative(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    int r = ln_fast_add_int_static(&result, "offset", 6, -42);
    TEST_ASSERT_EQ(r, 0, "add_int should succeed");
    TEST_ASSERT_EQ(result.fields[0].v.i, -42, "value should be -42");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_add_int_zero(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    int r = ln_fast_add_int_static(&result, "count", 5, 0);
    TEST_ASSERT_EQ(r, 0, "add_int should succeed");
    TEST_ASSERT_EQ(result.fields[0].v.i, 0, "value should be 0");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_add_int_extremes(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    int r = ln_fast_add_int_static(&result, "max", 3, INT64_MAX);
    TEST_ASSERT_EQ(r, 0, "add INT64_MAX should succeed");
    TEST_ASSERT_EQ(result.fields[0].v.i, INT64_MAX, "should store INT64_MAX");

    r = ln_fast_add_int_static(&result, "min", 3, INT64_MIN);
    TEST_ASSERT_EQ(r, 0, "add INT64_MIN should succeed");
    TEST_ASSERT_EQ(result.fields[1].v.i, INT64_MIN, "should store INT64_MIN");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Double Field Tests
 *============================================================================*/

static int test_add_double_positive(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    int r = ln_fast_add_double_static(&result, "latency", 7, 1.5);
    TEST_ASSERT_EQ(r, 0, "add_double should succeed");

    const ln_fast_field_t *f = &result.fields[0];
    TEST_ASSERT_EQ(f->type, LN_FTYPE_DOUBLE, "type should be double");
    TEST_ASSERT(f->v.d > 1.4 && f->v.d < 1.6, "value should be ~1.5");
    TEST_ASSERT(f->flags & LN_FFIELD_STATIC_NAME, "should have STATIC_NAME flag");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_add_double_negative(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    int r = ln_fast_add_double_static(&result, "temp", 4, -273.15);
    TEST_ASSERT_EQ(r, 0, "add_double should succeed");
    TEST_ASSERT(result.fields[0].v.d < -273.0, "value should be < -273.0");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_add_double_zero(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    int r = ln_fast_add_double_static(&result, "rate", 4, 0.0);
    TEST_ASSERT_EQ(r, 0, "add_double should succeed");
    TEST_ASSERT(result.fields[0].v.d == 0.0, "value should be 0.0");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_add_double_precise(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Test with a value that has many decimal places */
    int r = ln_fast_add_double_static(&result, "pi", 2, 3.14159265358979);
    TEST_ASSERT_EQ(r, 0, "add_double should succeed");
    TEST_ASSERT(result.fields[0].v.d > 3.141 && result.fields[0].v.d < 3.142,
                "value should be ~3.14159");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Tag Tests
 *============================================================================*/

static int test_add_tag(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    int r = ln_fast_add_tag(&result, "web");
    TEST_ASSERT_EQ(r, 0, "add_tag should succeed");
    TEST_ASSERT_EQ(result.n_tags, 1, "should have 1 tag");
    TEST_ASSERT_STR_EQ(result.tags[0].tag, "web", "tag should be 'web'");
    TEST_ASSERT(result.tags[0].hash != 0, "hash should be computed");

    r = ln_fast_add_tag(&result, "http");
    TEST_ASSERT_EQ(r, 0, "add second tag should succeed");
    TEST_ASSERT_EQ(result.n_tags, 2, "should have 2 tags");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_add_tag_duplicate_same_pointer(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Same string literal (same pointer) should be deduped */
    static const char *tag = "syslog";
    ln_fast_add_tag(&result, tag);
    ln_fast_add_tag(&result, tag);

    /* Pointer equality check means same pointer is deduped */
    TEST_ASSERT_EQ(result.n_tags, 1, "same-pointer duplicate should be deduped");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_has_tag(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_tag(&result, "web");
    ln_fast_add_tag(&result, "http");
    ln_fast_add_tag(&result, "firewall");

    TEST_ASSERT(ln_fast_has_tag(&result, "web") == 1, "should have 'web' tag");
    TEST_ASSERT(ln_fast_has_tag(&result, "http") == 1, "should have 'http' tag");
    TEST_ASSERT(ln_fast_has_tag(&result, "firewall") == 1, "should have 'firewall' tag");
    TEST_ASSERT(ln_fast_has_tag(&result, "ftp") == 0, "should not have 'ftp' tag");
    TEST_ASSERT(ln_fast_has_tag(&result, "") == 0, "should not have empty tag");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_has_tag_null_safety(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    TEST_ASSERT(ln_fast_has_tag(NULL, "web") == 0, "NULL result should return 0");
    TEST_ASSERT(ln_fast_has_tag(&result, NULL) == 0, "NULL tag should return 0");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_tag_overflow(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char tag_name[32];

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Fill to max */
    for (int i = 0; i < LN_FAST_MAX_TAGS; i++) {
        snprintf(tag_name, sizeof(tag_name), "tag_%d", i);
        int r = ln_fast_add_tag(&result, tag_name);
        TEST_ASSERT_EQ(r, 0, "adding tag should succeed");
    }
    TEST_ASSERT_EQ(result.n_tags, LN_FAST_MAX_TAGS, "should have max tags");

    /* One more should fail */
    int r = ln_fast_add_tag(&result, "overflow_tag");
    TEST_ASSERT_EQ(r, -1, "overflow should fail");
    TEST_ASSERT_EQ(result.n_tags, LN_FAST_MAX_TAGS, "count should stay at max");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_tag_hash_bitmap(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* After adding a tag, the hash bitmap should be marked */
    ln_fast_add_tag(&result, "test_tag");

    uint32_t h = ln_fast_hash("test_tag");
    uint8_t slot = h & (LN_FAST_TAG_HASH_SIZE - 1);
    TEST_ASSERT(result.tag_hash[slot] == 1, "hash bitmap should be set");

    /* Clear should reset the bitmap */
    ln_fast_result_clear(&result);
    TEST_ASSERT(result.tag_hash[slot] == 0, "hash bitmap should be cleared");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Capacity Tests
 *============================================================================*/

static int test_max_fields(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char name[32];

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Fill to max */
    for (int i = 0; i < LN_FAST_MAX_FIELDS; i++) {
        snprintf(name, sizeof(name), "field%d", i);
        int r = ln_fast_add_int_static(&result, name, (uint16_t)strlen(name), i);
        TEST_ASSERT_EQ(r, 0, "adding field should succeed");
    }

    TEST_ASSERT_EQ(result.n_fields, LN_FAST_MAX_FIELDS, "should reach max");

    /* One more should fail */
    int r = ln_fast_add_int_static(&result, "overflow", 8, 999);
    TEST_ASSERT_EQ(r, -1, "overflow should fail");
    TEST_ASSERT_EQ(result.n_fields, LN_FAST_MAX_FIELDS, "count should stay at max");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_field_overflow_all_types(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char name[32];

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Fill to max with mixed types */
    for (int i = 0; i < LN_FAST_MAX_FIELDS; i++) {
        snprintf(name, sizeof(name), "f%d", i);
        ln_fast_add_string_static(&result, name, (uint16_t)strlen(name), "v", 1);
    }

    /* All add functions should reject when full */
    int r1 = ln_fast_add_string_static(&result, "s", 1, "v", 1);
    int r2 = ln_fast_add_int_static(&result, "i", 1, 42);
    int r3 = ln_fast_add_double_static(&result, "d", 1, 1.0);

    TEST_ASSERT_EQ(r1, -1, "string overflow should fail");
    TEST_ASSERT_EQ(r2, -1, "int overflow should fail");
    TEST_ASSERT_EQ(r3, -1, "double overflow should fail");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Metadata Tests
 *============================================================================*/

static int test_rule_id(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    TEST_ASSERT(result.rule_id == NULL, "initial rule_id should be NULL");
    TEST_ASSERT(!(result.flags & LN_FRESULT_MATCHED), "initially not matched");

    ln_fast_set_rule_id(&result, "rule_web_001");

    TEST_ASSERT(result.rule_id != NULL, "rule_id should be set");
    TEST_ASSERT_STR_EQ(result.rule_id, "rule_web_001", "rule_id should match");
    TEST_ASSERT(result.flags & LN_FRESULT_MATCHED, "MATCHED flag should be set");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_original_message(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    TEST_ASSERT(!(result.flags & LN_FRESULT_HAS_ORIG), "initially no original");

    const char *msg = "Jan  1 00:00:00 host test[123]: hello world";
    ln_fast_set_original(&result, msg, (uint32_t)strlen(msg));

    TEST_ASSERT(result.original != NULL, "original should be set");
    TEST_ASSERT_EQ(result.original_len, (uint32_t)strlen(msg), "length should match");
    TEST_ASSERT(memcmp(result.original, msg, strlen(msg)) == 0, "content should match");
    TEST_ASSERT(result.flags & LN_FRESULT_HAS_ORIG, "HAS_ORIG flag should be set");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_flags_combination(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_set_rule_id(&result, "rule1");
    ln_fast_set_original(&result, "msg", 3);

    TEST_ASSERT(result.flags & LN_FRESULT_MATCHED, "MATCHED should be set");
    TEST_ASSERT(result.flags & LN_FRESULT_HAS_ORIG, "HAS_ORIG should be set");
    TEST_ASSERT((result.flags & (LN_FRESULT_MATCHED | LN_FRESULT_HAS_ORIG)) ==
                (LN_FRESULT_MATCHED | LN_FRESULT_HAS_ORIG),
                "both flags should coexist");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Nested Field Detection Tests
 *============================================================================*/

static int test_nested_field_detection(void)
{
    /* Test the ln_ffield_detect_nested inline function */
    TEST_ASSERT_EQ(ln_ffield_detect_nested("host", 4), 0,
                   "no dot -> not nested");
    TEST_ASSERT_EQ(ln_ffield_detect_nested("source.ip", 9), LN_FFIELD_NESTED,
                   "one dot -> nested");
    TEST_ASSERT_EQ(ln_ffield_detect_nested("user.group.name", 15), LN_FFIELD_NESTED,
                   "two dots -> nested");
    TEST_ASSERT_EQ(ln_ffield_detect_nested(".", 1), LN_FFIELD_NESTED,
                   "lone dot -> nested");
    TEST_ASSERT_EQ(ln_ffield_detect_nested("", 0), 0,
                   "empty -> not nested");
    return 1;
}

static int test_nested_flag_on_add(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Flat field */
    ln_fast_add_string_static(&result, "host", 4, "srv1", 4);
    TEST_ASSERT(!(result.fields[0].flags & LN_FFIELD_NESTED),
                "flat field should not have NESTED");

    /* Dotted field */
    ln_fast_add_string_static(&result, "source.ip", 9, "1.2.3.4", 7);
    TEST_ASSERT(result.fields[1].flags & LN_FFIELD_NESTED,
                "dotted field should have NESTED");

    /* Deeply nested */
    ln_fast_add_int_static(&result, "user.group.id", 13, 42);
    TEST_ASSERT(result.fields[2].flags & LN_FFIELD_NESTED,
                "deeply dotted field should have NESTED");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Hash Function Tests
 *============================================================================*/

static int test_hash_functions(void)
{
    /* ln_fast_hash (null-terminated) */
    uint32_t h1 = ln_fast_hash("hello");
    uint32_t h2 = ln_fast_hash("hello");
    TEST_ASSERT_EQ(h1, h2, "same string should give same hash");

    uint32_t h3 = ln_fast_hash("world");
    TEST_ASSERT(h1 != h3, "different strings should give different hash (probabilistic)");

    /* ln_fast_hash_n (length-based) */
    uint32_t h4 = ln_fast_hash_n("hello", 5);
    TEST_ASSERT_EQ(h1, h4, "hash and hash_n should agree for same input");

    /* Partial hash */
    uint32_t h5 = ln_fast_hash_n("helloworld", 5);
    TEST_ASSERT_EQ(h4, h5, "hash_n should only hash first n bytes");

    /* Empty string */
    uint32_t h6 = ln_fast_hash("");
    uint32_t h7 = ln_fast_hash_n("anything", 0);
    TEST_ASSERT_EQ(h6, h7, "empty hash should be same as zero-length hash_n");

    /* Known FNV-1a basis value for empty input */
    TEST_ASSERT_EQ(h6, 2166136261u, "empty hash should be FNV offset basis");

    return 1;
}

/*============================================================================
 * Field Size Static Assert (compile-time check, just verify it compiled)
 *============================================================================*/

static int test_field_size(void)
{
    /* This is primarily a compile-time check (_Static_assert in header).
     * Just verify the sizeof at runtime too. */
    TEST_ASSERT_EQ(sizeof(ln_fast_field_t), 64, "field struct should be 64 bytes");
    return 1;
}

/*============================================================================
 * Multiple Field Types in One Result
 *============================================================================*/

static int test_mixed_field_types(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Add all supported types */
    ln_fast_add_string_static(&result, "host", 4, "srv1", 4);
    ln_fast_add_int_static(&result, "status", 6, 200);
    ln_fast_add_double_static(&result, "latency", 7, 0.42);
    ln_fast_set_rule_id(&result, "mixed_rule");
    ln_fast_add_tag(&result, "web");
    ln_fast_set_original(&result, "test msg", 8);

    TEST_ASSERT_EQ(result.n_fields, 3, "should have 3 fields");
    TEST_ASSERT_EQ(result.fields[0].type, LN_FTYPE_STRING_INLINE, "field 0 = string");
    TEST_ASSERT_EQ(result.fields[1].type, LN_FTYPE_INT, "field 1 = int");
    TEST_ASSERT_EQ(result.fields[2].type, LN_FTYPE_DOUBLE, "field 2 = double");
    TEST_ASSERT_EQ(result.n_tags, 1, "should have 1 tag");
    TEST_ASSERT(result.flags & LN_FRESULT_MATCHED, "should be matched");
    TEST_ASSERT(result.flags & LN_FRESULT_HAS_ORIG, "should have original");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * JSON Estimate Robustness (control-char escaping must not undercount)
 *============================================================================*/

/*
 * Regression for the serialization-estimate undercount (FINDING #3):
 * a value/name/tag dominated by control bytes (0x01) escapes to "\uXXXX"
 * (6 output bytes per input byte). The old estimate budgeted only ~2x, so
 * ln_fast_to_json returned -1 and the line was SILENTLY DROPPED. With the
 * 6x worst-case estimate the buffer is always large enough.
 */
static int test_json_estimate_control_chars(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;
    char json[8192];
    size_t out_len = 0;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* 47-byte all-0x01 inline value: true output ~= 47*6 + quotes ~= 284 B,
     * which the old `len*2+2` estimate could not cover. */
    char ctrl_val[47];
    memset(ctrl_val, 0x01, sizeof(ctrl_val));
    ln_fast_add_string_static(&result, "payload", 7, ctrl_val, sizeof(ctrl_val));

    /* A field name full of control chars (also escaped via write_escaped). */
    char ctrl_name[16];
    memset(ctrl_name, 0x01, sizeof(ctrl_name) - 1);
    ctrl_name[sizeof(ctrl_name) - 1] = '\0';
    ln_fast_add_string_static(&result, ctrl_name, sizeof(ctrl_name) - 1, "v", 1);

    /* A tag full of control chars (tags also go through write_escaped). */
    char ctrl_tag[24];
    memset(ctrl_tag, 0x01, sizeof(ctrl_tag) - 1);
    ctrl_tag[sizeof(ctrl_tag) - 1] = '\0';
    ln_fast_add_tag(&result, ctrl_tag);

    /* The estimate must cover the true worst-case output. */
    size_t est = ln_fast_json_estimate(&result);
    TEST_ASSERT(est >= 282, "estimate must cover ~282B control-char value output");

    /* Serialize must SUCCEED (no -1 / no silent drop) and fit our buffer. */
    int r = ln_fast_to_json(&result, json, sizeof(json), &out_len);
    TEST_ASSERT_EQ(r, 0, "control-char result must serialize without drop");
    TEST_ASSERT(out_len > 0, "serialized length must be non-zero");
    TEST_ASSERT(out_len <= est, "actual output must fit within the estimate");

    /* Round-trip evidence: each 0x01 byte became a 6-byte unicode
     * escape sequence in the serialized output. */
    TEST_ASSERT(strstr(json, "\\u0001") != NULL,
                "control byte must serialize as \\u0001 escape");

    /* And the allocating path (which reuses the same estimate) must agree. */
    char *jstr = NULL;
    size_t jlen = 0;
    int ra = ln_fast_to_json_alloc(&result, &jstr, &jlen);
    TEST_ASSERT_EQ(ra, 0, "alloc path must serialize control-char result");
    TEST_ASSERT(jstr != NULL && jlen > 0, "alloc path must yield output");
    free(jstr);

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Snapshot Tests
 *============================================================================*/

static int test_snapshot_create_basic(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Populate result */
    ln_fast_add_string_static(&result, "host", 4, "server01", 8);
    ln_fast_add_int_static(&result, "status", 6, 200);
    ln_fast_set_rule_id(&result, "rule_001");
    ln_fast_add_tag(&result, "web");

    /* Create snapshot */
    ln_fast_result_snapshot_t *snap = ln_fast_result_snapshot_create(&result, &arena);
    TEST_ASSERT(snap != NULL, "snapshot should be created");

    /* Get result from snapshot */
    const ln_fast_result_t *sr = ln_fast_result_snapshot_get(snap);
    TEST_ASSERT(sr != NULL, "get should return non-NULL");
    TEST_ASSERT_EQ(sr->n_fields, 2, "snapshot should have 2 fields");
    TEST_ASSERT_EQ(sr->n_tags, 1, "snapshot should have 1 tag");
    TEST_ASSERT_STR_EQ(sr->rule_id, "rule_001", "rule_id should match");

    /* Verify field values survive snapshot */
    TEST_ASSERT_EQ(sr->fields[0].type, LN_FTYPE_STRING_INLINE, "field 0 type");
    TEST_ASSERT(memcmp(sr->fields[0].v.inl, "server01", 8) == 0, "field 0 value");
    TEST_ASSERT_EQ(sr->fields[1].type, LN_FTYPE_INT, "field 1 type");
    TEST_ASSERT_EQ(sr->fields[1].v.i, 200, "field 1 value");

    /* Arena pointer should be NULL in snapshot (self-contained) */
    TEST_ASSERT(sr->arena == NULL, "snapshot arena should be NULL");

    ln_fast_result_snapshot_free(snap);
    ln_arena_destroy(&arena);
    return 1;
}

static int test_snapshot_survives_arena_reset(void)
{
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Use arena-allocated string (external, large) */
    char *val = ln_arena_strndup(&arena, "this-is-an-arena-allocated-value-that-is-long-enough-to-be-external", 66);
    ln_fast_add_string_static(&result, "data", 4, val, 66);
    ln_fast_set_rule_id(&result, "rule_snap");

    /* Snapshot before reset */
    ln_fast_result_snapshot_t *snap = ln_fast_result_snapshot_create(&result, &arena);
    TEST_ASSERT(snap != NULL, "snapshot should be created");

    /* Reset arena (invalidates original pointers) */
    ln_arena_reset(&arena);

    /* Snapshot should still be valid */
    const ln_fast_result_t *sr = ln_fast_result_snapshot_get(snap);
    TEST_ASSERT(sr != NULL, "snapshot should still be valid");
    TEST_ASSERT_EQ(sr->n_fields, 1, "should have 1 field");

    /* The external string pointer should now point into the snapshot's arena_data */
    if (sr->fields[0].type == LN_FTYPE_STRING) {
        TEST_ASSERT(memcmp(sr->fields[0].v.str.ptr, "this-is-an-arena-allocated-value-that-is-long-enough-to-be-external", 66) == 0,
                    "rebased pointer should have correct data");
    }

    ln_fast_result_snapshot_free(snap);
    ln_arena_destroy(&arena);
    return 1;
}

static int test_snapshot_self_contained_external_value(void)
{
    /* Regression: long (>= 48B) field VALUES are stored by
     * ln_fast_add_string_static as non-owning pointers straight into the
     * input line (LN_FFIELD_STATIC_VAL, type LN_FTYPE_STRING) — they are NOT
     * in the arena, so the old snapshot copied the raw input-line pointer
     * verbatim. After the input line is freed/overwritten that pointer
     * dangles (use-after-free). The snapshot must copy such values into its
     * own backing buffer.  Under ASan, the pre-fix code reads freed memory. */
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    /* Heap-allocated input line (so ASan tracks the free precisely). */
    static const char payload[] =
        "this-external-value-is-well-over-forty-eight-bytes-and-lives-in-the-input-line";
    const size_t plen = sizeof(payload) - 1;   /* 78 bytes, >= 48 -> external */
    char *input = malloc(plen + 1);
    TEST_ASSERT(input != NULL, "input alloc");
    memcpy(input, payload, plen + 1);

    /* External string VALUE that points into the input buffer. */
    ln_fast_add_string_static(&result, "long_field", 10, input, (uint32_t)plen);
    TEST_ASSERT_EQ(result.fields[0].type, LN_FTYPE_STRING,
                   "value should be external (>= 48B)");
    TEST_ASSERT(result.fields[0].v.str.ptr == input,
                "pre-snapshot pointer references the input line");

    /* original also points into the input line. */
    ln_fast_set_original(&result, input, (uint32_t)plen);

    /* Snapshot — empty arena, value/original are NOT arena-backed. */
    ln_fast_result_snapshot_t *snap = ln_fast_result_snapshot_create(&result, &arena);
    TEST_ASSERT(snap != NULL, "snapshot should be created");

    /* The snapshot value pointer must NOT alias the input line anymore. */
    const ln_fast_result_t *sr = ln_fast_result_snapshot_get(snap);
    TEST_ASSERT(sr->fields[0].v.str.ptr != input,
                "snapshot value must not alias the input line");

    /* Scribble + free the input line: any dangling pointer now reads garbage
     * (or freed memory under ASan). */
    memset(input, 'Z', plen);
    free(input);

    /* Snapshot must still return the original bytes. */
    TEST_ASSERT_EQ(sr->fields[0].v.str.len, (uint32_t)plen, "value length preserved");
    TEST_ASSERT(memcmp(sr->fields[0].v.str.ptr, payload, plen) == 0,
                "snapshot value survives input-line free/overwrite");
    TEST_ASSERT_EQ(sr->fields[0].v.str.ptr[plen], '\0',
                "snapshot value is NUL-terminated");

    /* original must also be self-contained. */
    TEST_ASSERT(sr->original != NULL, "original preserved");
    TEST_ASSERT(memcmp(sr->original, payload, plen) == 0,
                "snapshot original survives input-line free/overwrite");

    ln_fast_result_snapshot_free(snap);
    ln_arena_destroy(&arena);
    return 1;
}

static int test_snapshot_null_safety(void)
{
    /* NULL source */
    ln_fast_result_snapshot_t *snap = ln_fast_result_snapshot_create(NULL, NULL);
    TEST_ASSERT(snap == NULL, "NULL source should return NULL");

    /* NULL get */
    const ln_fast_result_t *r = ln_fast_result_snapshot_get(NULL);
    TEST_ASSERT(r == NULL, "NULL get should return NULL");

    /* NULL free should not crash */
    ln_fast_result_snapshot_free(NULL);

    return 1;
}

static int test_snapshot_no_arena(void)
{
    /* Result without arena data (all inline/static strings) */
    ln_arena_t arena;
    ln_fast_result_t result;

    ln_arena_init(&arena);
    ln_fast_result_init(&result, &arena);

    ln_fast_add_string_static(&result, "host", 4, "srv1", 4);  /* inline */
    ln_fast_add_int_static(&result, "code", 4, 42);

    /* Create snapshot with arena that has no used data */
    ln_arena_t empty_arena;
    ln_arena_init(&empty_arena);

    ln_fast_result_snapshot_t *snap = ln_fast_result_snapshot_create(&result, &empty_arena);
    TEST_ASSERT(snap != NULL, "snapshot should succeed with empty arena");

    const ln_fast_result_t *sr = ln_fast_result_snapshot_get(snap);
    TEST_ASSERT_EQ(sr->n_fields, 2, "should have 2 fields");
    TEST_ASSERT(memcmp(sr->fields[0].v.inl, "srv1", 4) == 0, "inline value preserved");

    ln_fast_result_snapshot_free(snap);
    ln_arena_destroy(&empty_arena);
    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void)
{
    printf("=== ln_fast_result Comprehensive Test Suite ===\n\n");

    /* Lifecycle tests */
    printf("Lifecycle tests:\n");
    RUN_TEST(test_init);
    RUN_TEST(test_clear);
    RUN_TEST(test_clear_and_reuse);
    printf("\n");

    /* String field tests */
    printf("String field tests:\n");
    RUN_TEST(test_add_string_inline);
    RUN_TEST(test_add_string_external);
    RUN_TEST(test_add_string_boundary_47);
    RUN_TEST(test_add_string_boundary_48);
    RUN_TEST(test_add_string_empty);
    printf("\n");

    /* Integer field tests */
    printf("Integer field tests:\n");
    RUN_TEST(test_add_int_positive);
    RUN_TEST(test_add_int_negative);
    RUN_TEST(test_add_int_zero);
    RUN_TEST(test_add_int_extremes);
    printf("\n");

    /* Double field tests */
    printf("Double field tests:\n");
    RUN_TEST(test_add_double_positive);
    RUN_TEST(test_add_double_negative);
    RUN_TEST(test_add_double_zero);
    RUN_TEST(test_add_double_precise);
    printf("\n");

    /* Tag tests */
    printf("Tag tests:\n");
    RUN_TEST(test_add_tag);
    RUN_TEST(test_add_tag_duplicate_same_pointer);
    RUN_TEST(test_has_tag);
    RUN_TEST(test_has_tag_null_safety);
    RUN_TEST(test_tag_overflow);
    RUN_TEST(test_tag_hash_bitmap);
    printf("\n");

    /* Capacity tests */
    printf("Capacity tests:\n");
    RUN_TEST(test_max_fields);
    RUN_TEST(test_field_overflow_all_types);
    printf("\n");

    /* Metadata tests */
    printf("Metadata tests:\n");
    RUN_TEST(test_rule_id);
    RUN_TEST(test_original_message);
    RUN_TEST(test_flags_combination);
    printf("\n");

    /* Nested field detection */
    printf("Nested field detection tests:\n");
    RUN_TEST(test_nested_field_detection);
    RUN_TEST(test_nested_flag_on_add);
    printf("\n");

    /* Hash functions */
    printf("Hash function tests:\n");
    RUN_TEST(test_hash_functions);
    printf("\n");

    /* Structure tests */
    printf("Structure tests:\n");
    RUN_TEST(test_field_size);
    RUN_TEST(test_mixed_field_types);
    printf("\n");

    /* JSON estimate robustness tests */
    printf("JSON estimate robustness tests:\n");
    RUN_TEST(test_json_estimate_control_chars);
    printf("\n");

    /* Snapshot tests */
    printf("Snapshot tests:\n");
    RUN_TEST(test_snapshot_create_basic);
    RUN_TEST(test_snapshot_survives_arena_reset);
    RUN_TEST(test_snapshot_self_contained_external_value);
    RUN_TEST(test_snapshot_null_safety);
    RUN_TEST(test_snapshot_no_arena);
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
