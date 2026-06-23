/**
 * @file turbo_arena.h
 * @brief High-performance arena allocator
 *//*
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_TURBO_ARENA_H_INCLUDED
#define	LIBLOGNORM_TURBO_ARENA_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Configuration
 *============================================================================*/

/**
 * Default arena capacity (bytes).
 * Sized for typical log messages with 20-30 extracted fields.
 * 16KB fits in L1 cache on most modern CPUs.
 */
#define LN_ARENA_DEFAULT_CAPACITY  16384

/**
 * Minimum arena capacity (bytes).
 * Must be at least one cache line.
 */
#define LN_ARENA_MIN_CAPACITY  64

/**
 * Maximum arena capacity (bytes).
 * Prevents accidental huge allocations. 16MB should handle any message.
 */
#define LN_ARENA_MAX_CAPACITY  (16 * 1024 * 1024)

/**
 * Default alignment for allocations (bytes).
 * 8-byte alignment is sufficient for most data types.
 */
#define LN_ARENA_DEFAULT_ALIGN  8

/**
 * Cache line size for alignment hints.
 */
#define LN_ARENA_CACHE_LINE  64

/*============================================================================
 * Error Codes
 *============================================================================*/

#define LN_ARENA_OK          0   /**< Success */
#define LN_ARENA_EINVAL     -1   /**< Invalid argument */
#define LN_ARENA_ENOMEM     -2   /**< Out of memory (system) */
#define LN_ARENA_EOVERFLOW  -3   /**< Arena capacity exceeded */
#define LN_ARENA_EALIGN     -4   /**< Invalid alignment */

/*============================================================================
 * Data Structures
 *============================================================================*/

/**
 * Arena memory region.
 *
 * Memory layout:
 * ┌────────────────────────────────────────────────────────────┐
 * │  Allocated region (used)    │  Free region (available)    │
 * └────────────────────────────────────────────────────────────┘
 * ^                             ^                              ^
 * base                          base + used                    base + capacity
 *
 * Allocations grow upward from base. Reset returns used to zero.
 */
typedef struct ln_arena_s {
	uint8_t    *base;       /**< Base pointer of allocated region */
	size_t      capacity;   /**< Total capacity in bytes */
	size_t      used;       /**< Currently used bytes */
	size_t      peak;       /**< Peak usage (high water mark) */
	uint32_t    alloc_count;/**< Number of allocations (for stats) */
	uint32_t    flags;      /**< Arena flags */
} ln_arena_t;

/**
 * Arena flags.
 */
#define LN_ARENA_FLAG_OWNED     0x0001  /**< Arena owns the memory (should free) */
#define LN_ARENA_FLAG_STATIC    0x0002  /**< Arena uses static/external buffer */

/**
 * Arena mark for save/restore during backtracking.
 *
 * Usage pattern:
 *   ln_arena_mark_t mark;
 *   ln_arena_save(arena, &mark);
 *   // ... attempt parse, allocate fields ...
 *   if (parse_failed) {
 *       ln_arena_restore(arena, &mark);  // rollback allocations
 *   }
 */
typedef struct ln_arena_mark_s {
	size_t      used;       /**< Saved used offset */
	uint32_t    alloc_count;/**< Saved allocation count */
} ln_arena_mark_t;

/**
 * Arena statistics.
 */
typedef struct ln_arena_stats_s {
	size_t      capacity;       /**< Total capacity */
	size_t      used;           /**< Current usage */
	size_t      peak;           /**< Peak usage */
	size_t      available;      /**< Remaining capacity */
	uint32_t    alloc_count;    /**< Total allocations since last reset */
	uint32_t    reset_count;    /**< Number of resets (if tracked externally) */
	double      utilization;    /**< Current utilization (0.0 - 1.0) */
} ln_arena_stats_t;

/*============================================================================
 * Lifecycle Functions
 *============================================================================*/

