/**
 * @file turbo_test_simd.c
 * @brief Comprehensive test suite for turbo_simd primitives
 *
 * Tests all SIMD-accelerated parsing functions across all backends
 * (SSE4.2, NEON, scalar). Exercises both fast-path (>16 byte) and
 * tail-path (<16 byte) code paths to ensure correctness regardless
 * of the compiled backend.
 *
 * @author Jérémie Jourdin / Advens
 * @copyright 2026 Advens. Released under ASL 2.0.
 */

#include "config.h"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

#ifdef ENABLE_TURBO

#include "turbo_simd.h"
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
        fprintf(stderr, "  FAIL: %s - got %lld, expected %lld (line %d)\n", \
                msg, (long long)(a), (long long)(b), __LINE__); \
        return 0; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    tests_run++; \
    printf("Running %s... ", #test_func); \
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
 * Backend Tests
 *============================================================================*/

static int test_backend_info(void)
{
    const char *name = ln_simd_backend_name();
    TEST_ASSERT(name != NULL, "backend name should not be NULL");

    int width = ln_simd_width();
    TEST_ASSERT(width >= 1, "width should be at least 1");

    printf("(backend: %s, width: %d) ", name, width);

    return 1;
}

/*============================================================================
 * find_char Tests
 *============================================================================*/

static int test_find_char_basic(void)
{
    const char *buf = "hello world";

    TEST_ASSERT_EQ(ln_simd_find_char(buf, 11, 'h'), 0, "find 'h' at start");
    TEST_ASSERT_EQ(ln_simd_find_char(buf, 11, 'w'), 6, "find 'w'");
    TEST_ASSERT_EQ(ln_simd_find_char(buf, 11, 'd'), 10, "find 'd' at end");
    TEST_ASSERT_EQ(ln_simd_find_char(buf, 11, ' '), 5, "find space");
    TEST_ASSERT_EQ(ln_simd_find_char(buf, 11, 'x'), 11, "not found");

    return 1;
}

static int test_find_char_edge(void)
{
    const char *buf = "x";

    TEST_ASSERT_EQ(ln_simd_find_char(buf, 1, 'x'), 0, "single char found");
    TEST_ASSERT_EQ(ln_simd_find_char(buf, 1, 'y'), 1, "single char not found");
    TEST_ASSERT_EQ(ln_simd_find_char(buf, 0, 'x'), 0, "empty buffer");
    TEST_ASSERT_EQ(ln_simd_find_char(NULL, 0, 'x'), 0, "NULL buffer");

    return 1;
}

static int test_find_char_long(void)
{
    /* String longer than 16 bytes to exercise SIMD register boundary */
    const char *buf = "abcdefghijklmnopqrstuvwxyz0123456789";
    size_t len = strlen(buf);

    TEST_ASSERT_EQ(ln_simd_find_char(buf, len, 'a'), 0, "find 'a' at pos 0");
    TEST_ASSERT_EQ(ln_simd_find_char(buf, len, 'q'), 16, "find 'q' at pos 16 (crosses SIMD boundary)");
    TEST_ASSERT_EQ(ln_simd_find_char(buf, len, '9'), 35, "find '9' near end");
    TEST_ASSERT_EQ(ln_simd_find_char(buf, len, '!'), len, "not found in long string");

    return 1;
}

/*============================================================================
 * find_char_set / find_not_char_set Tests
 *============================================================================*/

static int test_find_char_set_basic(void)
{
    const char *buf = "hello world";

    /* Find first whitespace */
    TEST_ASSERT_EQ(ln_simd_find_char_set(buf, 11, " \t\n"), 5,
                   "find whitespace");

    /* Find first colon or space */
    const char *buf2 = "field:value";
    TEST_ASSERT_EQ(ln_simd_find_char_set(buf2, 11, ": "), 5,
                   "find colon or space");

    /* Nothing found */
    TEST_ASSERT_EQ(ln_simd_find_char_set(buf, 11, "!@#"), 11,
                   "no match returns len");

    return 1;
}

static int test_find_char_set_edge(void)
{
    /* Empty buffer */
    TEST_ASSERT_EQ(ln_simd_find_char_set("", 0, "abc"), 0, "empty buffer");

    /* Empty char set */
    TEST_ASSERT_EQ(ln_simd_find_char_set("hello", 5, ""), 5, "empty set");

    /* NULL inputs */
    TEST_ASSERT_EQ(ln_simd_find_char_set(NULL, 5, "abc"), 5, "NULL buffer");
    TEST_ASSERT_EQ(ln_simd_find_char_set("hello", 5, NULL), 5, "NULL chars");

    return 1;
}

static int test_find_char_set_long(void)
{
    /* Crosses SIMD register boundaries */
    const char *buf = "aaaaaaaaaaaaaaaaaaaaaaaaaaaa:rest";
    size_t len = strlen(buf);

    TEST_ASSERT_EQ(ln_simd_find_char_set(buf, len, ":"), 28,
                   "find colon after many a's");

    return 1;
}

static int test_find_not_char_set_basic(void)
{
    /* Skip digits */
    const char *buf = "12345abc";
    size_t pos = ln_simd_find_not_char_set(buf, 8, "0123456789");
    TEST_ASSERT_EQ(pos, 5, "first non-digit");

    /* All match */
    pos = ln_simd_find_not_char_set("aaaa", 4, "abc");
    TEST_ASSERT_EQ(pos, 4, "all chars in set returns len");

    return 1;
}

static int test_find_not_char_set_edge(void)
{
    /* Empty buffer */
    TEST_ASSERT_EQ(ln_simd_find_not_char_set("", 0, "abc"), 0, "empty buffer");

    /* NULL inputs */
    TEST_ASSERT_EQ(ln_simd_find_not_char_set(NULL, 5, "abc"), 0, "NULL buffer");
    TEST_ASSERT_EQ(ln_simd_find_not_char_set("hello", 5, NULL), 0, "NULL chars");

    /* First char doesn't match */
    TEST_ASSERT_EQ(ln_simd_find_not_char_set("xyz", 3, "abc"), 0, "first char not in set");

    return 1;
}

/*
 * Regression: embedded NUL must NOT truncate the SSE4.2 scan.
 *
 * The buggy implicit-length PCMPISTRI treated each 16-byte chunk as
 * ending at its first 0x00, so a delimiter after an embedded NUL was
 * silently skipped -> the SSE path mis-segmented lines that the NEON
 * and scalar paths parsed correctly (detection evasion). With the
 * explicit-length PCMPESTRI fix, all three backends must agree.
 *
 * The chunk below is exactly 16 bytes so it is consumed entirely by the
 * SIMD fast path (i + 16 <= len), exercising the patched code rather
 * than the scalar tail.
 */
static int test_embedded_nul_no_truncation(void)
{
    /* 16-byte chunk: "aaaa\0bbb;ccccccc" — NUL at idx 4, ';' at idx 8 */
    static const char chunk[16] = {
        'a','a','a','a','\0','b','b','b',
        ';','c','c','c','c','c','c','c'
    };

    /* find_char_set must find ';' at offset 8, not skip the chunk */
    TEST_ASSERT_EQ(ln_simd_find_char_set(chunk, 16, ";"), 8,
                   "find ';' after embedded NUL (SSE fast path)");

    /* find_not_char_set: first byte not in {a} is the NUL at offset 4 */
    TEST_ASSERT_EQ(ln_simd_find_not_char_set(chunk, 16, "a"), 4,
                   "first non-'a' is the embedded NUL at offset 4");

    /* skip_space: leading whitespace then NUL then ';' — NUL stops skip */
    static const char wschunk[16] = {
        ' ',' ',' ','\0',' ',' ',' ',';',
        'c','c','c','c','c','c','c','c'
    };
    TEST_ASSERT_EQ(ln_simd_skip_space(wschunk, 16), 3,
                   "skip_space stops at embedded NUL (offset 3)");

    /*
     * Cross-check: results must match the byte-accurate scalar reference,
     * regardless of which backend was compiled in.
     */
    {
        size_t ref = 16, k;
        for (k = 0; k < 16; k++) { if (chunk[k] == ';') { ref = k; break; } }
        TEST_ASSERT_EQ(ln_simd_find_char_set(chunk, 16, ";"), ref,
                       "find_char_set agrees with scalar reference");
    }

    return 1;
}

/*============================================================================
 * skip_space Tests
 *============================================================================*/

static int test_skip_space(void)
{
    TEST_ASSERT_EQ(ln_simd_skip_space("  hello", 7), 2, "skip 2 spaces");
    TEST_ASSERT_EQ(ln_simd_skip_space("hello", 5), 0, "no leading ws");
    TEST_ASSERT_EQ(ln_simd_skip_space("   ", 3), 3, "all whitespace");
    TEST_ASSERT_EQ(ln_simd_skip_space("", 0), 0, "empty");
    TEST_ASSERT_EQ(ln_simd_skip_space("\t\n\r hello", 9), 4, "mixed whitespace");

    return 1;
}

static int test_skip_space_long(void)
{
    /* >16 spaces to cross SIMD boundary */
    const char *buf = "                    hello";
    TEST_ASSERT_EQ(ln_simd_skip_space(buf, strlen(buf)), 20,
                   "skip 20 spaces (crosses SIMD boundary)");

    return 1;
}

/*============================================================================
 * skip_chars Tests
 *============================================================================*/

static int test_skip_chars(void)
{
    /* Skip digits */
    TEST_ASSERT_EQ(ln_simd_skip_chars("123abc", 6, "0123456789"), 3,
                   "skip 3 digits");

    /* Skip nothing */
    TEST_ASSERT_EQ(ln_simd_skip_chars("abc", 3, "0123456789"), 0,
                   "skip nothing");

    /* Skip all */
    TEST_ASSERT_EQ(ln_simd_skip_chars("aaa", 3, "a"), 3,
                   "skip all");

    return 1;
}

/*============================================================================
 * word Tests
 *============================================================================*/

static int test_word_basic(void)
{
    ln_span_t span;

    int r = ln_simd_word("hello world", 11, &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "should succeed");
    TEST_ASSERT_EQ(span.len, 5, "word length");
    TEST_ASSERT(memcmp(span.start, "hello", 5) == 0, "word content");

    return 1;
}

static int test_word_no_space(void)
{
    ln_span_t span;

    int r = ln_simd_word("hello", 5, &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "should succeed");
    TEST_ASSERT_EQ(span.len, 5, "entire string is word");

    return 1;
}

static int test_word_empty(void)
{
    ln_span_t span;

    /* Starting on whitespace returns ENOTFOUND */
    int r = ln_simd_word("   ", 3, &span);
    TEST_ASSERT_EQ(r, LN_SIMD_ENOTFOUND, "should return not found");

    return 1;
}

static int test_word_single_char(void)
{
    ln_span_t span;

    int r = ln_simd_word("a", 1, &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "single char word");
    TEST_ASSERT_EQ(span.len, 1, "length 1");
    TEST_ASSERT(span.start[0] == 'a', "char is 'a'");

    return 1;
}

static int test_word_long(void)
{
    ln_span_t span;

    /* Word longer than 16 bytes */
    const char *buf = "abcdefghijklmnopqrstuvwxyz rest";
    int r = ln_simd_word(buf, strlen(buf), &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "long word");
    TEST_ASSERT_EQ(span.len, 26, "26-char word");

    return 1;
}

/*============================================================================
 * char_to Tests
 *============================================================================*/

static int test_char_to_basic(void)
{
    ln_span_t span;

    int r = ln_simd_char_to("field:value", 11, ':', &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "should succeed");
    TEST_ASSERT_EQ(span.len, 5, "field length");
    TEST_ASSERT(memcmp(span.start, "field", 5) == 0, "field content");

    return 1;
}

static int test_char_to_not_found(void)
{
    ln_span_t span;

    int r = ln_simd_char_to("no delimiter", 12, ':', &span);
    TEST_ASSERT_EQ(r, LN_SIMD_ENOTFOUND, "should return not found");

    return 1;
}

static int test_char_to_at_end(void)
{
    ln_span_t span;

    int r = ln_simd_char_to("field:", 6, ':', &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "delimiter at end");
    TEST_ASSERT_EQ(span.len, 5, "field is 'field'");

    return 1;
}

static int test_char_to_long(void)
{
    ln_span_t span;

    /* Delimiter past SIMD boundary */
    const char *buf = "abcdefghijklmnopqrstuvwxyz:value";
    int r = ln_simd_char_to(buf, strlen(buf), ':', &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "long char_to");
    TEST_ASSERT_EQ(span.len, 26, "26 chars before ':'");

    return 1;
}

/*============================================================================
 * string_to Tests
 *============================================================================*/

static int test_string_to_basic(void)
{
    ln_span_t span;

    int r = ln_simd_string_to("data]]more", 10, "]]", 2, &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "should succeed");
    TEST_ASSERT_EQ(span.len, 4, "data length");
    TEST_ASSERT(memcmp(span.start, "data", 4) == 0, "data content");

    return 1;
}

static int test_string_to_not_found(void)
{
    ln_span_t span;

    int r = ln_simd_string_to("no match here", 13, "]]", 2, &span);
    TEST_ASSERT_EQ(r, LN_SIMD_ENOTFOUND, "not found");

    return 1;
}

static int test_string_to_single_char_delim(void)
{
    ln_span_t span;

    int r = ln_simd_string_to("field:value", 11, ":", 1, &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "single char delim");
    TEST_ASSERT_EQ(span.len, 5, "field length");

    return 1;
}

/*============================================================================
 * number Tests
 *============================================================================*/

static int test_number_basic(void)
{
    ln_number_t num;

    int r = ln_simd_number("42", 2, &num);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "should succeed");
    TEST_ASSERT_EQ(num.value, 42, "value should be 42");

    r = ln_simd_number("-123", 4, &num);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "should succeed");
    TEST_ASSERT_EQ(num.value, -123, "value should be -123");

    return 1;
}

