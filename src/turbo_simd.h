/**
 * @file turbo_simd.h
 * @brief SIMD-accelerated parsing primitives
 *//*
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_TURBO_SIMD_H_INCLUDED
#define	LIBLOGNORM_TURBO_SIMD_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Architecture Detection
 *============================================================================*/

#if defined(__x86_64__) || defined(_M_X64)
	#if defined(__SSE4_2__) || defined(__AVX2__)
		#define LN_SIMD_SSE42 1
		#include <nmmintrin.h>  /* SSE4.2 */
		#include <emmintrin.h>  /* SSE2 */
	#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
	#if defined(__ARM_NEON) || defined(__ARM_NEON__)
		#define LN_SIMD_NEON 1
		#include <arm_neon.h>
	#endif
#endif

/* SIMD register width */
#if defined(LN_SIMD_SSE42) || defined(LN_SIMD_NEON)
	#define LN_SIMD_WIDTH 16
#else
	#define LN_SIMD_WIDTH 1  /* Scalar fallback */
#endif

/*============================================================================
 * Return Codes
 *============================================================================*/

#define LN_SIMD_OK           0    /**< Success */
#define LN_SIMD_EINVAL      -1    /**< Invalid argument */
#define LN_SIMD_ENOTFOUND   -2    /**< Pattern not found */
#define LN_SIMD_EOVERFLOW   -3    /**< Numeric overflow */
#define LN_SIMD_EFORMAT     -4    /**< Invalid format */

/*============================================================================
 * Parse Result Structure
 *============================================================================*/

/**
 * @brief Result of a parsing operation.
 *
 * Contains the extracted value and how many bytes were consumed.
 */
typedef struct {
	const char *start;    /**< Start of matched region */
	size_t      len;      /**< Length of matched region */
	size_t      consumed; /**< Total bytes consumed (including delimiters) */
} ln_span_t;

/**
 * @brief Result of numeric parsing.
 */
typedef struct {
	int64_t value;        /**< Parsed value */
	size_t  consumed;     /**< Bytes consumed */
	bool    negative;     /**< Was negative */
	bool    overflow;     /**< Overflow occurred */
} ln_number_t;

/**
 * @brief Result of IPv4 parsing.
 */
typedef struct {
	uint32_t addr;        /**< IPv4 address in network byte order */
	uint8_t  octets[4];   /**< Individual octets */
	size_t   consumed;    /**< Bytes consumed */
	bool     valid;       /**< Was valid IPv4 */
} ln_ipv4_t;

/*============================================================================
 * Character Classification (Vectorized)
 *============================================================================*/

/**
 * @brief Check if character is whitespace.
 */
static inline bool
ln_is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/**
 * @brief Check if character is a digit.
 */
static inline bool
ln_is_digit(char c)
{
	return c >= '0' && c <= '9';
}

/**
 * @brief Check if character is alphanumeric.
 */
static inline bool
ln_is_alnum(char c)
{
	return (c >= '0' && c <= '9') ||
		   (c >= 'A' && c <= 'Z') ||
		   (c >= 'a' && c <= 'z');
}

/*============================================================================
 * Core SIMD Primitives
 *============================================================================*/

/**
 * @brief Find first occurrence of a character.
 *
 * SIMD-accelerated scan for a single character.
 *
 * @param[in] buf     Buffer to search
 * @param[in] len     Buffer length
 * @param[in] c       Character to find
 * @return Offset of character, or len if not found
 */
size_t ln_simd_find_char(const char *buf, size_t len, char c);

/**
 * @brief Find first occurrence of any character from a set.
 *
 * Searches for the first character that matches any in the set.
 * Set is specified as a null-terminated string of characters.
 *
 * @param[in] buf     Buffer to search
 * @param[in] len     Buffer length  
 * @param[in] chars   Null-terminated set of characters to find
 * @return Offset of first match, or len if not found
 *
 * Example:
 * @code
 *   // Find first whitespace or colon
 *   size_t pos = ln_simd_find_char_set(buf, len, " \t:");
 * @endcode
 */
