/**
 * @file lognorm-turbo.h
 * @brief Public API for the TurboVM bytecode normalization engine.
 *
 * This is the ONLY Turbo header that liblognorm installs (alongside
 * liblognorm.h and lognorm-features.h). It exposes an opaque,
 * function-level contract: no internal struct layouts cross the library
 * boundary, so the fast-result/snapshot representation, inline buffer
 * sizes and field limits remain free to change without breaking the ABI.
 *
 * Availability is gated on LOGNORM_TURBO_SUPPORTED (from lognorm-features.h)
 * for external consumers, and on ENABLE_TURBO when building liblognorm
 * itself. When neither is defined, the symbols degrade to no-op stubs so
 * consumers can compile unconditionally.
 *
 * The internal headers (turbo.h, turbo_result_fast.h, turbo_snapshot.h, ...)
 * remain private and are not installed.
 *//*
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_LOGNORM_TURBO_H_INCLUDED
#define	LIBLOGNORM_LOGNORM_TURBO_H_INCLUDED

/* When building liblognorm itself, config.h provides ENABLE_TURBO.
 * External consumers pull the installed feature macros instead. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "lognorm-features.h"
#endif

#if defined(ENABLE_TURBO) || defined(LOGNORM_TURBO_SUPPORTED)

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of the liblognorm context (also typedef'd, identically,
 * in liblognorm.h — a redundant typedef is permitted and harmless). */
typedef struct ln_ctx_s *ln_ctx;

/*============================================================================
 * Opaque types
 *
 * Declared as incomplete types: callers only ever hold pointers. The full
 * definitions live in the private headers and never reach installed code.
 *============================================================================*/

typedef struct ln_fast_result_s          ln_fast_result_t;
typedef struct ln_fast_result_snapshot_s ln_fast_result_snapshot_t;

/*============================================================================
 * Field value types
 *
 * Returned via ln_fast_result_get_field_typed(). The enum is part of the
 * public contract; the numeric values are stable.
 *============================================================================*/

typedef enum {
	LN_FTYPE_NULL = 0,
	LN_FTYPE_STRING,        /**< String value (ptr + len)        */
	LN_FTYPE_STRING_INLINE, /**< String value (inline-stored)    */
	LN_FTYPE_INT,           /**< Signed 64-bit integer           */
	LN_FTYPE_DOUBLE,        /**< Double-precision float          */
	LN_FTYPE_BOOL           /**< Boolean (reported as int 0/1)   */
} ln_ftype_t;

/** Field flag: the field name is dotted and denotes a nested object
 *  (e.g. "a.b" -> {"a":{"b":...}}). Other flag bits are private. */
#define LN_FFIELD_NESTED 0x04

/*============================================================================
 * Availability
 *============================================================================*/

/**
 * Check whether a compiled TurboVM program is available for this context.
 * @return non-zero if turbo normalization can be used, 0 otherwise.
 */
int ln_turbo_is_available(ln_ctx ctx);

/*============================================================================
 * Normalization
 *============================================================================*/

/**
 * Normalize using the TurboVM and emit a JSON string.
 *
 * @param ctx       liblognorm context
 * @param str       input to normalize
 * @param strLen    length of @p str
 * @param json_str  receives a newly allocated JSON string (caller frees)
 * @param json_len  receives the JSON string length
 * @return 0 on match, negative on no-match/error.
 */
int ln_turbo_normalize_to_str(ln_ctx ctx, const char *str, size_t strLen,
							  char **json_str, size_t *json_len);

/**
 * Normalize using the TurboVM with direct (zero-JSON) result access.
 *
 * The result is owned by the turbo context and remains valid only until the
 * next normalize call on the same context. Do not free it. To retain it
 * across calls, take a snapshot (see below).
 *
 * @param ctx     liblognorm context
 * @param str     input to normalize
 * @param strLen  length of @p str
 * @param result  receives a pointer to the (opaque) result
 * @return 0 on match, negative on no-match/error.
 */
int ln_turbo_normalize_raw(ln_ctx ctx, const char *str, size_t strLen,
						   const ln_fast_result_t **result);

/*============================================================================
 * Result accessors
 *============================================================================*/

/** Number of fields in the result. */
int ln_fast_result_field_count(const ln_fast_result_t *r);

/**
 * Get a field by index as a string.
 *
 * Non-string fields yield a NULL value with zero length; use
 * ln_fast_result_get_field_typed() to read their typed value.
 *
 * @return 0 on success, -1 if @p idx is out of range.
 */
int ln_fast_result_get_field(const ln_fast_result_t *r, int idx,
							 const char **name, size_t *nlen,
							 const char **value, size_t *vlen);