/**
 * @brief Initialize an arena with default capacity.
 *
 * Allocates LN_ARENA_DEFAULT_CAPACITY bytes of memory for the arena.
 * The arena takes ownership of the allocated memory.
 *
 * @param[out] arena  Arena to initialize (must not be NULL)
 * @return LN_ARENA_OK on success, negative error code on failure
 *
 * @note Call ln_arena_destroy() to free resources.
 *
 * Example:
 * @code
 *   ln_arena_t arena;
 *   if (ln_arena_init(&arena) != LN_ARENA_OK) {
 *       // handle error
 *   }
 *   // ... use arena ...
 *   ln_arena_destroy(&arena);
 * @endcode
 */
int ln_arena_init(ln_arena_t *arena);

/**
 * @brief Initialize an arena with specified capacity.
 *
 * @param[out] arena     Arena to initialize (must not be NULL)
 * @param[in]  capacity  Desired capacity in bytes
 * @return LN_ARENA_OK on success, negative error code on failure
 *
 * @note Capacity is clamped to [LN_ARENA_MIN_CAPACITY, LN_ARENA_MAX_CAPACITY]
 */
int ln_arena_init_sized(ln_arena_t *arena, size_t capacity);

/**
 * @brief Initialize an arena using an external buffer.
 *
 * The arena does NOT take ownership of the buffer. The caller must ensure
 * the buffer remains valid for the lifetime of the arena.
 *
 * @param[out] arena     Arena to initialize (must not be NULL)
 * @param[in]  buffer    External buffer (must not be NULL)
 * @param[in]  capacity  Buffer size in bytes
 * @return LN_ARENA_OK on success, negative error code on failure
 *
 * @note ln_arena_destroy() will NOT free the external buffer.
 *
 * Example:
 * @code
 *   uint8_t stack_buffer[4096];
 *   ln_arena_t arena;
 *   ln_arena_init_static(&arena, stack_buffer, sizeof(stack_buffer));
 *   // ... use arena ...
 *   ln_arena_destroy(&arena);  // safe, does not free stack_buffer
 * @endcode
 */
int ln_arena_init_static(ln_arena_t *arena, void *buffer, size_t capacity);

/**
 * @brief Destroy an arena and free its resources.
 *
 * If the arena owns its memory (allocated via ln_arena_init or
 * ln_arena_init_sized), the memory is freed. Static arenas are
 * simply zeroed.
 *
 * @param[in,out] arena  Arena to destroy (may be NULL, no-op if so)
 *
 * @note Safe to call multiple times on the same arena.
 * @note After destroy, arena is zeroed and safe to re-init.
 */
void ln_arena_destroy(ln_arena_t *arena);

/*============================================================================
 * Allocation Functions
 *============================================================================*/

/**
 * @brief Allocate memory from the arena.
 *
 * Returns a pointer to at least `size` bytes of memory, aligned to
 * LN_ARENA_DEFAULT_ALIGN (8 bytes). The memory is NOT initialized.
 *
 * @param[in] arena  Arena to allocate from (must not be NULL)
 * @param[in] size   Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if arena is exhausted
 *
 * @note Returned pointer is valid until arena is reset or destroyed.
 * @note For aligned allocations, use ln_arena_alloc_aligned().
 */
void *ln_arena_alloc(ln_arena_t *arena, size_t size);

/**
 * @brief Allocate an array of `nmemb` elements of `elem` bytes from the arena.
 *
 * Computes the total allocation size as `nmemb * elem` with a checked
 * multiply. If the product would overflow size_t (CWE-190), returns NULL
 * instead of allocating an undersized region. This is the safe primitive
 * behind the LN_ARENA_ARRAY() macro and must be used whenever the element
 * count may be derived from untrusted input.
 *
 * @param[in] arena  Arena to allocate from (must not be NULL)
 * @param[in] nmemb  Number of elements
 * @param[in] elem   Size of each element in bytes
 * @return Pointer to allocated memory, or NULL on overflow or exhaustion
 *
 * @note Returned pointer is valid until arena is reset or destroyed.
 * @note The memory is NOT initialized.
 */
