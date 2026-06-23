/*
 * turbo_json_impl.c -- Ultra-fast JSON serialization with nested object support
 *
 * Part of the TurboVM bytecode engine for high-performance log parsing.
 *
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * This file is part of liblognorm.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * A copy of the LGPL v2.1 can be found in the file "COPYING" in this distribution.
 */
#include "config.h"
#include "turbo_json.h"
#include "turbo_json_fast.h"
#include "turbo_result_fast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* JSON escaping needs only SSE2/NEON, not SSE4.2 — separate from LN_SIMD_* in simd.h */
#ifdef __ARM_NEON
#include <arm_neon.h>
#define HAS_SIMD 1
#elif defined(__SSE2__)
#include <emmintrin.h>
#define HAS_SIMD 1
#else
#define HAS_SIMD 0
#endif

/*============================================================================
 * Fast Integer Formatting
 *============================================================================*/

static const char digit_pairs[201] =
	"00010203040506070809"
	"10111213141516171819"
	"20212223242526272829"
	"30313233343536373839"
	"40414243444546474849"
	"50515253545556575859"
	"60616263646566676869"
	"70717273747576777879"
	"80818283848586878889"
	"90919293949596979899";

static inline int
fast_i64_to_str(int64_t val, char *buf)
{
	char tmp[21];
	char *p = tmp + 20;
	int neg = 0;
	int len;
	uint64_t uval;

	*p = '\0';

	if (val < 0) {
		neg = 1;
		uval = ~(uint64_t)val + 1u;
	} else {
		uval = (uint64_t)val;
	}

	while (uval >= 100) {
		unsigned idx = (uval % 100) * 2;
		uval /= 100;
		*--p = digit_pairs[idx + 1];
		*--p = digit_pairs[idx];
	}

	if (uval >= 10) {
		unsigned idx = (unsigned)uval * 2;
		*--p = digit_pairs[idx + 1];
		*--p = digit_pairs[idx];
	} else {
		*--p = '0' + (char)uval;
	}

	if (neg) *--p = '-';

	len = (int)(tmp + 20 - p);
	memcpy(buf, p, len);
	return len;
}

/*============================================================================
 * Fast String Escaping
 *============================================================================*/

/* Lookup table: 1 if char needs escaping */
static const uint8_t needs_escape[256] = {
	/* 0x00-0x1F: control chars need escaping */
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	/* 0x20-0x7F: printable, except " and \ */
	0,0,1,0,0,0,0,0, 0,0,0,0,0,0,0,0,  /* " at 0x22 */
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,1,0,0,0,  /* \ at 0x5C */
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	/* 0x80-0xFF: UTF-8 continuation, pass through */
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
};

/**
 * @brief Check if string needs escaping (fast path).
 *
 * Returns position of first char needing escape, or len if clean.
 */
