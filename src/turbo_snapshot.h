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
typedef struct ln_fast_result_snapshot_s {
	ln_fast_result_t result;    /**< Deep copy of result (pointers rebased) */
	size_t           arena_size;/**< Size of arena data copy */
	char             arena_data[];/**< Flexible array: arena bytes follow inline */
} ln_fast_result_snapshot_t;

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

/**
 * @brief Get the result from a snapshot.
 *
 * The returned pointer is valid for the lifetime of the snapshot.
 * It can be used with all ln_fast_result_* accessor functions.
 *
 * @param snap  Snapshot to read from
 * @return Pointer to the result, or NULL if snap is NULL
 */
const ln_fast_result_t *
ln_fast_result_snapshot_get(const ln_fast_result_snapshot_t *snap);

/**
 * @brief Free a snapshot.
 *
 * Single free() — the snapshot is a single allocation.
 *
 * @param snap  Snapshot to free (NULL-safe)
 */
void
ln_fast_result_snapshot_free(ln_fast_result_snapshot_t *snap);

#ifdef __cplusplus
}
#endif

#else /* !ENABLE_TURBO && !LOGNORM_TURBO_SUPPORTED */

/* Stubs when turbo is disabled */
typedef void ln_fast_result_snapshot_t;
#define ln_fast_result_snapshot_create(src, arena)  ((void*)0)
#define ln_fast_result_snapshot_get(snap)           ((void*)0)
#define ln_fast_result_snapshot_free(snap)          ((void)(snap))

#endif /* ENABLE_TURBO || LOGNORM_TURBO_SUPPORTED */

#endif /* LIBLOGNORM_TURBO_SNAPSHOT_H_INCLUDED */