void *ln_arena_alloc_array(ln_arena_t *arena, size_t nmemb, size_t elem);

/**
 * @brief Allocate aligned memory from the arena.
 *
 * Returns a pointer to at least `size` bytes of memory, aligned to
 * the specified alignment boundary.
 *
 * @param[in] arena      Arena to allocate from (must not be NULL)
 * @param[in] size       Number of bytes to allocate
 * @param[in] alignment  Alignment boundary (must be power of 2, max 4096)
 * @return Pointer to allocated memory, or NULL on failure
 *
 * @note Use for SIMD data requiring 16, 32, or 64-byte alignment.
 *
 * Example:
 * @code
 *   // Allocate 64-byte aligned buffer for SIMD operations
 *   void *simd_buf = ln_arena_alloc_aligned(arena, 256, 64);
 * @endcode
 */
void *ln_arena_alloc_aligned(ln_arena_t *arena, size_t size, size_t alignment);

/**
 * @brief Allocate and zero-initialize memory from the arena.
 *
 * Equivalent to ln_arena_alloc() followed by memset to zero.
 *
 * @param[in] arena  Arena to allocate from
 * @param[in] size   Number of bytes to allocate
 * @return Pointer to zero-initialized memory, or NULL on failure
 */
void *ln_arena_calloc(ln_arena_t *arena, size_t size);

/**
 * @brief Duplicate a string into the arena.
 *
 * Allocates strlen(str)+1 bytes and copies the string including
 * the null terminator.
 *
 * @param[in] arena  Arena to allocate from
 * @param[in] str    String to duplicate (must not be NULL)
 * @return Pointer to duplicated string, or NULL on failure
 */
char *ln_arena_strdup(ln_arena_t *arena, const char *str);

/**
 * @brief Duplicate a string with known length into the arena.
 *
 * Allocates len+1 bytes, copies len bytes, and null-terminates.
 * More efficient than ln_arena_strdup() when length is known.
 *
 * @param[in] arena  Arena to allocate from
 * @param[in] str    String to duplicate (must not be NULL)
 * @param[in] len    Number of characters to copy (excluding null terminator)
 * @return Pointer to duplicated string, or NULL on failure
 *
 * @note The source string does not need to be null-terminated.
 */
char *ln_arena_strndup(ln_arena_t *arena, const char *str, size_t len);

/**
 * @brief Copy memory into the arena.
 *
 * Allocates `size` bytes and copies data from `src`.
 *
 * @param[in] arena  Arena to allocate from
 * @param[in] src    Source data to copy
 * @param[in] size   Number of bytes to copy
 * @return Pointer to copied data, or NULL on failure
 */
void *ln_arena_memdup(ln_arena_t *arena, const void *src, size_t size);

/*============================================================================
 * Reset and Backtracking
 *============================================================================*/

/**
 * @brief Reset the arena for reuse.
 *
 * Returns the arena to its initial empty state. All allocations become
 * invalid. The underlying memory is retained for reuse.
 *
 * This is the primary mechanism for per-message arena reuse:
 * @code
 *   for (each message) {
 *       ln_arena_reset(&arena);
 *       // ... parse message, allocate fields ...
 *   }
 * @endcode
 *
 * @param[in,out] arena  Arena to reset
 *
 * @note Peak usage statistic is preserved across resets.
 * @note This is O(1) - does not touch the memory.
 */
void ln_arena_reset(ln_arena_t *arena);

/**
 * @brief Save current arena position for later restore.
 *
 * Creates a checkpoint that can be restored with ln_arena_restore().
 * Used for backtracking during parsing when a parse path fails.
 *
 * @param[in]  arena  Arena to save state from
 * @param[out] mark   Mark structure to save state into
 *
 * @note Marks can be nested (save multiple marks, restore in reverse order).
 */
