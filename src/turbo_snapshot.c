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

/**
 * @brief Number of trailing bytes a non-arena string needs in the snapshot
 *        backing buffer when it has to be copied in (len + 1 for the NUL).
 *
 * A pointer needs copying iff it is non-NULL and does NOT already live in the
 * arena region (those are handled by the cheap rebase path). The byte count
 * uses the stored length — values carry an explicit length and may contain
 * embedded NULs; names are length-prefixed too. We always append a trailing
 * NUL so the snapshot strings stay C-string compatible like the originals.
 */
static inline size_t
copy_bytes_if_external(const char *ptr, size_t len,
					   const uint8_t *arena_base, size_t arena_used)
{
	if (!ptr) return 0;
	if (arena_base && ptr_in_arena(ptr, arena_base, arena_used)) return 0;
	return len + 1;
}

/**
 * @brief Append @p len bytes from @p src into the snapshot backing buffer at
 *        @p *off, NUL-terminate, and return the destination pointer. Advances
 *        @p *off past the written bytes (+ the NUL).
 */
static inline const char *
append_external(char *backing, size_t *off, const char *src, size_t len)
{
	char *dst = backing + *off;
	memcpy(dst, src, len);
	dst[len] = '\0';
	*off += len + 1;
	return dst;
}

ln_fast_result_snapshot_t *
ln_fast_result_snapshot_create(const ln_fast_result_t *src,
								const ln_arena_t *arena)
{
	size_t arena_used = 0;
	const uint8_t *arena_base = NULL;
	size_t extra = 0;        /* bytes for non-arena strings copied in */
	size_t off;              /* bump offset into the backing buffer    */
	size_t total;
	char *backing;
	ln_fast_result_snapshot_t *snap;

	if (!src) return NULL;

	/* Determine arena copy size — may be 0 if no arena or no overflow strings */
	if (arena && arena->base && arena->used > 0) {
		arena_used = arena->used;
		arena_base = arena->base;
	}

	/* Pass 1: size the extra space needed for every pointer that does NOT
	 * live in the arena and therefore cannot be rebased. "Not in arena" does
	 * NOT imply "long-lived/static" — OP_FIELD_REST/WORD/STR_TO/CHAR_TO/QUOTED
	 * store long (>= LN_FAST_INLINE_SIZE) values as raw pointers straight into
	 * the input line. Those, plus non-static names and the original message,
	 * must be copied so the snapshot can outlive the input line. */
	for (int i = 0; i < src->n_fields; i++) {
		const ln_fast_field_t *f = &src->fields[i];

		if (f->type == LN_FTYPE_STRING)
			extra += copy_bytes_if_external(f->v.str.ptr, f->v.str.len,
											arena_base, arena_used);
		if (!(f->flags & LN_FFIELD_STATIC_NAME))
			extra += copy_bytes_if_external(f->name, f->name_len,
											arena_base, arena_used);
	}
	extra += copy_bytes_if_external(src->original, src->original_len,
									arena_base, arena_used);

	/* Single allocation: header + arena data + non-arena string copies */
	total = sizeof(ln_fast_result_snapshot_t) + arena_used + extra;
	snap = malloc(total);
	if (!snap) return NULL;

	/* Copy the result struct */
	memcpy(&snap->result, src, sizeof(ln_fast_result_t));
	/* arena_size tracks the whole owned backing region (arena + extra) so the
	 * snapshot remains a single self-contained allocation. */
	snap->arena_size = arena_used + extra;

	backing = snap->arena_data;

	/* Copy arena bytes into the head of the backing buffer */
	if (arena_used > 0) {
		memcpy(backing, arena_base, arena_used);
	}

	/* Detach from original arena — snapshot is self-contained */
	snap->result.arena = NULL;

	/* Pass 2: rebase arena pointers; copy-and-repoint non-arena ones. The
	 * non-arena copies are bump-appended after the arena region. */
	off = arena_used;
	for (int i = 0; i < snap->result.n_fields; i++) {
		ln_fast_field_t *f = &snap->result.fields[i];

		/* Field name: rebase if in arena, else copy in (unless static). */
		if (f->name && !(f->flags & LN_FFIELD_STATIC_NAME)) {
			if (arena_base && ptr_in_arena(f->name, arena_base, arena_used)) {
				f->name = rebase_ptr(f->name, arena_base, backing);
			} else {
				f->name = append_external(backing, &off,
										  f->name, f->name_len);
			}
		}

		/* String value: rebase if in arena, else copy in. */
		if (f->type == LN_FTYPE_STRING && f->v.str.ptr) {
			if (arena_base &&
				ptr_in_arena(f->v.str.ptr, arena_base, arena_used)) {
				f->v.str.ptr = rebase_ptr(f->v.str.ptr, arena_base, backing);
			} else {
				f->v.str.ptr = append_external(backing, &off,
											   f->v.str.ptr, f->v.str.len);
			}
		}
		/* LN_FTYPE_STRING_INLINE: data is inline in the struct, already copied */
		/* LN_FTYPE_INT/DOUBLE/BOOL: no pointers to rebase */
	}

	/* Rebase rule_id if it points into the arena (otherwise it is a
	 * compile-time-static rule identifier with program lifetime). */
	if (snap->result.rule_id && arena_base &&
		ptr_in_arena(snap->result.rule_id, arena_base, arena_used)) {
		snap->result.rule_id = rebase_ptr(snap->result.rule_id, arena_base,
										  backing);
	}

	/* Original message: rebase if in arena, else copy in. It usually points
	 * straight into the (caller-owned, soon-to-be-freed/reused) input line,
	 * so a verbatim pointer copy would dangle. */
	if (snap->result.original) {
		if (arena_base &&
			ptr_in_arena(snap->result.original, arena_base, arena_used)) {
			snap->result.original = rebase_ptr(snap->result.original,
											   arena_base, backing);
		} else {
			snap->result.original = append_external(backing, &off,
													snap->result.original,
													snap->result.original_len);
		}
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