size_t ln_simd_find_char_set(const char *buf, size_t len, const char *chars);

/**
 * @brief char-to over a SET of terminators.
 *
 * Same contract as ln_simd_char_to(), except the field ends at whichever
 * member of @p chars appears first. @p chars is a null-terminated set.
 *
 * @return LN_SIMD_OK when a terminator was found, LN_SIMD_ENOTFOUND otherwise.
 */
int ln_simd_char_to_set(const char *buf, size_t len, const char *chars, ln_span_t *span);

/**
 * @brief Find first character NOT in a set.
 *
 * Inverse of find_char_set - finds first character that doesn't match.
 *
 * @param[in] buf     Buffer to search
 * @param[in] len     Buffer length
 * @param[in] chars   Characters to skip
 * @return Offset of first non-matching char, or len if all match
 */
size_t ln_simd_find_not_char_set(const char *buf, size_t len, const char *chars);

/**
 * @brief Skip leading whitespace.
 *
 * @param[in] buf  Buffer
 * @param[in] len  Buffer length
 * @return Number of whitespace characters skipped
 */
size_t ln_simd_skip_space(const char *buf, size_t len);

/**
 * @brief Offset of the first whitespace byte, or len if none.
 *
 * Whitespace is the same class skip_space uses: HT..CR (0x09-0x0D) and SP.
 * Vectorized on SSE4.2 and NEON; the scalar path uses the same class.
 */
size_t ln_simd_find_space(const char *buf, size_t len);

/**
 * @brief Skip while characters match a set.
 *
 * @param[in] buf    Buffer
 * @param[in] len    Buffer length
 * @param[in] chars  Characters to skip
 * @return Number of characters skipped
 */
size_t ln_simd_skip_chars(const char *buf, size_t len, const char *chars);

/*============================================================================
 * Field Extraction Primitives
 *============================================================================*/

/**
 * @brief Extract a whitespace-delimited word.
 *
 * Extracts characters until whitespace or end of buffer.
 * Leading whitespace is skipped.
 *
 * @param[in]  buf   Buffer to parse
 * @param[in]  len   Buffer length
 * @param[out] span  Result span (start points into buf)
 * @return LN_SIMD_OK on success, LN_SIMD_ENOTFOUND if empty
 *
 * Example: "  hello world" -> span={start="hello", len=5, consumed=7}
 */
int ln_simd_word(const char *buf, size_t len, ln_span_t *span);

/**
 * @brief Extract characters until a delimiter.
 *
 * Extracts all characters until the delimiter is found.
 * The delimiter is NOT included in the span but IS consumed.
 *
 * @param[in]  buf    Buffer to parse
 * @param[in]  len    Buffer length
 * @param[in]  delim  Delimiter character
 * @param[out] span   Result span
 * @return LN_SIMD_OK on success, LN_SIMD_ENOTFOUND if delimiter not found
 *
 * Example: "field:value" with delim=':' -> span={start="field", len=5, consumed=6}
 */
int ln_simd_char_to(const char *buf, size_t len, char delim, ln_span_t *span);

/**
 * @brief Extract characters until a delimiter string.
 *
 * Like char_to but matches a multi-character delimiter.
 *
 * @param[in]  buf       Buffer to parse
 * @param[in]  len       Buffer length
 * @param[in]  delim     Delimiter string
 * @param[in]  delim_len Delimiter length
 * @param[out] span      Result span
 * @return LN_SIMD_OK on success, LN_SIMD_ENOTFOUND if delimiter not found
 *
 * Example: "data]]more" with delim="]]" -> span={start="data", len=4, consumed=6}
 */
int ln_simd_string_to(const char *buf, size_t len, 
					  const char *delim, size_t delim_len,
					  ln_span_t *span);