static int test_number_zero(void)
{
    ln_number_t num;

    int r = ln_simd_number("0", 1, &num);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse zero");
    TEST_ASSERT_EQ(num.value, 0, "value should be 0");

    return 1;
}

static int test_number_with_trailing(void)
{
    ln_number_t num;

    int r = ln_simd_number("12345abc", 8, &num);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "should succeed");
    TEST_ASSERT_EQ(num.value, 12345, "value should be 12345");
    TEST_ASSERT_EQ(num.consumed, 5, "consumed should be 5");

    return 1;
}

static int test_number_not_a_number(void)
{
    ln_number_t num;

    int r = ln_simd_number("abc", 3, &num);
    TEST_ASSERT_EQ(r, LN_SIMD_EFORMAT, "not a number");

    return 1;
}

static int test_number_positive_sign(void)
{
    ln_number_t num;

    int r = ln_simd_number("+42", 3, &num);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "positive sign");
    TEST_ASSERT_EQ(num.value, 42, "value should be 42");

    return 1;
}

/*============================================================================
 * unsigned Tests
 *============================================================================*/

static int test_unsigned_basic(void)
{
    ln_number_t num;

    int r = ln_simd_unsigned("42", 2, &num);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse unsigned");
    TEST_ASSERT_EQ(num.value, 42, "value should be 42");

    return 1;
}

