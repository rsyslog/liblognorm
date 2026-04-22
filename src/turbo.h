/**
 * @file turbo.h
 * @brief liblognorm integration for TurboVM bytecode engine
 *//*
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_TURBO_H_INCLUDED
#define	LIBLOGNORM_TURBO_H_INCLUDED

/* When building liblognorm itself, config.h provides ENABLE_TURBO.
 * When included from external projects (e.g. rsyslog), the consumer
 * should define LOGNORM_TURBO_SUPPORTED (from lognorm-features.h). */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if defined(ENABLE_TURBO) || defined(LOGNORM_TURBO_SUPPORTED)

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct ln_ctx_s *ln_ctx;
struct json_object;  /* libfastjson (for legacy API only) */

/*============================================================================
 * Turbo VM State (opaque)
 *============================================================================*/

typedef struct ln_turbo_ctx_s ln_turbo_ctx_t;

/*============================================================================
 * Fast Result Structure (opaque for external use)
 *============================================================================*/

typedef struct ln_fast_result_s ln_fast_result_t;

/*============================================================================
 * Context Management
 *============================================================================*/

/**
 * Initialize turbo VM context.
 * Call from ln_initCtx().
 */
ln_turbo_ctx_t *ln_turbo_ctx_init(void);

/**
 * Free turbo VM context.
 * Call from ln_exitCtx().
 */
void ln_turbo_ctx_free(ln_turbo_ctx_t *turbo);

/**
 * Compile PDAG to VM bytecode.
 * Call after ln_loadSamples() completes successfully.
 *
 * @param ctx  liblognorm context with loaded PDAG
 * @return 0 on success, -1 on error (non-fatal, will use recursive walker)
 */
int ln_turbo_compile(ln_ctx ctx);

/**
 * Check if turbo VM is available for this context.
 */
int ln_turbo_is_available(ln_ctx ctx);

/*============================================================================
 * Normalization API - Choose based on your needs
 *============================================================================*/

/**
 * Normalize using turbo VM - JSON string output.
 * Best for CLI tools and JSON pipelines.
 *
 * @param ctx       liblognorm context
 * @param str       Input string to normalize
 * @param strLen    Length of input string
 * @param json_str  Receives JSON string (caller must free)
 * @param json_len  Receives JSON string length
 * @return 0 on match, negative on no match/error
 */
int ln_turbo_normalize_to_str(ln_ctx ctx, const char *str, size_t strLen,
							  char **json_str, size_t *json_len);

/**
 * Normalize using turbo VM - direct result access.
 * Best for rsyslog - ZERO JSON overhead!
 *
 * Result is valid until next normalize call on same context.
 * DO NOT free the result - it's owned by the turbo context.
 *
 * @param ctx       liblognorm context
 * @param str       Input string to normalize
 * @param strLen    Length of input string
 * @param result    Receives pointer to internal result structure
 * @return 0 on match, negative on no match/error
 */
int ln_turbo_normalize_raw(ln_ctx ctx, const char *str, size_t strLen,
						   const ln_fast_result_t **result);

/**
 * Normalize using turbo VM - libfastjson object output.
 * Legacy API for backward compatibility. Avoid if possible.
 *
 * @param ctx       liblognorm context
 * @param str       Input string to normalize
 * @param strLen    Length of input string
 * @param json_p    Receives JSON result object (caller must free)
 * @return 0 on match, negative on no match/error
 */
int ln_turbo_normalize(ln_ctx ctx, const char *str, size_t strLen,
					   struct json_object **json_p);

/*============================================================================
 * Direct Field Access API (for rsyslog - no JSON overhead)
 *============================================================================*/

/**
 * Get number of fields in result.
 */
int ln_fast_result_field_count(const ln_fast_result_t *r);

/**
 * Get field by index.
 *
 * @param r      Result
 * @param idx    Field index (0 to field_count-1)
 * @param name   Receives field name pointer (do not free)
 * @param nlen   Receives name length
 * @param value  Receives value pointer (do not free)
 * @param vlen   Receives value length
 * @return 0 on success, -1 if index out of range
 */
int ln_fast_result_get_field(const ln_fast_result_t *r, int idx,
							 const char **name, size_t *nlen,
							 const char **value, size_t *vlen);

/**
 * Get string field by name.
 *
 * @param r      Result
 * @param name   Field name to find
 * @param value  Receives value pointer
 * @param vlen   Receives value length
 * @return 0 if found, -1 if not found
 */
int ln_fast_result_get_string(const ln_fast_result_t *r, const char *name,
							  const char **value, size_t *vlen);