/**
 * @brief Extract a quoted string.
 *
 * Extracts content between matching quotes, handling escapes.
 * Supports both single and double quotes.
 * Escape sequences: \\ \" \' \n \r \t
 *
 * @param[in]  buf   Buffer to parse (should start with quote)
 * @param[in]  len   Buffer length
 * @param[out] span  Result span (content without quotes)
 * @return LN_SIMD_OK on success, LN_SIMD_EFORMAT if malformed
 *
 * Example: "\"hello\\nworld\"" -> span={start=<inside>, len=11, consumed=14}
 *
 * @note The span points into the original buffer. If escapes were present,
 *       caller must unescape separately if needed.
 */
int ln_simd_quoted(const char *buf, size_t len, ln_span_t *span);

/**
 * @brief Extract content between brackets/braces.
 *
 * Handles nested brackets of the same type.
 *
 * @param[in]  buf    Buffer (should start with opening bracket)
 * @param[in]  len    Buffer length
 * @param[in]  open   Opening bracket character
 * @param[in]  close  Closing bracket character
 * @param[out] span   Result span (content without brackets)
 * @return LN_SIMD_OK on success, LN_SIMD_EFORMAT if unbalanced
 */
int ln_simd_bracketed(const char *buf, size_t len,
					  char open, char close, ln_span_t *span);

/*============================================================================
 * Numeric Parsing
 *============================================================================*/

/**
 * @brief Parse an integer.
 *
 * Parses a decimal integer with optional sign.
 * Stops at first non-digit.
 *
 * @param[in]  buf     Buffer to parse
 * @param[in]  len     Buffer length
 * @param[out] result  Parse result
 * @return LN_SIMD_OK on success, LN_SIMD_EFORMAT if no digits
 *
 * Example: "-12345abc" -> result={value=-12345, consumed=6}
 */
int ln_simd_number(const char *buf, size_t len, ln_number_t *result);

/**
 * @brief Parse an unsigned integer.
 *
 * Like number() but rejects negative values.
 *
 * @param[in]  buf     Buffer to parse
 * @param[in]  len     Buffer length
 * @param[out] result  Parse result (value is always positive)
 * @return LN_SIMD_OK on success
 */
int ln_simd_unsigned(const char *buf, size_t len, ln_number_t *result);

/**
 * @brief Parse a hexadecimal integer.
 *
 * Parses hex with optional 0x/0X prefix.
 *
 * @param[in]  buf     Buffer to parse
 * @param[in]  len     Buffer length
 * @param[out] result  Parse result
 * @return LN_SIMD_OK on success
 */
int ln_simd_hex(const char *buf, size_t len, ln_number_t *result);

/*============================================================================
 * Network Address Parsing
 *============================================================================*/

/**
 * @brief Parse an IPv4 address.
 *
 * Parses dotted-decimal notation (e.g., "192.168.1.1").
 * Validates octet ranges (0-255).
 *
 * @param[in]  buf     Buffer to parse
 * @param[in]  len     Buffer length
 * @param[out] result  Parse result
 * @return LN_SIMD_OK on success, LN_SIMD_EFORMAT if invalid
 *
 * Example: "192.168.1.1:8080" -> result={addr=0xC0A80101, consumed=11}
 */
int ln_simd_ipv4(const char *buf, size_t len, ln_ipv4_t *result);

/**
 * @brief Parse an IPv4:port combination.
 *
 * @param[in]  buf     Buffer to parse
 * @param[in]  len     Buffer length
 * @param[out] ip      IP result
 * @param[out] port    Port number (0 if not present)
 * @return LN_SIMD_OK on success
 */
int ln_simd_ipv4_port(const char *buf, size_t len, 
					  ln_ipv4_t *ip, uint16_t *port);

/*============================================================================
 * Timestamp Parsing
 *============================================================================*/

/**
 * @brief Parse a syslog-style timestamp.
 *
 * Formats supported:
 * - RFC 3164: "Jan 15 10:30:45"
 * - RFC 5424: "2024-01-15T10:30:45.123Z"
 *
 * @param[in]  buf       Buffer to parse
 * @param[in]  len       Buffer length
 * @param[out] span      Span of timestamp
 * @param[out] epoch_ms  Unix epoch milliseconds (optional, may be NULL)
 * @return LN_SIMD_OK on success, LN_SIMD_EFORMAT if not recognized
 */