/**
 * Get a field by index, preserving its value type.
 *
 * Lets callers build correctly-typed output (JSON numbers/booleans, nested
 * objects) without peeking at the internal result layout. Exactly one value
 * out-param is populated, selected by @p type:
 *   - LN_FTYPE_STRING / LN_FTYPE_STRING_INLINE -> @p sval / @p slen
 *   - LN_FTYPE_INT / LN_FTYPE_BOOL             -> @p ival (BOOL is 0 or 1)
 *   - LN_FTYPE_DOUBLE                          -> @p dval
 *   - LN_FTYPE_NULL                            -> none
 *
 * @p flags receives the public field flags (test against LN_FFIELD_NESTED).
 * Any out-param may be NULL if the caller is not interested in it.
 *
 * @return 0 on success, -1 if @p idx is out of range.
 */
int ln_fast_result_get_field_typed(const ln_fast_result_t *r, int idx,
								   const char **name, size_t *nlen,
								   unsigned *type, unsigned *flags,
								   const char **sval, size_t *slen,
								   int64_t *ival, double *dval);

/** Find a string field by name. @return 0 if found, -1 otherwise. */
int ln_fast_result_get_string(const ln_fast_result_t *r, const char *name,
							  const char **value, size_t *vlen);

/** Find an integer field by name. @return 0 if found, -1 otherwise. */
int ln_fast_result_get_int(const ln_fast_result_t *r, const char *name,
						   int64_t *value);

/** Number of tags in the result. */
int ln_fast_result_tag_count(const ln_fast_result_t *r);

/** Get a tag by index, or NULL if @p idx is out of range. */
const char *ln_fast_result_get_tag(const ln_fast_result_t *r, int idx);

/** @return 1 if @p tag is present, 0 otherwise. */
int ln_fast_result_has_tag(const ln_fast_result_t *r, const char *tag);

/** Matched rule ID, or NULL if there was no match. */
const char *ln_fast_result_get_rule_id(const ln_fast_result_t *r);

/*============================================================================
 * Snapshots (retain a result beyond the next normalize call)
 *============================================================================*/

/**
 * Snapshot the current turbo result into a self-contained, caller-owned copy.
 *
 * Must be called after a successful ln_turbo_normalize_raw() and before the
 * next normalize call on the same context.
 *
 * @return snapshot (free with ln_fast_result_snapshot_free), or NULL on error.
 */
ln_fast_result_snapshot_t *ln_turbo_snapshot_result(ln_ctx ctx);

/**
 * Get the result held by a snapshot. Valid for the snapshot's lifetime and
 * usable with every ln_fast_result_* accessor above.
 */
const ln_fast_result_t *
ln_fast_result_snapshot_get(const ln_fast_result_snapshot_t *snap);

/** Free a snapshot (single free, NULL-safe). */
void ln_fast_result_snapshot_free(ln_fast_result_snapshot_t *snap);

#ifdef __cplusplus
}
#endif

#else /* !ENABLE_TURBO && !LOGNORM_TURBO_SUPPORTED */

/* No-op stubs mirroring the full public surface, so consumers can compile and
 * link unconditionally even when TurboVM is not built. Calls in conditional
 * (runtime-disabled but still compiled) blocks degrade to error/empty values. */
typedef struct ln_ctx_s *ln_ctx;
typedef void *ln_fast_result_t;
typedef void *ln_fast_result_snapshot_t;

typedef enum {
	LN_FTYPE_NULL = 0,
	LN_FTYPE_STRING,
	LN_FTYPE_STRING_INLINE,
	LN_FTYPE_INT,
	LN_FTYPE_DOUBLE,
	LN_FTYPE_BOOL
} ln_ftype_t;

#define LN_FFIELD_NESTED 0x04

#define ln_turbo_is_available(ctx)                        (0)
#define ln_turbo_normalize_to_str(ctx, str, len, js, jl)  (-1)
#define ln_turbo_normalize_raw(ctx, str, len, r)          (-1)
#define ln_fast_result_field_count(r)                     (0)
#define ln_fast_result_get_field(r, idx, name, nlen, val, vlen)  (-1)
#define ln_fast_result_get_field_typed(r, idx, name, nlen, type, flags, sval, slen, ival, dval)  (-1)
#define ln_fast_result_get_string(r, name, val, vlen)     (-1)
#define ln_fast_result_get_int(r, name, val)              (-1)
#define ln_fast_result_tag_count(r)                       (0)
#define ln_fast_result_get_tag(r, idx)                    ((const char *)0)
#define ln_fast_result_has_tag(r, tag)                    (0)
#define ln_fast_result_get_rule_id(r)                     ((const char *)0)
#define ln_turbo_snapshot_result(ctx)                     ((void *)0)
#define ln_fast_result_snapshot_get(snap)                 ((void *)0)
#define ln_fast_result_snapshot_free(snap)                ((void)(snap))

#endif /* ENABLE_TURBO || LOGNORM_TURBO_SUPPORTED */

#endif /* LIBLOGNORM_LOGNORM_TURBO_H_INCLUDED */
