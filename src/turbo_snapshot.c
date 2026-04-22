/*
 * turbo_snapshot.c -- Deep-copy snapshot of turbo parse results
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

#ifdef ENABLE_TURBO

#include "turbo_snapshot.h"
#include "turbo_result_fast.h"
#include "turbo_arena.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/**
 * @brief Check if a pointer falls within the arena region.
 */
static inline int
ptr_in_arena(const void *ptr, const uint8_t *arena_base, size_t arena_used)
{
	const uint8_t *p = (const uint8_t *)ptr;
	return (p >= arena_base && p < arena_base + arena_used);
}

/**
 * @brief Rebase a pointer from old arena to new arena copy.
 */
static inline const char *
rebase_ptr(const char *ptr, const uint8_t *old_base, const char *new_base)
{
	ptrdiff_t offset = (const uint8_t *)ptr - old_base;
	return new_base + offset;
}

ln_fast_result_snapshot_t *
ln_fast_result_snapshot_create(const ln_fast_result_t *src,
								const ln_arena_t *arena)
{
	size_t arena_used = 0;
	const uint8_t *arena_base = NULL;
	size_t total;
	ln_fast_result_snapshot_t *snap;

	if (!src) return NULL;

	/* Determine arena copy size — may be 0 if no arena or no overflow strings */
	if (arena && arena->base && arena->used > 0) {
		arena_used = arena->used;
		arena_base = arena->base;
	}

	/* Single allocation: header + arena data */
	total = sizeof(ln_fast_result_snapshot_t) + arena_used;
	snap = malloc(total);
	if (!snap) return NULL;

	/* Copy the result struct */
	memcpy(&snap->result, src, sizeof(ln_fast_result_t));
	snap->arena_size = arena_used;

	/* Copy arena bytes */
	if (arena_used > 0) {
		memcpy(snap->arena_data, arena_base, arena_used);
	}

	/* Detach from original arena — snapshot is self-contained */
	snap->result.arena = NULL;

	/* Rebase all pointers that reference the arena region */
	for (int i = 0; i < snap->result.n_fields; i++) {
		ln_fast_field_t *f = &snap->result.fields[i];

		/* Rebase field name if it points into the arena */
		if (f->name && arena_base &&
			!(f->flags & LN_FFIELD_STATIC_NAME) &&
			ptr_in_arena(f->name, arena_base, arena_used)) {
			f->name = rebase_ptr(f->name, arena_base,
								 (const char *)snap->arena_data);
		}

		/* Rebase string value if it points into the arena */
		if (f->type == LN_FTYPE_STRING && f->v.str.ptr &&
			arena_base &&
			ptr_in_arena(f->v.str.ptr, arena_base, arena_used)) {
			f->v.str.ptr = rebase_ptr(f->v.str.ptr, arena_base,
									  (const char *)snap->arena_data);
		}
		/* LN_FTYPE_STRING_INLINE: data is inline in the struct, already copied */
		/* LN_FTYPE_INT/DOUBLE/BOOL: no pointers to rebase */
	}

	/* Rebase rule_id if it points into the arena (unlikely, usually static) */
	if (snap->result.rule_id && arena_base &&
		ptr_in_arena(snap->result.rule_id, arena_base, arena_used)) {
		snap->result.rule_id = rebase_ptr(snap->result.rule_id, arena_base,
										  (const char *)snap->arena_data);
	}

	/* Note: original message pointer (result.original) typically points
	 * into the input buffer, NOT the arena. We leave it as-is because
	 * the input buffer outlives the snapshot in the rsyslog pipeline
	 * (message string is on the smsg_t). If it pointed into the arena,
	 * we'd rebase it too. */
	if (snap->result.original && arena_base &&
		ptr_in_arena(snap->result.original, arena_base, arena_used)) {
		snap->result.original = rebase_ptr(snap->result.original, arena_base,
										   (const char *)snap->arena_data);
	}

	/* Tag strings are static (compile-time constants), no rebasing needed */

	return snap;
}

const ln_fast_result_t *
ln_fast_result_snapshot_get(const ln_fast_result_snapshot_t *snap)
{
	if (!snap) return NULL;
	return &snap->result;
}

void
ln_fast_result_snapshot_free(ln_fast_result_snapshot_t *snap)
{
	/* Single allocation — single free */
	free(snap);
}

#endif /* ENABLE_TURBO */