/**
 * Get integer field by name.
 *
 * @param r      Result
 * @param name   Field name to find
 * @param value  Receives integer value
 * @return 0 if found, -1 if not found or not an integer
 */
int ln_fast_result_get_int(const ln_fast_result_t *r, const char *name,
						   int64_t *value);

/**
 * Get number of tags in result.
 */
int ln_fast_result_tag_count(const ln_fast_result_t *r);

/**
 * Get tag by index.
 *
 * @param r    Result
 * @param idx  Tag index (0 to tag_count-1)
 * @return Tag string or NULL if index out of range
 */
const char *ln_fast_result_get_tag(const ln_fast_result_t *r, int idx);

/**
 * Check if result has a specific tag.
 *
 * @param r    Result
 * @param tag  Tag to check
 * @return 1 if tag present, 0 if not
 */
int ln_fast_result_has_tag(const ln_fast_result_t *r, const char *tag);

/**
 * Get matched rule ID.
 *
 * @param r  Result
 * @return Rule ID string or NULL if no match
 */
const char *ln_fast_result_get_rule_id(const ln_fast_result_t *r);

/*============================================================================
 * Snapshot API (for rsyslog zero-JSON hot path)
 *============================================================================*/

/**
 * Forward declare snapshot type.
 * Full definition in turbo_snapshot.h.
 */
typedef struct ln_fast_result_snapshot_s ln_fast_result_snapshot_t;

/**
 * Create a snapshot of the current turbo parse result.
 *
 * Must be called after a successful ln_turbo_normalize_raw() and before
 * the next normalize call on the same context (which resets the arena).
 *
 * The snapshot is a self-contained deep copy: single allocation containing
 * the result struct + arena data, with all pointers rebased.
 *
 * @param ctx  liblognorm context with a valid turbo result
 * @return Snapshot (caller must free with ln_fast_result_snapshot_free),
 *         or NULL on failure
 */
ln_fast_result_snapshot_t *ln_turbo_snapshot_result(ln_ctx ctx);

/*============================================================================
 * Statistics
 *============================================================================*/

typedef struct {
	uint32_t n_instructions;    /**< Instructions in compiled program */
	uint32_t n_rules;           /**< Number of rules */
	uint32_t n_branches;        /**< Branch instructions (FORK) */
	uint32_t max_depth;         /**< Max PDAG depth */
	uint64_t messages_processed;/**< Messages normalized */
	uint64_t total_bytes;       /**< Total bytes processed */
	uint64_t vm_time_ns;        /**< Time spent in VM (nanoseconds) */
} ln_turbo_stats_t;

/**
 * Get turbo VM statistics.
 */
int ln_turbo_get_stats(ln_ctx ctx, ln_turbo_stats_t *stats);

/**
 * Dump compiled bytecode disassembly to a file stream.
 * Useful for verifying per-worker bytecode matches the standalone tool.
 * Only available when liblognorm is built with -DLN_VM_TRACE.
 *
 * @param ctx   liblognorm context with compiled turbo bytecode
 * @param fp    Output stream (e.g. stderr)
 * @param label Label to prefix the disassembly (e.g. "worker-turbo")
 */
void ln_turbo_disasm(ln_ctx ctx, FILE *fp, const char *label);

#ifdef __cplusplus
}
#endif

#else /* !ENABLE_TURBO && !LOGNORM_TURBO_SUPPORTED */

/* Stub definitions when turbo is disabled */
typedef void *ln_turbo_ctx_t;
typedef void *ln_fast_result_t;
typedef void *ln_fast_result_snapshot_t;

#define ln_turbo_ctx_init()          NULL
#define ln_turbo_ctx_free(t)         ((void)(t))
#define ln_turbo_compile(ctx)        (-1)
#define ln_turbo_is_available(ctx)   (0)
#define ln_turbo_normalize(ctx, str, len, json) (-1)
#define ln_turbo_normalize_to_str(ctx, str, len, js, jl) (-1)
#define ln_turbo_normalize_raw(ctx, str, len, r) (-1)
#define ln_turbo_snapshot_result(ctx)             ((void*)0)
#define ln_fast_result_snapshot_get(snap)         ((void*)0)
#define ln_fast_result_snapshot_free(snap)        ((void)(snap))
#define ln_turbo_disasm(ctx, fp, label)          ((void)0)

#endif /* ENABLE_TURBO || LOGNORM_TURBO_SUPPORTED */

#endif /* LIBLOGNORM_TURBO_H_INCLUDED */