static int test_unsigned_rejects_negative(void)
{
    ln_number_t num;

    int r = ln_simd_unsigned("-42", 3, &num);
    /* Should reject negative numbers */
    TEST_ASSERT(r != LN_SIMD_OK, "should reject negative");

    return 1;
}

/*============================================================================
 * hex Tests
 *============================================================================*/

static int test_hex_basic(void)
{
    ln_number_t num;

    int r = ln_simd_hex("0xFF", 4, &num);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse hex");
    TEST_ASSERT_EQ(num.value, 255, "value should be 255");

    return 1;
}

static int test_hex_upper(void)
{
    ln_number_t num;

    int r = ln_simd_hex("0XAB", 4, &num);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse uppercase hex prefix");
    TEST_ASSERT_EQ(num.value, 0xAB, "value should be 0xAB");

    return 1;
}

static int test_hex_with_trailing(void)
{
    ln_number_t num;

    int r = ln_simd_hex("0xDEADbeef rest", 15, &num);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "hex with trailing");
    TEST_ASSERT_EQ(num.value, (int64_t)0xDEADBEEF, "value should be 0xDEADBEEF");

    return 1;
}

/*============================================================================
 * ipv4 Tests
 *============================================================================*/

static int test_ipv4_basic(void)
{
    ln_ipv4_t ip;

    int r = ln_simd_ipv4("192.168.1.1", 11, &ip);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse IPv4");
    TEST_ASSERT(ip.valid, "should be valid");
    TEST_ASSERT_EQ(ip.octets[0], 192, "octet 0");
    TEST_ASSERT_EQ(ip.octets[1], 168, "octet 1");
    TEST_ASSERT_EQ(ip.octets[2], 1, "octet 2");
    TEST_ASSERT_EQ(ip.octets[3], 1, "octet 3");

    return 1;
}