static inline size_t
find_escape_needed(const char *s, size_t len)
{
#if HAS_SIMD && defined(__ARM_NEON)
	/* NEON: check 16 bytes at a time */
	size_t i = 0;

	/* Characters that need escaping: < 0x20, 0x22 ("), 0x5C (\) */
	uint8x16_t v_space = vdupq_n_u8(0x20);
	uint8x16_t v_quote = vdupq_n_u8('"');
	uint8x16_t v_bslash = vdupq_n_u8('\\');

	while (i + 16 <= len) {
		uint8x16_t v = vld1q_u8((const uint8_t *)(s + i));

		/* Check for control chars (< 0x20) */
		uint8x16_t ctrl = vcltq_u8(v, v_space);
		/* Check for quote */
		uint8x16_t quote = vceqq_u8(v, v_quote);
		/* Check for backslash */
		uint8x16_t bslash = vceqq_u8(v, v_bslash);

		/* Combine */
		uint8x16_t any = vorrq_u8(vorrq_u8(ctrl, quote), bslash);

		/* Check if any byte set */
		if (vmaxvq_u8(any) != 0) {
			/* Find first set byte */
			for (size_t j = 0; j < 16; j++) {
				if (needs_escape[(uint8_t)s[i + j]]) {
					return i + j;
				}
			}
		}
		i += 16;
	}

	/* Scalar tail */
	for (; i < len; i++) {
		if (needs_escape[(uint8_t)s[i]]) return i;
	}
	return len;

#elif HAS_SIMD && defined(__SSE2__)
	/* SSE2: similar approach */
	size_t i = 0;
	__m128i v_space = _mm_set1_epi8(0x20);
	__m128i v_quote = _mm_set1_epi8('"');
	__m128i v_bslash = _mm_set1_epi8('\\');

	while (i + 16 <= len) {
		/* _mm_loadu_si128 is the unaligned load intrinsic
		 *  — it explicitly does NOT require 16-byte alignment.
		 * The cast to const __m128i * is just how Intel's intrinsic API is designed;
		 * clang's -Wcast-align doesn't understand this semantic.
		 * So we cast through const void * to break the alignment chain */
		__m128i v = _mm_loadu_si128((const __m128i *)(const void *)(s + i));

		/* Check for control chars */
		__m128i ctrl = _mm_cmplt_epi8(v, v_space);
		/* Check for quote and backslash */
		__m128i quote = _mm_cmpeq_epi8(v, v_quote);
		__m128i bslash = _mm_cmpeq_epi8(v, v_bslash);

		/* Combine and get mask */
		__m128i any = _mm_or_si128(_mm_or_si128(ctrl, quote), bslash);
		int mask = _mm_movemask_epi8(any);

		if (mask) {
			return i + __builtin_ctz(mask);
		}
		i += 16;
	}

	/* Scalar tail */
	for (; i < len; i++) {
		if (needs_escape[(uint8_t)s[i]]) return i;
	}
	return len;

#else
	/* Scalar fallback */
	for (size_t i = 0; i < len; i++) {
		if (needs_escape[(uint8_t)s[i]]) return i;
	}
	return len;
#endif
}

/**
 * @brief Write escaped string to buffer.
 *
 * @return Number of bytes written, or -1 if overflow
 */
static inline int
write_escaped_string(const char *s, size_t len, char *out, size_t outlen)
{
	char *p = out;
	char *end = out + outlen;
	size_t i;
	size_t clean_start, clean_end, clean_len;
	uint8_t c;

	if (p >= end) return -1;
	*p++ = '"';

	i = 0;
	while (i < len) {
		/* Find run of clean chars */
		clean_start = i;
		clean_end = find_escape_needed(s + i, len - i) + i;

		/* Copy clean run */
		clean_len = clean_end - clean_start;
		if (clean_len > 0) {
			if (p + clean_len >= end) return -1;
			memcpy(p, s + clean_start, clean_len);
			p += clean_len;
			i = clean_end;
		}

		/* Handle escape if needed */
		if (i < len) {
			if (p + 6 >= end) return -1;  /* Max escape is \uXXXX */

			c = (uint8_t)s[i];
			switch (c) {
			case '"':  *p++ = '\\'; *p++ = '"'; break;
			case '\\': *p++ = '\\'; *p++ = '\\'; break;
			case '\b': *p++ = '\\'; *p++ = 'b'; break;
			case '\f': *p++ = '\\'; *p++ = 'f'; break;
			case '\n': *p++ = '\\'; *p++ = 'n'; break;
			case '\r': *p++ = '\\'; *p++ = 'r'; break;
			case '\t': *p++ = '\\'; *p++ = 't'; break;
			default: {
				/* \uXXXX for other control chars */
				int esc_n = snprintf(p, (size_t)(end - p), "\\u%04x", c);
				if (esc_n < 0 || p + esc_n >= end) return -1;
				p += esc_n;
				break;
			}
			}
			i++;
		}
	}

	if (p >= end) return -1;
	*p++ = '"';
	return (int)(p - out);
}

/*============================================================================
 * Multi-Depth Nested Object Serialization
 *============================================================================*/

/**
 * Maximum nesting depth for ECS-style dotted field names.
 * ECS rarely exceeds 3 levels (e.g., user.group.name).
 * 8 levels provides generous headroom with minimal stack cost.
 */
#define LN_JSON_MAX_DEPTH 8
_Static_assert(LN_JSON_MAX_DEPTH <= 8,
	"level_has_entry bitmask requires LN_JSON_MAX_DEPTH <= 8");

