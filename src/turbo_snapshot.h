/**
 * @file turbo_snapshot.h
 * @brief Deep-copy snapshot of turbo parse results
 *//*
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_TURBO_SNAPSHOT_H_INCLUDED
#define	LIBLOGNORM_TURBO_SNAPSHOT_H_INCLUDED

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Public opaque snapshot type + get/free decls (and disabled-build stubs). */
#include "lognorm-turbo.h"

#if defined(ENABLE_TURBO) || defined(LOGNORM_TURBO_SUPPORTED)

#include <stddef.h>
#include "turbo_result_fast.h"
#include "turbo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Self-contained snapshot of a turbo parse result.
 *
 * Memory layout: header struct followed by inline arena data copy.
 * All arena pointers in the result are rebased to point into arena_data[].
 */
/* Completes the opaque ln_fast_result_snapshot_t declared in lognorm-turbo.h. */
struct ln_fast_result_snapshot_s {
	ln_fast_result_t result;    /**< Deep copy of result (pointers rebased) */
	size_t           arena_size;/**< Size of arena data copy */
	char             arena_data[];/**< Flexible array: arena bytes follow inline */
};

/**
 * @brief Create a snapshot from a turbo parse result.
 *
 * Performs a single malloc(sizeof(snapshot) + arena->used), copies the
 * result struct and arena bytes, then rebases all arena pointers.
 *
 * @param src    Source result (from ln_turbo_normalize_raw)
 * @param arena  Arena that backs the result's string data
 * @return Snapshot (caller owns), or NULL on allocation failure
 *
 * Cost: 1 malloc + ~4.4KB memcpy (result) + arena->used memcpy + field walk
 * Typically under 6KB total for a ~20-field log message.
 */
ln_fast_result_snapshot_t *
ln_fast_result_snapshot_create(const ln_fast_result_t *src,
								const ln_arena_t *arena);

/* ln_fast_result_snapshot_get() and ln_fast_result_snapshot_free() are part
 * of the public, opaque API and are declared in lognorm-turbo.h. */

#ifdef __cplusplus
}
#endif

#else /* !ENABLE_TURBO && !LOGNORM_TURBO_SUPPORTED */

/* Stubs when turbo is disabled (the snapshot type and the public get/free
 * stubs are provided by lognorm-turbo.h, included via turbo_result_fast.h). */
#define ln_fast_result_snapshot_create(src, arena)  ((void*)0)

#endif /* ENABLE_TURBO || LOGNORM_TURBO_SUPPORTED */

#endif /* LIBLOGNORM_TURBO_SNAPSHOT_H_INCLUDED */