static int test_ipv4_with_trailing(void)
{
    ln_ipv4_t ip;

    int r = ln_simd_ipv4("10.0.0.1:8080", 13, &ip);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse IPv4 with trailing");
    TEST_ASSERT_EQ(ip.consumed, 8, "consumed 8 bytes");

    return 1;
}

static int test_ipv4_all_zeros(void)
{
    ln_ipv4_t ip;

    int r = ln_simd_ipv4("0.0.0.0", 7, &ip);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse 0.0.0.0");

    return 1;
}

static int test_ipv4_max(void)
{
    ln_ipv4_t ip;

    int r = ln_simd_ipv4("255.255.255.255", 15, &ip);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse 255.255.255.255");

    return 1;
}

static int test_ipv4_invalid(void)
{
    ln_ipv4_t ip;

    /* Octet > 255 */
    int r = ln_simd_ipv4("256.1.1.1", 9, &ip);
    TEST_ASSERT(r != LN_SIMD_OK, "reject 256.x.x.x");

    /* Too short */
    r = ln_simd_ipv4("1.2.3", 5, &ip);
    TEST_ASSERT(r != LN_SIMD_OK, "reject incomplete IPv4");

    /* Not an IP */
    r = ln_simd_ipv4("hello", 5, &ip);
    TEST_ASSERT(r != LN_SIMD_OK, "reject non-IP");

    return 1;
}