/**
 * Path component for tracking open JSON objects.
 * Each level stores the start offset and length of the component
 * within the original field name string.
 */
typedef struct {
	const char *start;   /**< Pointer into field name string */
	uint16_t    len;     /**< Length of this path component */
} ln_json_path_comp_t;

/**
 * @brief Parse a dotted field name into path components.
 *
 * "user.group.name" → ["user", "group", "name"]
 * "source.ip"       → ["source", "ip"]
 * "message"         → ["message"]
 *
 * @return Number of components (1 = no dots, >1 = nested)
 */
static inline int
parse_path(const char *name, uint16_t name_len,
		   ln_json_path_comp_t *comps, int max_comps)
{
	int n = 0;
	const char *p = name;
	const char *end = name + name_len;

	while (p < end && n < max_comps) {
		const char *dot = p;
		while (dot < end && *dot != '.') dot++;

		comps[n].start = p;
		comps[n].len = (uint16_t)(dot - p);
		n++;

		p = dot + 1;  /* skip the dot (or past end) */
	}
	return n;
}

/**
 * @brief Compare two path components for equality.
 */
static inline int
path_comp_eq(const ln_json_path_comp_t *a, const ln_json_path_comp_t *b)
{
	return a->len == b->len && memcmp(a->start, b->start, a->len) == 0;
}

/**
 * @brief Insertion sort on field index array by field name.
 *
 * Insertion sort is optimal for N ≤ 64: no function call overhead,
 * no recursion, excellent cache behavior on small arrays.
 * Sorting indices (uint8_t) avoids moving 64-byte field structs.
 */
static inline void
sort_field_indices(const ln_fast_result_t *r, uint8_t *idx, uint8_t n)
{
	/* Initialize identity mapping */
	for (uint8_t i = 0; i < n; i++) idx[i] = i;

	/* Insertion sort by field name (strcmp order) */
	for (uint8_t i = 1; i < n; i++) {
		uint8_t key = idx[i];
		const char *key_name = r->fields[key].name;
		int j = (int)i - 1;

		while (j >= 0 && strcmp(r->fields[idx[j]].name, key_name) > 0) {
			idx[j + 1] = idx[j];
			j--;
		}
		idx[j + 1] = key;
	}
}

/*============================================================================
 * Main Serialization
 *============================================================================*/

/**
 * @brief Write a single field value.
 */
static inline int
write_field_value(const ln_fast_field_t *f, char *out, size_t outlen)
{
	char *p = out;
	char *end = out + outlen;
	int n;

	switch (f->type) {
	case LN_FTYPE_STRING:
		n = write_escaped_string(f->v.str.ptr, f->v.str.len, p, end - p);
		if (n < 0) return -1;
		return n;

	case LN_FTYPE_STRING_INLINE: {
		size_t len = strlen(f->v.inl);
		n = write_escaped_string(f->v.inl, len, p, end - p);
		if (n < 0) return -1;
		return n;
	}

	case LN_FTYPE_INT:
		if (p + 21 >= end) return -1;
		return fast_i64_to_str(f->v.i, p);

	case LN_FTYPE_DOUBLE:
		if (p + 32 >= end) return -1;
		n = snprintf(p, end - p, "%.2f", f->v.d);
		if (n < 0 || n >= (int)(end - p)) return -1;
		return n;

	case LN_FTYPE_BOOL:
		if (f->v.b) {
			if (p + 4 >= end) return -1;
			memcpy(p, "true", 4);
			return 4;
		} else {
			if (p + 5 >= end) return -1;
			memcpy(p, "false", 5);
			return 5;
		}

	case LN_FTYPE_NULL:
	default:
		if (p + 4 >= end) return -1;
		memcpy(p, "null", 4);
		return 4;
	}
}

