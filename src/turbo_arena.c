/*
 * turbo_arena.c -- High-performance arena allocator
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
#include "turbo_arena.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/**
 * @brief Check if a value is a power of two.
 */
static inline bool
is_power_of_two(size_t x)
{
	return x && !(x & (x - 1));
}

/**
 * @brief Align a size/offset up to the specified alignment.
 *
 * @param value      Value to align
 * @param alignment  Alignment boundary (must be power of 2)
 * @return Aligned value (>= original value)
 */
static inline size_t
align_up(size_t value, size_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

/**
 * @brief Clamp a capacity value to valid range.
 */
static inline size_t
clamp_capacity(size_t capacity)
{
	if (capacity < LN_ARENA_MIN_CAPACITY) {
		return LN_ARENA_MIN_CAPACITY;
	}
	if (capacity > LN_ARENA_MAX_CAPACITY) {
		return LN_ARENA_MAX_CAPACITY;
	}
	return capacity;
}

/**
 * @brief Allocate aligned memory from the system.
 *
 * Uses posix_memalign on POSIX systems, _aligned_malloc on Windows.
 *
 * @param size       Number of bytes to allocate
 * @param alignment  Alignment boundary
 * @return Aligned pointer, or NULL on failure
 */
static void *
system_aligned_alloc(size_t size, size_t alignment)
{
	void *ptr = NULL;

#if defined(_WIN32) || defined(_WIN64)
	ptr = _aligned_malloc(size, alignment);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__APPLE__)
	/* C11 aligned_alloc - size must be multiple of alignment */
	size_t aligned_size = align_up(size, alignment);
	ptr = aligned_alloc(alignment, aligned_size);
#else
	/* POSIX posix_memalign */
	if (posix_memalign(&ptr, alignment, size) != 0) {
		ptr = NULL;
	}
#endif

	return ptr;
}

/**
 * @brief Free aligned memory allocated by system_aligned_alloc.
 */
