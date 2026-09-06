/**
 * @file turbo.h
 * @brief liblognorm integration for TurboVM bytecode engine (internal).
 *
 * INTERNAL header: not installed. The public, opaque Turbo API lives in
 * lognorm-turbo.h (which this header includes); only internal lifecycle,
 * legacy and debug entry points are declared here.
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

/* Public, opaque Turbo API (opaque types, enum, accessors, snapshots). */
#include "lognorm-turbo.h"

#if defined(ENABLE_TURBO) || defined(LOGNORM_TURBO_SUPPORTED)

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations (ln_ctx comes from lognorm-turbo.h) */
struct json_object;  /* libfastjson (for legacy API only) */

/*============================================================================
 * Turbo VM State (opaque, internal)
 *============================================================================*/

typedef struct ln_turbo_ctx_s ln_turbo_ctx_t;

/*============================================================================
 * Context Management (internal lifecycle)
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

/*============================================================================
 * Legacy normalization (libfastjson object output): internal
 *============================================================================*/

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
 * Statistics & debug (internal)
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

/* Stub definitions when turbo is disabled (public data-type stubs and the
 * public entry-point stubs are provided by lognorm-turbo.h). */
typedef void *ln_turbo_ctx_t;

#define ln_turbo_ctx_init()          NULL
#define ln_turbo_ctx_free(t)         ((void)(t))
#define ln_turbo_compile(ctx)        (-1)
#define ln_turbo_normalize(ctx, str, len, json) (-1)
#define ln_turbo_disasm(ctx, fp, label)          ((void)0)

#endif /* ENABLE_TURBO || LOGNORM_TURBO_SUPPORTED */

#endif /* LIBLOGNORM_TURBO_H_INCLUDED */