void ln_arena_save(const ln_arena_t *arena, ln_arena_mark_t *mark);

/**
 * @brief Restore arena to a previously saved position.
 *
 * All allocations made after the mark was saved become invalid.
 * The arena's used pointer is reset to the marked position.
 *
 * @param[in,out] arena  Arena to restore
 * @param[in]     mark   Mark to restore to
 *
 * @note The mark must have been created from the same arena.
 * @note Restoring invalidates any marks created after this one.
 */
void ln_arena_restore(ln_arena_t *arena, const ln_arena_mark_t *mark);

/*============================================================================
 * Query Functions
 *============================================================================*/

/**
 * @brief Get remaining capacity in the arena.
 *
 * @param[in] arena  Arena to query
 * @return Number of bytes available for allocation
 */
static inline size_t
ln_arena_available(const ln_arena_t *arena)
{
	return arena ? arena->capacity - arena->used : 0;
}

/**
 * @brief Get current usage of the arena.
 *
 * @param[in] arena  Arena to query
 * @return Number of bytes currently allocated
 */
static inline size_t
ln_arena_used(const ln_arena_t *arena)
{
	return arena ? arena->used : 0;
}

/**
 * @brief Get peak usage of the arena.
 *
 * Returns the maximum `used` value seen since arena creation.
 * Useful for tuning arena capacity.
 *
 * @param[in] arena  Arena to query
 * @return Peak number of bytes allocated
 */
static inline size_t
ln_arena_peak(const ln_arena_t *arena)
{
	return arena ? arena->peak : 0;
}

/**
 * @brief Get total capacity of the arena.
 *
 * @param[in] arena  Arena to query
 * @return Total capacity in bytes
 */
static inline size_t
ln_arena_capacity(const ln_arena_t *arena)
{
	return arena ? arena->capacity : 0;
}

/**
 * @brief Check if arena has enough space for an allocation.
 *
 * @param[in] arena  Arena to check
 * @param[in] size   Desired allocation size
 * @return true if allocation would succeed, false otherwise
 *
 * @note Does not account for alignment padding.
 */
static inline bool
ln_arena_has_space(const ln_arena_t *arena, size_t size)
{
	return arena && (arena->capacity - arena->used) >= size;
}

/**
 * @brief Get detailed arena statistics.
 *
 * @param[in]  arena  Arena to query
 * @param[out] stats  Statistics structure to fill
 */
void ln_arena_get_stats(const ln_arena_t *arena, ln_arena_stats_t *stats);

/*============================================================================
 * Utility Macros
 *============================================================================*/

/**
 * @brief Allocate a typed object from the arena.
 *
 * Example:
 * @code
 *   my_struct_t *obj = LN_ARENA_NEW(arena, my_struct_t);
 * @endcode
 */
#define LN_ARENA_NEW(arena, type) \
	((type *)ln_arena_alloc((arena), sizeof(type)))

/**
 * @brief Allocate an array of typed objects from the arena.
 *
 * Example:
 * @code
 *   int *array = LN_ARENA_ARRAY(arena, int, 100);
 * @endcode
 */
#define LN_ARENA_ARRAY(arena, type, count) \
	((type *)ln_arena_alloc_array((arena), (count), sizeof(type)))

/**
 * @brief Allocate an aligned typed object from the arena.
 *
 * Example:
 * @code
 *   __m128i *vec = LN_ARENA_NEW_ALIGNED(arena, __m128i, 16);
 * @endcode
 */
#define LN_ARENA_NEW_ALIGNED(arena, type, align) \
	((type *)ln_arena_alloc_aligned((arena), sizeof(type), (align)))

#ifdef __cplusplus
}
#endif

#endif /* LIBLOGNORM_TURBO_ARENA_H_INCLUDED */