static void
system_aligned_free(void *ptr)
{
#if defined(_WIN32) || defined(_WIN64)
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

/**
 * @brief Check for addition overflow.
 */
static inline bool
would_overflow_add(size_t a, size_t b)
{
	return a > SIZE_MAX - b;
}

/*============================================================================
 * Lifecycle Functions
 *============================================================================*/

int
ln_arena_init(ln_arena_t *arena)
{
	return ln_arena_init_sized(arena, LN_ARENA_DEFAULT_CAPACITY);
}

int
ln_arena_init_sized(ln_arena_t *arena, size_t capacity)
{
	if (!arena) {
		return LN_ARENA_EINVAL;
	}

	/* Zero the structure first */
	memset(arena, 0, sizeof(*arena));

	/* Clamp and align capacity */
	capacity = clamp_capacity(capacity);
	capacity = align_up(capacity, LN_ARENA_CACHE_LINE);

	/* Allocate cache-line-aligned memory for optimal performance */
	arena->base = (uint8_t *)system_aligned_alloc(capacity, LN_ARENA_CACHE_LINE);
	if (!arena->base) {
		return LN_ARENA_ENOMEM;
	}

	arena->capacity = capacity;
	arena->used = 0;
	arena->peak = 0;
	arena->alloc_count = 0;
	arena->flags = LN_ARENA_FLAG_OWNED;

	return LN_ARENA_OK;
}

int
ln_arena_init_static(ln_arena_t *arena, void *buffer, size_t capacity)
{
	if (!arena || !buffer) {
		return LN_ARENA_EINVAL;
	}

	if (capacity < LN_ARENA_MIN_CAPACITY) {
		return LN_ARENA_EINVAL;
	}

	memset(arena, 0, sizeof(*arena));

	arena->base = (uint8_t *)buffer;
	arena->capacity = capacity;
	arena->used = 0;
	arena->peak = 0;
	arena->alloc_count = 0;
	arena->flags = LN_ARENA_FLAG_STATIC;

	return LN_ARENA_OK;
}

void
ln_arena_destroy(ln_arena_t *arena)
{
	if (!arena) {
		return;
	}

	/* Only free if we own the memory */
	if ((arena->flags & LN_ARENA_FLAG_OWNED) && arena->base) {
		system_aligned_free(arena->base);
	}

	/* Zero the structure for safety */
	memset(arena, 0, sizeof(*arena));
}

/*============================================================================
 * Allocation Functions
 *============================================================================*/

void *
ln_arena_alloc(ln_arena_t *arena, size_t size)
{
	return ln_arena_alloc_aligned(arena, size, LN_ARENA_DEFAULT_ALIGN);
}

void *
ln_arena_alloc_array(ln_arena_t *arena, size_t nmemb, size_t elem)
{
	/* Checked multiply: reject nmemb * elem that would wrap (CWE-190).
	 * ln_arena_alloc() sees only the (already-wrapped) product and cannot
	 * detect the overflow itself, so the guard must live here. */
	if (elem != 0 && nmemb > SIZE_MAX / elem) {
		return NULL;
	}

	return ln_arena_alloc(arena, nmemb * elem);
}

void *
ln_arena_alloc_aligned(ln_arena_t *arena, size_t size, size_t alignment)
{
	size_t current;
	size_t aligned_offset;
	size_t new_used;

	if (!arena || !arena->base) {
		return NULL;
	}

	/* Validate alignment */
	if (alignment == 0 || !is_power_of_two(alignment) || alignment > 4096) {
		return NULL;
	}

	/* Zero-size allocations return a valid unique pointer */
	if (size == 0) {
		size = 1;
	}

	/* Calculate aligned offset */
	current = arena->used;
	aligned_offset = align_up(current, alignment);

	/* Check for alignment overflow */
	if (aligned_offset < current) {
		return NULL;  /* Overflow in alignment calculation */
	}

	/* Check for size overflow */
	if (would_overflow_add(aligned_offset, size)) {
		return NULL;
	}

	new_used = aligned_offset + size;

	/* Check capacity */
	if (new_used > arena->capacity) {
		return NULL;  /* Arena exhausted */
	}

	/* Commit allocation */
	arena->used = new_used;
	arena->alloc_count++;

	/* Update peak usage */
	if (new_used > arena->peak) {
		arena->peak = new_used;
	}

	return arena->base + aligned_offset;
}

void *
ln_arena_calloc(ln_arena_t *arena, size_t size)
{
	void *ptr = ln_arena_alloc(arena, size);
	if (ptr) {
		memset(ptr, 0, size);
	}
	return ptr;
}

char *
ln_arena_strdup(ln_arena_t *arena, const char *str)
{
	if (!str) {
		return NULL;
	}
	return ln_arena_strndup(arena, str, strlen(str));
}

char *
ln_arena_strndup(ln_arena_t *arena, const char *str, size_t len)
{
	char *copy;

	if (!arena || !str) {
		return NULL;
	}

	/* Check for overflow when adding null terminator */
	if (len == SIZE_MAX) {
		return NULL;
	}

	/* Allocate space for string + null terminator */
	copy = (char *)ln_arena_alloc(arena, len + 1);
	if (!copy) {
		return NULL;
	}

	memcpy(copy, str, len);
	copy[len] = '\0';

	return copy;
}

void *
ln_arena_memdup(ln_arena_t *arena, const void *src, size_t size)
{
	void *copy;

	if (!arena || !src || size == 0) {
		return NULL;
	}

	copy = ln_arena_alloc(arena, size);
	if (!copy) {
		return NULL;
	}

	memcpy(copy, src, size);
	return copy;
}

/*============================================================================
 * Reset and Backtracking
 *============================================================================*/

void
ln_arena_reset(ln_arena_t *arena)
{
	if (!arena) {
		return;
	}

	arena->used = 0;
	arena->alloc_count = 0;
	/* Note: peak is intentionally preserved across resets */
}

void
ln_arena_save(const ln_arena_t *arena, ln_arena_mark_t *mark)
{
	if (!arena || !mark) {
		return;
	}

	mark->used = arena->used;
	mark->alloc_count = arena->alloc_count;
}

void
ln_arena_restore(ln_arena_t *arena, const ln_arena_mark_t *mark)
{
	if (!arena || !mark) {
		return;
	}

	/* Sanity check: can't restore to a point beyond current usage */
	if (mark->used > arena->used) {
		return;
	}

	arena->used = mark->used;
	arena->alloc_count = mark->alloc_count;
}

/*============================================================================
 * Query Functions
 *============================================================================*/

void
ln_arena_get_stats(const ln_arena_t *arena, ln_arena_stats_t *stats)
{
	if (!stats) {
		return;
	}

	memset(stats, 0, sizeof(*stats));

	if (!arena) {
		return;
	}

	stats->capacity = arena->capacity;
	stats->used = arena->used;
	stats->peak = arena->peak;
	stats->available = arena->capacity - arena->used;
	stats->alloc_count = arena->alloc_count;

	if (arena->capacity > 0) {
		stats->utilization = (double)arena->used / (double)arena->capacity;
	}
}

/*============================================================================
 * Debug/Test Support
 *============================================================================*/

#ifdef LN_ARENA_DEBUG

#include <stdio.h>

/**
 * @brief Dump arena state to stderr (debug builds only).
 */
void
ln_arena_dump(const ln_arena_t *arena, const char *label)
{
	if (!arena) {
		fprintf(stderr, "[ARENA] %s: NULL\n", label ? label : "arena");
		return;
	}

	fprintf(stderr, "[ARENA] %s: base=%p capacity=%zu used=%zu peak=%zu "
			"alloc_count=%u flags=0x%x\n",
			label ? label : "arena",
			(void *)arena->base,
			arena->capacity,
			arena->used,
			arena->peak,
			arena->alloc_count,
			arena->flags);
}

#endif /* LN_ARENA_DEBUG */
