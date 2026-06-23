/**
 * @file turbo_test_arena.c
 * @brief Comprehensive test suite for turbo_arena
 *
 * Coverage:
 * - Lifecycle: init (default, sized, static), destroy (idempotent, NULL-safe)
 * - Allocation: basic, alignment (8/16/32/64), zero-size, exhaustion, calloc
 * - Invalid alignments: 0, non-power-of-2, excessive
 * - String operations: strdup, strndup (with embedded NUL), memdup
 * - Reset and backtracking: reset, mark/restore, nested marks
 * - Query functions: capacity, used, available, peak, has_space
 * - Statistics: get_stats, NULL safety
 * - Stress: many small allocations, alignment stress
 * - Macros: LN_ARENA_NEW, LN_ARENA_ARRAY, LN_ARENA_NEW_ALIGNED
 * - Peak tracking: preserved across reset, monotonic
 * - Configuration constants: LN_ARENA_DEFAULT_CAPACITY, MIN, MAX
 *
 * @author Jeremie Jourdin / Advens
 * @copyright 2026 Advens. Released under ASL 2.0.
 */

#include "config.h"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

#ifdef ENABLE_TURBO

#include "turbo_arena.h"

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
 * Configuration Constants Tests
 *============================================================================*/

static int test_constants(void)
{
    TEST_ASSERT(LN_ARENA_DEFAULT_CAPACITY == 16384,
                "default capacity should be 16KB");
    TEST_ASSERT(LN_ARENA_MIN_CAPACITY == 64,
                "min capacity should be 64 bytes");
    TEST_ASSERT(LN_ARENA_MAX_CAPACITY == (16 * 1024 * 1024),
                "max capacity should be 16MB");
    TEST_ASSERT(LN_ARENA_DEFAULT_ALIGN == 8,
                "default alignment should be 8 bytes");
    TEST_ASSERT(LN_ARENA_CACHE_LINE == 64,
                "cache line should be 64 bytes");
    return 1;
}

/*============================================================================
 * Basic Lifecycle Tests
 *============================================================================*/

static int test_init_default(void)
{
    ln_arena_t arena;
    int r = ln_arena_init(&arena);

    TEST_ASSERT_EQ(r, LN_ARENA_OK, "init should succeed");
    TEST_ASSERT(arena.base != NULL, "base should be allocated");
    TEST_ASSERT_EQ((long long)arena.capacity, LN_ARENA_DEFAULT_CAPACITY,
                   "capacity should be default");
    TEST_ASSERT_EQ((long long)arena.used, 0, "used should be 0");
    TEST_ASSERT_EQ((long long)arena.peak, 0, "peak should be 0");
    TEST_ASSERT(arena.flags & LN_ARENA_FLAG_OWNED, "should own memory");

    ln_arena_destroy(&arena);
    TEST_ASSERT(arena.base == NULL, "base should be NULL after destroy");

    return 1;
}

static int test_init_sized(void)
{
    ln_arena_t arena;

    /* Test with various sizes */
    int r = ln_arena_init_sized(&arena, 4096);
    TEST_ASSERT_EQ(r, LN_ARENA_OK, "init_sized should succeed");
    TEST_ASSERT(arena.capacity >= 4096, "capacity should be at least 4096");
    ln_arena_destroy(&arena);

    /* Test minimum clamping */
    r = ln_arena_init_sized(&arena, 1);
    TEST_ASSERT_EQ(r, LN_ARENA_OK, "init_sized with small value should succeed");
    TEST_ASSERT(arena.capacity >= LN_ARENA_MIN_CAPACITY,
                "capacity should be clamped to minimum");
    ln_arena_destroy(&arena);

    /* Test maximum clamping */
    r = ln_arena_init_sized(&arena, SIZE_MAX);
    TEST_ASSERT_EQ(r, LN_ARENA_OK, "init_sized with huge value should succeed");
    TEST_ASSERT(arena.capacity <= LN_ARENA_MAX_CAPACITY,
                "capacity should be clamped to maximum");
    ln_arena_destroy(&arena);

    return 1;
}