/*============================================================================
 * quoted Tests
 *============================================================================*/

static int test_quoted_double(void)
{
    ln_span_t span;

    int r = ln_simd_quoted("\"hello world\"", 13, &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse double-quoted");
    TEST_ASSERT_EQ(span.len, 11, "content length");
    TEST_ASSERT(memcmp(span.start, "hello world", 11) == 0, "content");

    return 1;
}

static int test_quoted_single(void)
{
    ln_span_t span;

    int r = ln_simd_quoted("'hello'", 7, &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse single-quoted");
    TEST_ASSERT_EQ(span.len, 5, "content length");

    return 1;
}

static int test_quoted_with_escape(void)
{
    ln_span_t span;

    int r = ln_simd_quoted("\"hello\\\"world\"", 14, &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse with escaped quote");

    return 1;
}

static int test_quoted_empty(void)
{
    ln_span_t span;

    int r = ln_simd_quoted("\"\"", 2, &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse empty quoted");
    TEST_ASSERT_EQ(span.len, 0, "empty content");

    return 1;
}

static int test_quoted_unclosed(void)
{
    ln_span_t span;

    int r = ln_simd_quoted("\"unclosed", 9, &span);
    TEST_ASSERT_EQ(r, LN_SIMD_EFORMAT, "reject unclosed quote");

    return 1;
}

/*============================================================================
 * bracketed Tests
 *============================================================================*/

static int test_bracketed_basic(void)
{
    ln_span_t span;

    int r = ln_simd_bracketed("{hello}", 7, '{', '}', &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse bracketed");
    TEST_ASSERT_EQ(span.len, 5, "content length");

    return 1;
}

static int test_bracketed_nested(void)
{
    ln_span_t span;

    int r = ln_simd_bracketed("{a{b}c}", 7, '{', '}', &span);
    TEST_ASSERT_EQ(r, LN_SIMD_OK, "parse nested brackets");
    TEST_ASSERT_EQ(span.len, 5, "content includes nested");

    return 1;
}

static int test_bracketed_unclosed(void)
{
    ln_span_t span;

    int r = ln_simd_bracketed("{unclosed", 9, '{', '}', &span);
    TEST_ASSERT_EQ(r, LN_SIMD_EFORMAT, "reject unclosed bracket");

    return 1;
}

/*============================================================================
 * unescape Tests
 *============================================================================*/

static int test_unescape_basic(void)
{
    char buf[32];

    strcpy(buf, "hello\\nworld");
    size_t new_len = ln_simd_unescape(buf, strlen(buf));
    TEST_ASSERT_EQ(new_len, 11, "unescaped length");
    TEST_ASSERT_EQ(buf[5], '\n', "newline inserted");

    return 1;
}

static int test_unescape_backslash(void)
{
    char buf[32];

    strcpy(buf, "a\\\\b");
    size_t new_len = ln_simd_unescape(buf, strlen(buf));
    TEST_ASSERT_EQ(new_len, 3, "unescaped length");
    TEST_ASSERT_EQ(buf[1], '\\', "literal backslash");

    return 1;
}

static int test_unescape_tab(void)
{
    char buf[32];

    strcpy(buf, "a\\tb");
    size_t new_len = ln_simd_unescape(buf, strlen(buf));
    TEST_ASSERT_EQ(new_len, 3, "unescaped length");
    TEST_ASSERT_EQ(buf[1], '\t', "tab inserted");

    return 1;
}

static int test_unescape_no_escapes(void)
{
    char buf[32];

    strcpy(buf, "hello");
    size_t new_len = ln_simd_unescape(buf, 5);
    TEST_ASSERT_EQ(new_len, 5, "unchanged");
    TEST_ASSERT(memcmp(buf, "hello", 5) == 0, "content unchanged");

    return 1;
}

/*============================================================================
 * Span Utility Tests
 *============================================================================*/

static int test_span_eq(void)
{
    ln_span_t a = { .start = "hello", .len = 5 };
    ln_span_t b = { .start = "hello", .len = 5 };
    ln_span_t c = { .start = "world", .len = 5 };
    ln_span_t d = { .start = "hell",  .len = 4 };
    ln_span_t e = { .start = NULL,    .len = 0 };
    ln_span_t f = { .start = NULL,    .len = 0 };

    TEST_ASSERT(ln_span_eq(&a, &b), "equal spans");
    TEST_ASSERT(!ln_span_eq(&a, &c), "different content");
    TEST_ASSERT(!ln_span_eq(&a, &d), "different length");
    TEST_ASSERT(ln_span_eq(&e, &f), "both empty");

    return 1;
}

static int test_span_eq_str(void)
{
    ln_span_t a = { .start = "hello", .len = 5 };
    ln_span_t b = { .start = NULL,    .len = 0 };

    TEST_ASSERT(ln_span_eq_str(&a, "hello"), "match string");
    TEST_ASSERT(!ln_span_eq_str(&a, "world"), "no match");
    TEST_ASSERT(!ln_span_eq_str(&a, "hell"), "different length");
    TEST_ASSERT(ln_span_eq_str(&b, ""), "empty span matches empty string");

    return 1;
}

/*============================================================================
 * Character Classification Tests
 *============================================================================*/

static int test_char_classification(void)
{
    /* ln_is_space */
    TEST_ASSERT(ln_is_space(' '), "space is whitespace");
    TEST_ASSERT(ln_is_space('\t'), "tab is whitespace");
    TEST_ASSERT(ln_is_space('\n'), "newline is whitespace");
    TEST_ASSERT(ln_is_space('\r'), "CR is whitespace");
    TEST_ASSERT(!ln_is_space('a'), "'a' is not whitespace");

    /* ln_is_digit */
    TEST_ASSERT(ln_is_digit('0'), "'0' is digit");
    TEST_ASSERT(ln_is_digit('9'), "'9' is digit");
    TEST_ASSERT(!ln_is_digit('a'), "'a' is not digit");

    /* ln_is_alnum */
    TEST_ASSERT(ln_is_alnum('a'), "'a' is alnum");
    TEST_ASSERT(ln_is_alnum('Z'), "'Z' is alnum");
    TEST_ASSERT(ln_is_alnum('5'), "'5' is alnum");
    TEST_ASSERT(!ln_is_alnum('.'), "'.' is not alnum");

    return 1;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void)
{
    printf("=== ln_simd Test Suite ===\n\n");

    printf("Backend tests:\n");
    RUN_TEST(test_backend_info);
    printf("\n");

    printf("find_char tests:\n");
    RUN_TEST(test_find_char_basic);
    RUN_TEST(test_find_char_edge);
    RUN_TEST(test_find_char_long);
    printf("\n");

    printf("find_char_set tests:\n");
    RUN_TEST(test_find_char_set_basic);
    RUN_TEST(test_find_char_set_edge);
    RUN_TEST(test_find_char_set_long);
    printf("\n");

    printf("find_not_char_set tests:\n");
    RUN_TEST(test_find_not_char_set_basic);
    RUN_TEST(test_find_not_char_set_edge);
    RUN_TEST(test_embedded_nul_no_truncation);
    printf("\n");

    printf("skip_space tests:\n");
    RUN_TEST(test_skip_space);
    RUN_TEST(test_skip_space_long);
    printf("\n");

    printf("skip_chars tests:\n");
    RUN_TEST(test_skip_chars);
    printf("\n");

    printf("word tests:\n");
    RUN_TEST(test_word_basic);
    RUN_TEST(test_word_no_space);
    RUN_TEST(test_word_empty);
    RUN_TEST(test_word_single_char);
    RUN_TEST(test_word_long);
    printf("\n");

    printf("char_to tests:\n");
    RUN_TEST(test_char_to_basic);
    RUN_TEST(test_char_to_not_found);
    RUN_TEST(test_char_to_at_end);
    RUN_TEST(test_char_to_long);
    printf("\n");

    printf("string_to tests:\n");
    RUN_TEST(test_string_to_basic);
    RUN_TEST(test_string_to_not_found);
    RUN_TEST(test_string_to_single_char_delim);
    printf("\n");

    printf("number tests:\n");
    RUN_TEST(test_number_basic);
    RUN_TEST(test_number_zero);
    RUN_TEST(test_number_with_trailing);
    RUN_TEST(test_number_not_a_number);
    RUN_TEST(test_number_positive_sign);
    printf("\n");

    printf("unsigned tests:\n");
    RUN_TEST(test_unsigned_basic);
    RUN_TEST(test_unsigned_rejects_negative);
    printf("\n");

    printf("hex tests:\n");
    RUN_TEST(test_hex_basic);
    RUN_TEST(test_hex_upper);
    RUN_TEST(test_hex_with_trailing);
    printf("\n");

    printf("ipv4 tests:\n");
    RUN_TEST(test_ipv4_basic);
    RUN_TEST(test_ipv4_with_trailing);
    RUN_TEST(test_ipv4_all_zeros);
    RUN_TEST(test_ipv4_max);
    RUN_TEST(test_ipv4_invalid);
    printf("\n");

    printf("quoted tests:\n");
    RUN_TEST(test_quoted_double);
    RUN_TEST(test_quoted_single);
    RUN_TEST(test_quoted_with_escape);
    RUN_TEST(test_quoted_empty);
    RUN_TEST(test_quoted_unclosed);
    printf("\n");

    printf("bracketed tests:\n");
    RUN_TEST(test_bracketed_basic);
    RUN_TEST(test_bracketed_nested);
    RUN_TEST(test_bracketed_unclosed);
    printf("\n");

    printf("unescape tests:\n");
    RUN_TEST(test_unescape_basic);
    RUN_TEST(test_unescape_backslash);
    RUN_TEST(test_unescape_tab);
    RUN_TEST(test_unescape_no_escapes);
    printf("\n");

    printf("span utility tests:\n");
    RUN_TEST(test_span_eq);
    RUN_TEST(test_span_eq_str);
    printf("\n");

    printf("char classification tests:\n");
    RUN_TEST(test_char_classification);
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