int ln_simd_timestamp(const char *buf, size_t len,
					  ln_span_t *span, int64_t *epoch_ms);

/**
 * @brief Parse a bare ISO calendar date: YYYY-MM-DD (10 bytes, no clock).
 *
 * Month 01-12, day 01-31. No calendar (leap-year) check; matches ISODate.
 * Uses a 16-byte SSE4.2/NEON layout check when 16 bytes are readable,
 * otherwise a 10-byte scalar scan. Never reads past @p len.
 *
 * @return LN_SIMD_OK on success, LN_SIMD_EFORMAT if not an ISO date
 */
int ln_simd_iso_date(const char *buf, size_t len, ln_span_t *span);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Unescape a string in-place.
 *
 * Processes escape sequences: \\ \" \' \n \r \t \xHH
 *
 * @param[in,out] buf  Buffer to unescape
 * @param[in]     len  Buffer length
 * @return New length after unescaping
 */
size_t ln_simd_unescape(char *buf, size_t len);

/**
 * @brief Compare two spans for equality.
 *
 * @param[in] a  First span
 * @param[in] b  Second span
 * @return true if equal, false otherwise
 */
static inline bool
ln_span_eq(const ln_span_t *a, const ln_span_t *b)
{
	if (a->len != b->len) return false;
	if (a->len == 0) return true;
	return __builtin_memcmp(a->start, b->start, a->len) == 0;
}

/**
 * Equal-length memory compare: 16-byte SIMD chunks, scalar tail.
 * Both pointers must be readable for @p n bytes. n == 0 is equal.
 */
static inline bool
ln_simd_memeq(const char *a, const char *b, size_t n)
{
	if (n == 0)
		return true;
#if defined(LN_SIMD_NEON)
	while (n >= 16) {
		uint8x16_t va = vld1q_u8((const uint8_t *)(const void *)a);
		uint8x16_t vb = vld1q_u8((const uint8_t *)(const void *)b);
		uint64x2_t eq = vreinterpretq_u64_u8(vceqq_u8(va, vb));
		if (~vgetq_lane_u64(eq, 0) | ~vgetq_lane_u64(eq, 1))
			return false;
		a += 16;
		b += 16;
		n -= 16;
	}
#elif defined(LN_SIMD_SSE42)
	while (n >= 16) {
		__m128i va = _mm_loadu_si128((const __m128i *)(const void *)a);
		__m128i vb = _mm_loadu_si128((const __m128i *)(const void *)b);
		if (_mm_movemask_epi8(_mm_cmpeq_epi8(va, vb)) != 0xFFFF)
			return false;
		a += 16;
		b += 16;
		n -= 16;
	}
#endif
	return __builtin_memcmp(a, b, n) == 0;
}

/**
 * @brief Compare span to a string literal.
 *
 * @param[in] span  Span to compare
 * @param[in] str   Null-terminated string
 * @return true if equal, false otherwise
 */
static inline bool
ln_span_eq_str(const ln_span_t *span, const char *str)
{
	size_t slen = __builtin_strlen(str);
	if (span->len != slen) return false;
	if (slen == 0) return true;
	return __builtin_memcmp(span->start, str, slen) == 0;
}

/*============================================================================
 * Runtime Feature Detection
 *============================================================================*/

/**
 * @brief Get SIMD backend name.
 *
 * @return "sse42", "neon", or "scalar"
 */
const char *ln_simd_backend_name(void);

/**
 * @brief Get SIMD register width in bytes.
 *
 * @return 16 for SSE/NEON, 1 for scalar
 */
static inline int ln_simd_width(void) { return LN_SIMD_WIDTH; }

/**
 * @brief Check if SIMD is available.
 */
static inline bool ln_simd_available(void) { return LN_SIMD_WIDTH > 1; }

#ifdef __cplusplus
}
#endif

#endif /* LIBLOGNORM_TURBO_SIMD_H_INCLUDED */