static int test_init_static(void)
{
    ln_arena_t arena;
    uint8_t buffer[1024];

    int r = ln_arena_init_static(&arena, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(r, LN_ARENA_OK, "init_static should succeed");
    TEST_ASSERT(arena.base == buffer, "base should point to provided buffer");
    TEST_ASSERT_EQ((long long)arena.capacity, (long long)sizeof(buffer),
                   "capacity should match buffer size");
    TEST_ASSERT(arena.flags & LN_ARENA_FLAG_STATIC, "should be marked static");
    TEST_ASSERT(!(arena.flags & LN_ARENA_FLAG_OWNED), "should not own memory");

    /* Destroy should not crash or free the static buffer */
    ln_arena_destroy(&arena);
    TEST_ASSERT(arena.base == NULL, "base should be NULL after destroy");

    /* Buffer should still be usable (not freed) */
    buffer[0] = 42;
    TEST_ASSERT_EQ(buffer[0], 42, "static buffer should still be valid");

    return 1;
}

static int test_init_errors(void)
{
    int r;
    ln_arena_t arena;

    /* NULL arena */
    r = ln_arena_init(NULL);
    TEST_ASSERT_EQ(r, LN_ARENA_EINVAL, "init with NULL should fail");

    r = ln_arena_init_sized(NULL, 1024);
    TEST_ASSERT_EQ(r, LN_ARENA_EINVAL, "init_sized with NULL should fail");

    /* Static buffer errors */
    r = ln_arena_init_static(NULL, (void *)1, 1024);
    TEST_ASSERT_EQ(r, LN_ARENA_EINVAL, "init_static with NULL arena should fail");

    r = ln_arena_init_static(&arena, NULL, 1024);
    TEST_ASSERT_EQ(r, LN_ARENA_EINVAL, "init_static with NULL buffer should fail");

    r = ln_arena_init_static(&arena, (void *)1, 1);
    TEST_ASSERT_EQ(r, LN_ARENA_EINVAL, "init_static with tiny buffer should fail");

    return 1;
}

static int test_destroy_idempotent(void)
{
    ln_arena_t arena;

    /* Destroy NULL should be safe */
    ln_arena_destroy(NULL);

    /* Double destroy should be safe */
    ln_arena_init(&arena);
    ln_arena_destroy(&arena);
    ln_arena_destroy(&arena);

    return 1;
}

/*============================================================================
 * Allocation Tests
 *============================================================================*/

static int test_alloc_basic(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    void *p1 = ln_arena_alloc(&arena, 100);
    TEST_ASSERT(p1 != NULL, "first alloc should succeed");

    void *p2 = ln_arena_alloc(&arena, 200);
    TEST_ASSERT(p2 != NULL, "second alloc should succeed");
    TEST_ASSERT(p2 != p1, "allocations should be distinct");
    TEST_ASSERT((uintptr_t)p2 > (uintptr_t)p1, "allocations should grow upward");

    TEST_ASSERT_EQ(arena.alloc_count, 2, "alloc_count should be 2");
    TEST_ASSERT(arena.used >= 300, "used should be at least 300");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_alloc_alignment(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    /* Default alignment should be 8 bytes */
    void *p1 = ln_arena_alloc(&arena, 1);
    TEST_ASSERT(((uintptr_t)p1 % LN_ARENA_DEFAULT_ALIGN) == 0,
                "default alloc should be aligned");

    /* Explicit alignments */
    void *p16 = ln_arena_alloc_aligned(&arena, 32, 16);
    TEST_ASSERT(p16 != NULL, "16-byte aligned alloc should succeed");
    TEST_ASSERT(((uintptr_t)p16 % 16) == 0, "should be 16-byte aligned");

    void *p32 = ln_arena_alloc_aligned(&arena, 64, 32);
    TEST_ASSERT(p32 != NULL, "32-byte aligned alloc should succeed");
    TEST_ASSERT(((uintptr_t)p32 % 32) == 0, "should be 32-byte aligned");

    void *p64 = ln_arena_alloc_aligned(&arena, 128, 64);
    TEST_ASSERT(p64 != NULL, "64-byte aligned alloc should succeed");
    TEST_ASSERT(((uintptr_t)p64 % 64) == 0, "should be 64-byte aligned");

    /* Invalid alignments */
    void *bad1 = ln_arena_alloc_aligned(&arena, 32, 0);
    TEST_ASSERT(bad1 == NULL, "0 alignment should fail");

    void *bad2 = ln_arena_alloc_aligned(&arena, 32, 3);
    TEST_ASSERT(bad2 == NULL, "non-power-of-2 alignment should fail");

    void *bad3 = ln_arena_alloc_aligned(&arena, 32, 8192);
    TEST_ASSERT(bad3 == NULL, "excessive alignment should fail");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_alloc_zero_size(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    /* Zero-size allocation should return valid unique pointer */
    void *p1 = ln_arena_alloc(&arena, 0);
    TEST_ASSERT(p1 != NULL, "zero-size alloc should succeed");

    void *p2 = ln_arena_alloc(&arena, 0);
    TEST_ASSERT(p2 != NULL, "second zero-size alloc should succeed");
    TEST_ASSERT(p2 != p1, "zero-size allocations should be distinct");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_alloc_exhaustion(void)
{
    ln_arena_t arena;
    uint8_t buffer[256];
    ln_arena_init_static(&arena, buffer, sizeof(buffer));

    /* Fill most of the arena */
    void *p1 = ln_arena_alloc(&arena, 200);
    TEST_ASSERT(p1 != NULL, "first large alloc should succeed");

    /* This should fail - not enough space */
    void *p2 = ln_arena_alloc(&arena, 100);
    TEST_ASSERT(p2 == NULL, "alloc beyond capacity should fail");

    /* Smaller alloc might still succeed depending on alignment padding */
    void *p3 = ln_arena_alloc(&arena, 10);
    (void)p3;  /* May or may not be NULL depending on alignment */

    ln_arena_destroy(&arena);
    return 1;
}

static int test_calloc(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    /* Allocate and verify zeroed */
    uint8_t *p = (uint8_t *)ln_arena_calloc(&arena, 256);
    TEST_ASSERT(p != NULL, "calloc should succeed");

    int all_zero = 1;
    for (int i = 0; i < 256; i++) {
        if (p[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    TEST_ASSERT(all_zero, "calloc memory should be zeroed");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_alloc_write_read(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    /* Allocate and write data, then verify it's still there */
    char *p1 = (char *)ln_arena_alloc(&arena, 32);
    TEST_ASSERT(p1 != NULL, "alloc should succeed");
    memcpy(p1, "hello world", 12);

    int *p2 = (int *)ln_arena_alloc(&arena, sizeof(int) * 10);
    TEST_ASSERT(p2 != NULL, "alloc should succeed");
    for (int i = 0; i < 10; i++) p2[i] = i * 100;

    /* Verify first allocation is still intact */
    TEST_ASSERT(memcmp(p1, "hello world", 12) == 0,
                "first allocation data should be intact");

    /* Verify second allocation */
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQ(p2[i], i * 100, "array data should be intact");
    }

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * String Operations Tests
 *============================================================================*/

static int test_strdup(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    const char *original = "Hello, World!";
    char *copy = ln_arena_strdup(&arena, original);

    TEST_ASSERT(copy != NULL, "strdup should succeed");
    TEST_ASSERT(copy != original, "copy should be different pointer");
    TEST_ASSERT(strcmp(copy, original) == 0, "copy should match original");

    /* NULL string */
    char *null_copy = ln_arena_strdup(&arena, NULL);
    TEST_ASSERT(null_copy == NULL, "strdup of NULL should return NULL");

    /* Empty string */
    char *empty = ln_arena_strdup(&arena, "");
    TEST_ASSERT(empty != NULL, "strdup of empty string should succeed");
    TEST_ASSERT(empty[0] == '\0', "empty string copy should be empty");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_strndup(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    const char *original = "Hello, World!";

    /* Copy partial string */
    char *partial = ln_arena_strndup(&arena, original, 5);
    TEST_ASSERT(partial != NULL, "strndup should succeed");
    TEST_ASSERT(strcmp(partial, "Hello") == 0, "partial copy should match");
    TEST_ASSERT_EQ((long long)strlen(partial), 5, "partial copy length should be 5");

    /* Copy full string by specifying exact length */
    size_t orig_len = strlen(original);
    char *full = ln_arena_strndup(&arena, original, orig_len);
    TEST_ASSERT(full != NULL, "strndup with exact len should succeed");
    TEST_ASSERT(strcmp(full, original) == 0, "full copy should match original");

    /* Copy binary data (contains null byte) */
    const char binary[] = "AB\0CD";
    char *bin_copy = ln_arena_strndup(&arena, binary, 5);
    TEST_ASSERT(bin_copy != NULL, "strndup of binary should succeed");
    TEST_ASSERT(bin_copy[0] == 'A', "first byte should be A");
    TEST_ASSERT(bin_copy[2] == '\0', "embedded null should be preserved");
    TEST_ASSERT(bin_copy[3] == 'C', "byte after null should be C");
    TEST_ASSERT(bin_copy[5] == '\0', "should be null-terminated");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_memdup(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    uint8_t data[] = {1, 2, 3, 4, 5, 0, 7, 8};
    uint8_t *copy = (uint8_t *)ln_arena_memdup(&arena, data, sizeof(data));

    TEST_ASSERT(copy != NULL, "memdup should succeed");
    TEST_ASSERT(memcmp(copy, data, sizeof(data)) == 0, "memdup should match");

    /* NULL source */
    void *null_copy = ln_arena_memdup(&arena, NULL, 10);
    TEST_ASSERT(null_copy == NULL, "memdup of NULL should return NULL");

    /* Zero size */
    void *zero_copy = ln_arena_memdup(&arena, data, 0);
    TEST_ASSERT(zero_copy == NULL, "memdup of size 0 should return NULL");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Reset and Backtracking Tests
 *============================================================================*/

static int test_reset(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    /* Allocate some memory */
    ln_arena_alloc(&arena, 100);
    ln_arena_alloc(&arena, 200);
    ln_arena_alloc(&arena, 300);

    size_t used_before = arena.used;
    size_t peak_before = arena.peak;
    TEST_ASSERT(used_before > 0, "should have used some memory");
    TEST_ASSERT_EQ(arena.alloc_count, 3, "should have 3 allocations");

    /* Reset */
    ln_arena_reset(&arena);

    TEST_ASSERT_EQ((long long)arena.used, 0, "used should be 0 after reset");
    TEST_ASSERT_EQ(arena.alloc_count, 0, "alloc_count should be 0 after reset");
    TEST_ASSERT_EQ((long long)arena.peak, (long long)peak_before,
                   "peak should be preserved after reset");

    /* Should be able to allocate again */
    void *p = ln_arena_alloc(&arena, 100);
    TEST_ASSERT(p != NULL, "should be able to alloc after reset");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_mark_restore(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    /* Allocate initial data */
    char *p1 = ln_arena_strdup(&arena, "first");
    TEST_ASSERT(p1 != NULL, "first strdup should succeed");

    /* Save mark */
    ln_arena_mark_t mark;
    ln_arena_save(&arena, &mark);
    size_t used_at_mark = arena.used;

    /* Allocate more data */
    char *p2 = ln_arena_strdup(&arena, "second");
    char *p3 = ln_arena_strdup(&arena, "third");
    TEST_ASSERT(p2 != NULL && p3 != NULL, "additional allocations should succeed");
    TEST_ASSERT(arena.used > used_at_mark, "should have used more memory");

    /* Restore to mark */
    ln_arena_restore(&arena, &mark);

    TEST_ASSERT_EQ((long long)arena.used, (long long)used_at_mark,
                   "used should be restored");

    /* p2 and p3 are now invalid, but p1 should still be valid */
    TEST_ASSERT(strcmp(p1, "first") == 0, "p1 should still be valid");

    /* New allocation should reuse space */
    char *p4 = ln_arena_strdup(&arena, "new");
    TEST_ASSERT(p4 != NULL, "allocation after restore should succeed");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_nested_marks(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    ln_arena_strdup(&arena, "level0");

    ln_arena_mark_t mark1;
    ln_arena_save(&arena, &mark1);
    size_t used1 = arena.used;

    ln_arena_strdup(&arena, "level1");

    ln_arena_mark_t mark2;
    ln_arena_save(&arena, &mark2);
    size_t used2 = arena.used;

    ln_arena_strdup(&arena, "level2");
    size_t used3 = arena.used;

    TEST_ASSERT(used1 < used2 && used2 < used3, "usage should increase");

    /* Restore to mark2 */
    ln_arena_restore(&arena, &mark2);
    TEST_ASSERT_EQ((long long)arena.used, (long long)used2,
                   "should restore to mark2");

    /* Restore to mark1 */
    ln_arena_restore(&arena, &mark1);
    TEST_ASSERT_EQ((long long)arena.used, (long long)used1,
                   "should restore to mark1");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Query Function Tests
 *============================================================================*/

static int test_query_functions(void)
{
    ln_arena_t arena;
    ln_arena_init_sized(&arena, 1024);

    TEST_ASSERT(ln_arena_capacity(&arena) >= 1024,
                "capacity should be at least 1024");
    TEST_ASSERT_EQ((long long)ln_arena_used(&arena), 0,
                   "initial used should be 0");
    TEST_ASSERT_EQ((long long)ln_arena_available(&arena),
                   (long long)ln_arena_capacity(&arena),
                   "initial available should equal capacity");
    TEST_ASSERT_EQ((long long)ln_arena_peak(&arena), 0,
                   "initial peak should be 0");

    /* Allocate some memory */
    ln_arena_alloc(&arena, 100);

    TEST_ASSERT(ln_arena_used(&arena) >= 100, "used should be at least 100");
    TEST_ASSERT(ln_arena_available(&arena) < ln_arena_capacity(&arena),
                "available should decrease");
    TEST_ASSERT_EQ((long long)ln_arena_peak(&arena),
                   (long long)ln_arena_used(&arena),
                   "peak should equal used");

    TEST_ASSERT(ln_arena_has_space(&arena, 100), "should have space for 100");
    TEST_ASSERT(!ln_arena_has_space(&arena, 100000),
                "should not have space for 100000");

    ln_arena_destroy(&arena);

    /* Query on NULL should return 0/safe values */
    TEST_ASSERT_EQ((long long)ln_arena_capacity(NULL), 0,
                   "NULL capacity should be 0");
    TEST_ASSERT_EQ((long long)ln_arena_used(NULL), 0,
                   "NULL used should be 0");
    TEST_ASSERT_EQ((long long)ln_arena_available(NULL), 0,
                   "NULL available should be 0");
    TEST_ASSERT(!ln_arena_has_space(NULL, 1), "NULL should not have space");

    return 1;
}

static int test_stats(void)
{
    ln_arena_t arena;
    ln_arena_init_sized(&arena, 4096);

    ln_arena_alloc(&arena, 100);
    ln_arena_alloc(&arena, 200);
    ln_arena_alloc(&arena, 300);

    ln_arena_stats_t stats;
    ln_arena_get_stats(&arena, &stats);

    TEST_ASSERT(stats.capacity >= 4096, "stats capacity should match");
    TEST_ASSERT(stats.used >= 600, "stats used should be at least 600");
    TEST_ASSERT_EQ((long long)stats.peak, (long long)stats.used,
                   "stats peak should equal used");
    TEST_ASSERT_EQ((long long)stats.available,
                   (long long)(stats.capacity - stats.used),
                   "stats available should be correct");
    TEST_ASSERT_EQ(stats.alloc_count, 3, "stats alloc_count should be 3");
    TEST_ASSERT(stats.utilization > 0.0 && stats.utilization < 1.0,
                "utilization should be between 0 and 1");

    /* Stats on NULL */
    ln_arena_stats_t null_stats;
    memset(&null_stats, 0xFF, sizeof(null_stats));
    ln_arena_get_stats(NULL, &null_stats);
    TEST_ASSERT_EQ((long long)null_stats.capacity, 0,
                   "NULL arena stats should be zeroed");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Peak Tracking Tests
 *============================================================================*/

static int test_peak_tracking(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    /* Allocate some, note peak */
    ln_arena_alloc(&arena, 500);
    size_t peak1 = ln_arena_peak(&arena);
    TEST_ASSERT(peak1 >= 500, "peak should be >= 500");

    /* Allocate more */
    ln_arena_alloc(&arena, 300);
    size_t peak2 = ln_arena_peak(&arena);
    TEST_ASSERT(peak2 >= peak1, "peak should be monotonically increasing");

    /* Reset and re-allocate less */
    ln_arena_reset(&arena);
    ln_arena_alloc(&arena, 100);
    size_t peak3 = ln_arena_peak(&arena);
    TEST_ASSERT_EQ((long long)peak3, (long long)peak2,
                   "peak should survive reset (high water mark)");

    /* Allocate past previous peak */
    ln_arena_alloc(&arena, 1000);
    size_t peak4 = ln_arena_peak(&arena);
    TEST_ASSERT(peak4 > peak2, "peak should increase when exceeding HWM");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Stress Tests
 *============================================================================*/

static int test_many_allocations(void)
{
    ln_arena_t arena;
    ln_arena_init_sized(&arena, 1024 * 1024);  /* 1MB */

    /* Many small allocations */
    for (int i = 0; i < 10000; i++) {
        void *p = ln_arena_alloc(&arena, 16);
        if (!p) {
            TEST_ASSERT(0, "allocation should not fail with 1MB arena");
        }
    }

    TEST_ASSERT_EQ(arena.alloc_count, 10000, "should have 10000 allocations");

    /* Reset and do it again */
    ln_arena_reset(&arena);
    TEST_ASSERT_EQ(arena.alloc_count, 0, "alloc_count should reset");

    for (int i = 0; i < 10000; i++) {
        void *p = ln_arena_alloc(&arena, 16);
        TEST_ASSERT(p != NULL, "allocation after reset should succeed");
    }

    ln_arena_destroy(&arena);
    return 1;
}

static int test_alignment_stress(void)
{
    ln_arena_t arena;
    ln_arena_init_sized(&arena, 1024 * 1024);

    /* Mix of different alignments */
    size_t alignments[] = {1, 2, 4, 8, 16, 32, 64};
    int num_alignments = sizeof(alignments) / sizeof(alignments[0]);

    for (int i = 0; i < 1000; i++) {
        size_t align = alignments[i % num_alignments];
        size_t size = (size_t)(i % 100) + 1;

        void *p = ln_arena_alloc_aligned(&arena, size, align);
        TEST_ASSERT(p != NULL, "aligned alloc should succeed");
        TEST_ASSERT(((uintptr_t)p % align) == 0, "alignment should be correct");
    }

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Macro Tests
 *============================================================================*/

static int test_macros(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    /* Test LN_ARENA_NEW */
    typedef struct {
        int x;
        double y;
        char z[32];
    } test_struct_t;

    test_struct_t *obj = LN_ARENA_NEW(&arena, test_struct_t);
    TEST_ASSERT(obj != NULL, "LN_ARENA_NEW should succeed");
    obj->x = 42;
    obj->y = 3.14;
    strcpy(obj->z, "test");

    /* Test LN_ARENA_ARRAY */
    int *arr = LN_ARENA_ARRAY(&arena, int, 100);
    TEST_ASSERT(arr != NULL, "LN_ARENA_ARRAY should succeed");
    for (int i = 0; i < 100; i++) {
        arr[i] = i;
    }

    /* Verify data integrity after more allocations */
    TEST_ASSERT_EQ(obj->x, 42, "struct data should be intact");
    TEST_ASSERT_EQ(arr[50], 50, "array data should be intact");

    /* Test LN_ARENA_NEW_ALIGNED */
    typedef struct {
        uint8_t data[64];
    } aligned_struct_t;

    aligned_struct_t *aligned_obj = LN_ARENA_NEW_ALIGNED(&arena, aligned_struct_t, 64);
    TEST_ASSERT(aligned_obj != NULL, "LN_ARENA_NEW_ALIGNED should succeed");
    TEST_ASSERT(((uintptr_t)aligned_obj % 64) == 0, "should be 64-byte aligned");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_alloc_array_overflow(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    /* Hostile element count: (SIZE_MAX/2 + 1) * 4 wraps a size_t multiply.
     * The checked allocator must reject it rather than return a tiny slot. */
    void *wrapped = ln_arena_alloc_array(&arena, SIZE_MAX / 2 + 1, 4);
    TEST_ASSERT(wrapped == NULL, "overflowing array alloc should return NULL");

    /* A normal allocation must still succeed and be a valid, distinct slot. */
    int *ok1 = (int *)ln_arena_alloc_array(&arena, 10, sizeof(int));
    TEST_ASSERT(ok1 != NULL, "valid array alloc should succeed");
    for (int i = 0; i < 10; i++) ok1[i] = i;
    TEST_ASSERT_EQ(ok1[9], 9, "array data should be writable/intact");

    int *ok2 = (int *)ln_arena_alloc_array(&arena, 10, sizeof(int));
    TEST_ASSERT(ok2 != NULL, "second valid array alloc should succeed");
    TEST_ASSERT(ok2 != ok1, "array allocations should be distinct");

    /* elem == 0 must not divide-by-zero; it is a benign zero-size request. */
    void *zero_elem = ln_arena_alloc_array(&arena, SIZE_MAX, 0);
    TEST_ASSERT(zero_elem != NULL, "zero element size should be a valid alloc");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Error Code Tests
 *============================================================================*/

static int test_error_codes(void)
{
    /* Verify error code values are distinct and negative */
    TEST_ASSERT_EQ(LN_ARENA_OK, 0, "OK should be 0");
    TEST_ASSERT(LN_ARENA_EINVAL < 0, "EINVAL should be negative");
    TEST_ASSERT(LN_ARENA_ENOMEM < 0, "ENOMEM should be negative");
    TEST_ASSERT(LN_ARENA_EOVERFLOW < 0, "EOVERFLOW should be negative");
    TEST_ASSERT(LN_ARENA_EALIGN < 0, "EALIGN should be negative");

    /* All error codes should be distinct */
    TEST_ASSERT(LN_ARENA_EINVAL != LN_ARENA_ENOMEM, "EINVAL != ENOMEM");
    TEST_ASSERT(LN_ARENA_EINVAL != LN_ARENA_EOVERFLOW, "EINVAL != EOVERFLOW");
    TEST_ASSERT(LN_ARENA_EINVAL != LN_ARENA_EALIGN, "EINVAL != EALIGN");
    TEST_ASSERT(LN_ARENA_ENOMEM != LN_ARENA_EOVERFLOW, "ENOMEM != EOVERFLOW");
    TEST_ASSERT(LN_ARENA_ENOMEM != LN_ARENA_EALIGN, "ENOMEM != EALIGN");
    TEST_ASSERT(LN_ARENA_EOVERFLOW != LN_ARENA_EALIGN, "EOVERFLOW != EALIGN");

    return 1;
}

/*============================================================================
 * Per-Message Reuse Pattern
 *============================================================================*/

static int test_per_message_reuse(void)
{
    ln_arena_t arena;
    ln_arena_init(&arena);

    /* Simulate processing 100 log messages with arena reuse */
    for (int msg = 0; msg < 100; msg++) {
        ln_arena_reset(&arena);

        /* Simulate parse result allocations */
        char *host = ln_arena_strdup(&arena, "fw01.prod.example.com");
        char *ip = ln_arena_strndup(&arena, "192.168.1.100", 13);
        int *port = LN_ARENA_NEW(&arena, int);
        *port = 443 + msg;

        /* Verify data */
        TEST_ASSERT(host != NULL, "host alloc should succeed");
        TEST_ASSERT(ip != NULL, "ip alloc should succeed");
        TEST_ASSERT_EQ(*port, 443 + msg, "port should be correct");
    }

    /* Peak should reflect single-message usage, not accumulated */
    TEST_ASSERT(ln_arena_peak(&arena) < 1024,
                "peak should be small (single message worth)");

    ln_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void)
{
    printf("=== ln_arena Comprehensive Test Suite ===\n\n");

    /* Constants */
    printf("Configuration constants:\n");
    RUN_TEST(test_constants);
    RUN_TEST(test_error_codes);
    printf("\n");

    /* Lifecycle tests */
    printf("Lifecycle tests:\n");
    RUN_TEST(test_init_default);
    RUN_TEST(test_init_sized);
    RUN_TEST(test_init_static);
    RUN_TEST(test_init_errors);
    RUN_TEST(test_destroy_idempotent);
    printf("\n");

    /* Allocation tests */
    printf("Allocation tests:\n");
    RUN_TEST(test_alloc_basic);
    RUN_TEST(test_alloc_alignment);
    RUN_TEST(test_alloc_zero_size);
    RUN_TEST(test_alloc_exhaustion);
    RUN_TEST(test_calloc);
    RUN_TEST(test_alloc_write_read);
    printf("\n");

    /* String tests */
    printf("String operation tests:\n");
    RUN_TEST(test_strdup);
    RUN_TEST(test_strndup);
    RUN_TEST(test_memdup);
    printf("\n");

    /* Reset and backtracking tests */
    printf("Reset and backtracking tests:\n");
    RUN_TEST(test_reset);
    RUN_TEST(test_mark_restore);
    RUN_TEST(test_nested_marks);
    printf("\n");

    /* Query tests */
    printf("Query function tests:\n");
    RUN_TEST(test_query_functions);
    RUN_TEST(test_stats);
    printf("\n");

    /* Peak tracking */
    printf("Peak tracking tests:\n");
    RUN_TEST(test_peak_tracking);
    printf("\n");

    /* Stress tests */
    printf("Stress tests:\n");
    RUN_TEST(test_many_allocations);
    RUN_TEST(test_alignment_stress);
    printf("\n");

    /* Macro tests */
    printf("Macro tests:\n");
    RUN_TEST(test_macros);
    RUN_TEST(test_alloc_array_overflow);
    printf("\n");

    /* Usage pattern tests */
    printf("Usage pattern tests:\n");
    RUN_TEST(test_per_message_reuse);
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