size_t
ln_fast_json_estimate(const ln_fast_result_t *r)
{
	size_t est = 2;  /* {} */

	for (uint8_t i = 0; i < r->n_fields; i++) {
		const ln_fast_field_t *f = &r->fields[i];
		if (!f->name) continue;

		/* Each dot in name generates an extra object: "key":{ ... }
		 * Worst-case JSON escaping expands ONE byte to six ("\uXXXX"),
		 * and each dot expands to a nested-object wrapper. Budget
		 * name_len * 6 (full \uXXXX expansion) + 8 structural slack
		 * (quotes, colon, brace) so the buffer is always large enough. */
		est += f->name_len * 6 + 8;

		switch (f->type) {
		case LN_FTYPE_STRING:
			/* Worst case: every byte -> "\uXXXX" (6) + 2 quotes */
			est += f->v.str.len * 6 + 2;
			break;
		case LN_FTYPE_STRING_INLINE:
			est += LN_FAST_INLINE_SIZE * 6 + 2;
			break;
		case LN_FTYPE_INT:
			est += 21;
			break;
		case LN_FTYPE_DOUBLE:
			est += 32;
			break;
		default:
			est += 6;
		}
		est += 1;  /* comma */
	}

	/* Tags array */
	if (r->n_tags > 0) {
		est += 20;  /* "event":{"tags":[ or "tags":[ + closing */
		for (uint8_t i = 0; i < r->n_tags; i++) {
			if (r->tags[i].tag)
				/* Tags also go through write_escaped_string:
				 * every byte may expand to "\uXXXX" (6) + quotes/comma. */
				est += strlen(r->tags[i].tag) * 6 + 3;
		}
	}

	return est + 64;  /* Safety margin */
}

/**
 * @brief Serialize fast result to nested JSON with multi-depth support.
 *
 * Algorithm:
 * 1. Sort field indices by name (insertion sort, O(N²) optimal for N≤64)
 * 2. Walk sorted fields with a path component stack
 * 3. At each field, compare its path to the currently-open path:
 *    - Find the divergence point
 *    - Close objects from current depth down to divergence
 *    - Open objects from divergence up to new depth - 1
 *    - Emit the leaf key:value
 *
 * Example with fields [source.ip, source.port, user.group.name, user.name]:
 *   - source.ip:    open "source", emit "ip"
 *   - source.port:  same prefix, emit "port"
 *   - user.group.name: close "source", open "user", open "group", emit "name"
 *   - user.name:    close "group", emit "name"
 *   → {"source":{"ip":"...","port":443},"user":{"group":{"name":"..."},"name":"..."}}
 *
 * Tags are emitted under "event.tags" as a JSON array, nested under "event".
 */
