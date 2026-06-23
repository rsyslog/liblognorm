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

/* Turbo availability is published by lognorm-features.h, which liblognorm
 * installs alongside this header and generates with LOGNORM_TURBO_SUPPORTED set
 * to match the build.  This is a public installed header, so it must NOT pull
 * in the private build-time config.h: doing so would leak liblognorm's private
 * PACKAGE and HAVE_xxx macros into a consumer, or shadow them with the
 * consumer's own config.h.  Availability comes solely from the installed
 * lognorm-features.h; liblognorm's own .c files include config.h (for
 * ENABLE_TURBO) before this header. */
#include "lognorm-features.h"

/* Opaque liblognorm context.  Forward-declared as an incomplete struct rather
 * than re-typedef'd to `ln_ctx`: liblognorm.h owns that typedef, and repeating
 * it here is ill-formed under strict C99 when both public headers are included.
 * Callers obtain a context from ln_initCtx() (liblognorm.h) and pass it here. */
struct ln_ctx_s;

/* size_t / int64_t appear in the public signatures (the real prototypes and the
 * no-op stubs alike), so the standard headers are needed no matter which branch
 * compiles.  Standard headers only -- never config.h (see the note above). */
#include <stddef.h>
#include <stdint.h>

#if defined(ENABLE_TURBO) || defined(LOGNORM_TURBO_SUPPORTED)

#ifdef __cplusplus
extern "C" {
#endif

/* (ln_ctx context type: see the struct ln_ctx_s forward-declaration above.) */

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
int ln_turbo_is_available(struct ln_ctx_s *ctx);

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
int ln_turbo_normalize_to_str(struct ln_ctx_s *ctx, const char *str, size_t strLen,
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
int ln_turbo_normalize_raw(struct ln_ctx_s *ctx, const char *str, size_t strLen,
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
								   ln_ftype_t *type, unsigned *flags,
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
ln_fast_result_snapshot_t *ln_turbo_snapshot_result(struct ln_ctx_s *ctx);

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
 * (runtime-disabled but still compiled) blocks degrade to error/empty values.
 *
 * Implemented as static inline functions, NOT function-like macros: a macro
 * that discards its arguments leaves the caller's locals unreferenced (tripping
 * -Wunused-variable in consumer builds) and type-checks nothing.  The inline
 * stubs use their parameters (cast to void) and mirror the real signatures, so
 * consumer code compiles identically whether or not TurboVM is present. */

/* Same incomplete struct types as the real branch above, so the opaque handles
 * are type-identical in both build configurations (a consumer cannot silently
 * cross the void* boundary). They are only ever held as pointers. */
typedef struct ln_fast_result_s          ln_fast_result_t;
typedef struct ln_fast_result_snapshot_s ln_fast_result_snapshot_t;

typedef enum {
	LN_FTYPE_NULL = 0,
	LN_FTYPE_STRING,
	LN_FTYPE_STRING_INLINE,
	LN_FTYPE_INT,
	LN_FTYPE_DOUBLE,
	LN_FTYPE_BOOL
} ln_ftype_t;

#define LN_FFIELD_NESTED 0x04

#ifdef __cplusplus
extern "C" {
#endif

static inline int
ln_turbo_is_available(struct ln_ctx_s *ctx)
{
	(void)ctx;
	return 0;
}

static inline int
ln_turbo_normalize_to_str(struct ln_ctx_s *ctx, const char *str, size_t strLen,
		char **json_str, size_t *json_len)
{
	(void)ctx; (void)str; (void)strLen; (void)json_str; (void)json_len;
	return -1;
}

static inline int
ln_turbo_normalize_raw(struct ln_ctx_s *ctx, const char *str, size_t strLen,
		const ln_fast_result_t **result)
{
	(void)ctx; (void)str; (void)strLen; (void)result;
	return -1;
}

static inline int
ln_fast_result_field_count(const ln_fast_result_t *r)
{
	(void)r;
	return 0;
}

static inline int
ln_fast_result_get_field(const ln_fast_result_t *r, int idx,
		const char **name, size_t *nlen, const char **value, size_t *vlen)
{
	(void)r; (void)idx; (void)name; (void)nlen; (void)value; (void)vlen;
	return -1;
}

static inline int
ln_fast_result_get_field_typed(const ln_fast_result_t *r, int idx,
		const char **name, size_t *nlen, ln_ftype_t *type, unsigned *flags,
		const char **sval, size_t *slen, int64_t *ival, double *dval)
{
	(void)r; (void)idx; (void)name; (void)nlen; (void)type; (void)flags;
	(void)sval; (void)slen; (void)ival; (void)dval;
	return -1;
}

static inline int
ln_fast_result_get_string(const ln_fast_result_t *r, const char *name,
		const char **value, size_t *vlen)
{
	(void)r; (void)name; (void)value; (void)vlen;
	return -1;
}

static inline int
ln_fast_result_get_int(const ln_fast_result_t *r, const char *name,
		int64_t *value)
{
	(void)r; (void)name; (void)value;
	return -1;
}

static inline int
ln_fast_result_tag_count(const ln_fast_result_t *r)
{
	(void)r;
	return 0;
}

static inline const char *
ln_fast_result_get_tag(const ln_fast_result_t *r, int idx)
{
	(void)r; (void)idx;
	return (const char *)0;
}

static inline int
ln_fast_result_has_tag(const ln_fast_result_t *r, const char *tag)
{
	(void)r; (void)tag;
	return 0;
}

static inline const char *
ln_fast_result_get_rule_id(const ln_fast_result_t *r)
{
	(void)r;
	return (const char *)0;
}

static inline ln_fast_result_snapshot_t *
ln_turbo_snapshot_result(struct ln_ctx_s *ctx)
{
	(void)ctx;
	return (ln_fast_result_snapshot_t *)0;
}

static inline const ln_fast_result_t *
ln_fast_result_snapshot_get(const ln_fast_result_snapshot_t *snap)
{
	(void)snap;
	return (const ln_fast_result_t *)0;
}

static inline void
ln_fast_result_snapshot_free(ln_fast_result_snapshot_t *snap)
{
	(void)snap;
}

#ifdef __cplusplus
}
#endif

#endif /* ENABLE_TURBO || LOGNORM_TURBO_SUPPORTED */

#endif /* LIBLOGNORM_LOGNORM_TURBO_H_INCLUDED */