int
ln_fast_to_json(const ln_fast_result_t *r,
				char *buf, size_t buflen, size_t *outlen)
{
	char *p = buf;
	char *end = buf + buflen;
	int n;
	uint8_t sorted[LN_FAST_MAX_FIELDS];
	uint8_t n_valid = 0;
	ln_json_path_comp_t open_path[LN_JSON_MAX_DEPTH];
	int open_depth = 0;     /* Number of open nested objects */
	uint8_t fi;
	/*
	 * Per-level comma tracking.
	 * When we close a level and return to a parent, we need to know
	 * whether a comma is needed at that parent level. We use a bitmask:
	 * bit i is set if level i has already emitted at least one entry.
	 */
	uint8_t level_has_entry = 0;  /* bitmask, bit 0 = root level */
	uint8_t si;

	if (buflen < 3) return -1;

	/* Sort field indices by name for proper nesting grouping */
	for (fi = 0; fi < r->n_fields; fi++) {
		if (r->fields[fi].name && r->fields[fi].name_len > 0)
			sorted[n_valid++] = fi;
	}
	if (n_valid > 1)
		sort_field_indices(r, sorted, n_valid);

	*p++ = '{';

#define NEED_COMMA_AT(lvl)  (level_has_entry & (1u << (lvl)))
#define SET_HAS_ENTRY(lvl)  (level_has_entry |= (1u << (lvl)))
#define CLEAR_ENTRY_FROM(lvl) (level_has_entry &= (uint8_t)((1u << (lvl)) - 1))

	for (si = 0; si < n_valid; si++) {
		const ln_fast_field_t *f = &r->fields[sorted[si]];
		const ln_json_path_comp_t *leaf;
		ln_json_path_comp_t comps[LN_JSON_MAX_DEPTH];
		int n_comps, leaf_idx, new_depth, common, min_depth, d;

		/* Parse this field's path: "user.group.name" → [user, group, name] */
		n_comps = parse_path(f->name, f->name_len, comps, LN_JSON_MAX_DEPTH);
		leaf_idx = n_comps - 1;   /* Last component is the leaf key */
		new_depth = n_comps - 1;  /* Object nesting depth (0 = top-level) */

		/* Find divergence point between open_path and this field's path.
		 * We only compare object-level components (indices 0..new_depth-1). */
		common = 0;
		min_depth = open_depth < new_depth ? open_depth : new_depth;
		while (common < min_depth &&
			   path_comp_eq(&open_path[common], &comps[common])) {
			common++;
		}

		/* Close objects from open_depth down to common */
		for (d = open_depth - 1; d >= common; d--) {
			if (p >= end) return -1;
			*p++ = '}';
		}
		/* After closing, we're back at depth=common.
		 * Clear comma state for levels we just closed. */
		CLEAR_ENTRY_FROM(common + 1);
		open_depth = common;

		/* Open objects from common up to new_depth */
		for (d = common; d < new_depth; d++) {
			/* Comma at current level? */
			if (NEED_COMMA_AT(d)) {
				if (p >= end) return -1;
				*p++ = ',';
			}
			SET_HAS_ENTRY(d);

			/* Write object key: "component":{ */
			if ((size_t)(end - p) < (size_t)comps[d].len + 4) return -1;
			*p++ = '"';
			memcpy(p, comps[d].start, comps[d].len);
			p += comps[d].len;
			*p++ = '"';
			*p++ = ':';
			*p++ = '{';

			open_path[d] = comps[d];
		}
		open_depth = new_depth;

		/* Emit comma at leaf level if needed */
		if (NEED_COMMA_AT(open_depth)) {
			if (p >= end) return -1;
			*p++ = ',';
		}
		SET_HAS_ENTRY(open_depth);

		/* Write leaf key */
		leaf = &comps[leaf_idx];
		if ((size_t)(end - p) < (size_t)leaf->len + 3) return -1;
		*p++ = '"';
		memcpy(p, leaf->start, leaf->len);
		p += leaf->len;
		*p++ = '"';
		*p++ = ':';

		/* Write value */
		n = write_field_value(f, p, end - p);
		if (n < 0) return -1;
		p += n;
	}

	/* Close all remaining open objects */
	{
		int d;
		for (d = open_depth - 1; d >= 0; d--) {
			if (p >= end) return -1;
			*p++ = '}';
		}
	}
	open_depth = 0;

	/* Tags array — emit as "tags":[...] at root level (ECS standard) */
	if (r->n_tags > 0) {
		int tag_first = 1;
		uint8_t ti;
		size_t tlen;

		if (NEED_COMMA_AT(0)) {
			if (p >= end) return -1;
			*p++ = ',';
		}

		if ((size_t)(end - p) < 9) return -1;
		memcpy(p, "\"tags\":[", 8);
		p += 8;

		for (ti = 0; ti < r->n_tags; ti++) {
			if (!r->tags[ti].tag) continue;
			if (!tag_first) { if (p >= end) return -1; *p++ = ','; }
			tag_first = 0;

			tlen = strlen(r->tags[ti].tag);
			n = write_escaped_string(r->tags[ti].tag, tlen, p, end - p);
			if (n < 0) return -1;
			p += n;
		}

		if (p >= end) return -1;
		*p++ = ']';
	}

	if (p >= end) return -1;
	*p++ = '}';

	/* Null-terminate */
	if (p >= end) return -1;
	*p = '\0';

	if (outlen) *outlen = p - buf;
	return 0;

#undef NEED_COMMA_AT
#undef SET_HAS_ENTRY
#undef CLEAR_ENTRY_FROM
}

int
ln_fast_to_json_alloc(const ln_fast_result_t *r,
					  char **json_str, size_t *json_len)
{
	size_t est = ln_fast_json_estimate(r);
	char *buf;
	size_t len;

	buf = malloc(est);
	if (!buf) return -1;

	if (ln_fast_to_json(r, buf, est, &len) != 0) {
		free(buf);
		return -1;
	}

	*json_str = buf;
	if (json_len) *json_len = len;
	return 0;
}
