/**
 * @file turbo_result_fast.h
 * @brief Optimized result structure for maximum performance
 *//*
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_TURBO_RESULT_FAST_H_INCLUDED
#define	LIBLOGNORM_TURBO_RESULT_FAST_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Public, opaque API: the ln_fast_result_t typedef, the ln_ftype_t field-type
 * enum and the LN_FFIELD_NESTED flag are defined there. This header completes
 * the (otherwise opaque) struct and adds the internal-only builders. */
#include "lognorm-turbo.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Configuration
 *============================================================================*/

/** Maximum fields per result */
#define LN_FAST_MAX_FIELDS 64

/** Maximum tags per result */
#define LN_FAST_MAX_TAGS 16

/** Inline string size (fits in cache line with field metadata) */
#define LN_FAST_INLINE_SIZE 48

/** Tag hash table size (power of 2) */
#define LN_FAST_TAG_HASH_SIZE 32

/*============================================================================
 * Field Structure (64 bytes - cache line aligned)
 *============================================================================*/

typedef struct {
	const char *name;       /* Field name (static or arena) - 8 bytes */
	uint16_t    name_len;   /* Name length - 2 bytes */
	uint8_t     type;       /* ln_ftype_t - 1 byte */
	uint8_t     flags;      /* Flags - 1 byte */
	uint32_t    _pad;       /* Padding - 4 bytes */
	union {                 /* 48 bytes */
		struct {
			const char *ptr;
			uint32_t    len;
			uint32_t    _pad;
		} str;              /* External string - 16 bytes */
		char    inl[LN_FAST_INLINE_SIZE]; /* Inline string - 48 bytes */
		int64_t i;          /* Integer - 8 bytes */
		double  d;          /* Double - 8 bytes */
		bool    b;          /* Boolean - 1 byte */
	} v;
} ln_fast_field_t;

_Static_assert(sizeof(ln_fast_field_t) == 64, "Field must be 64 bytes");

/* Field flags (LN_FFIELD_NESTED 0x04 is public, see lognorm-turbo.h) */
#define LN_FFIELD_STATIC_NAME 0x01  /* Name is static (don't free) */
#define LN_FFIELD_STATIC_VAL  0x02  /* Value is static (don't free) */

/**
 * @brief Check if a field name contains a dot (indicating nested object).
 * Returns LN_FFIELD_NESTED if dotted, 0 otherwise.
 */
static inline uint8_t
ln_ffield_detect_nested(const char *name, uint16_t name_len)
{
	for (uint16_t i = 0; i < name_len; i++) {
		if (name[i] == '.') return LN_FFIELD_NESTED;
	}
	return 0;
}

/*============================================================================
 * Tag Hash Entry
 *============================================================================*/

typedef struct {
	const char *tag;        /* Tag string (static) */
	uint32_t    hash;       /* Pre-computed hash */
} ln_tag_entry_t;

/*============================================================================
 * Fast Result Structure
 *============================================================================*/

/* Completes the opaque ln_fast_result_t declared in lognorm-turbo.h. */
struct ln_fast_result_s {
	/* Fields array */
	ln_fast_field_t fields[LN_FAST_MAX_FIELDS];
	uint8_t         n_fields;
	
	/* Tags with hash-based dedup */
	ln_tag_entry_t  tags[LN_FAST_MAX_TAGS];
	uint8_t         tag_hash[LN_FAST_TAG_HASH_SIZE]; /* Quick lookup bitmap */
	uint8_t         n_tags;
	
	/* Match info */
	const char     *rule_id;
	uint8_t         flags;
	
	/* Original message (for unparsed-data on partial match) */
	const char     *original;
	uint32_t        original_len;
	
	/* Arena for any overflow allocations */
	void           *arena;
};

/* Result flags */
#define LN_FRESULT_MATCHED   0x01
#define LN_FRESULT_PARTIAL   0x02
#define LN_FRESULT_HAS_ORIG  0x04

/*============================================================================
 * Fast Hash Function (FNV-1a)
 *============================================================================*/

static inline uint32_t
ln_fast_hash(const char *str)
{
	uint32_t h = 2166136261u;
	while (*str) {
		h ^= (uint8_t)*str++;
		h *= 16777619u;
	}
	return h;
}

static inline uint32_t
ln_fast_hash_n(const char *str, size_t len)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < len; i++) {
		h ^= (uint8_t)str[i];
		h *= 16777619u;
	}
	return h;
}

/*============================================================================
 * Inline Operations
 *============================================================================*/

static inline void
ln_fast_result_init(ln_fast_result_t *r, void *arena)
{
	memset(r, 0, sizeof(*r));
	r->arena = arena;
}

static inline void
ln_fast_result_clear(ln_fast_result_t *r)
{
	r->n_fields = 0;
	r->n_tags = 0;
	memset(r->tag_hash, 0, sizeof(r->tag_hash));
	r->rule_id = NULL;
	r->flags = 0;
	r->original = NULL;
	r->original_len = 0;
}

/**
 * @brief Add string field with static name (no copy).
 *
 * FAST PATH: For known field names, pass static string literal.
 */
static inline int
ln_fast_add_string_static(ln_fast_result_t *r,
						  const char *name, uint16_t name_len,
						  const char *val, uint32_t val_len)
{
	ln_fast_field_t *f;
	if (r->n_fields >= LN_FAST_MAX_FIELDS) return -1;

	f = &r->fields[r->n_fields++];
	f->name = name;
	f->name_len = name_len;
	f->flags = LN_FFIELD_STATIC_NAME | LN_FFIELD_STATIC_VAL
			 | ln_ffield_detect_nested(name, name_len);

	/* Inline small strings */
	if (val_len < LN_FAST_INLINE_SIZE) {
		f->type = LN_FTYPE_STRING_INLINE;
		memcpy(f->v.inl, val, val_len);
		f->v.inl[val_len] = '\0';
	} else {
		f->type = LN_FTYPE_STRING;
		f->v.str.ptr = val;
		f->v.str.len = val_len;
	}
	
	return 0;
}

/**
 * @brief Add integer field with static name.
 */
static inline int
ln_fast_add_int_static(ln_fast_result_t *r,
					   const char *name, uint16_t name_len,
					   int64_t val)
{
	ln_fast_field_t *f;
	if (r->n_fields >= LN_FAST_MAX_FIELDS) return -1;

	f = &r->fields[r->n_fields++];
	f->name = name;
	f->name_len = name_len;
	f->type = LN_FTYPE_INT;
	f->flags = LN_FFIELD_STATIC_NAME
			 | ln_ffield_detect_nested(name, name_len);
	f->v.i = val;
	
	return 0;
}

/**
 * @brief Add double field with static name.
 */
static inline int
ln_fast_add_double_static(ln_fast_result_t *r,
						  const char *name, uint16_t name_len,
						  double val)
{
	ln_fast_field_t *f;
	if (r->n_fields >= LN_FAST_MAX_FIELDS) return -1;

	f = &r->fields[r->n_fields++];
	f->name = name;
	f->name_len = name_len;
	f->type = LN_FTYPE_DOUBLE;
	f->flags = LN_FFIELD_STATIC_NAME
			 | ln_ffield_detect_nested(name, name_len);
	f->v.d = val;
	
	return 0;
}

/**
 * @brief Add tag with O(1) dedup check.
 *
 * Tags are assumed to be static strings (compile-time constants).
 */
static inline int
ln_fast_add_tag(ln_fast_result_t *r, const char *tag)
{
	uint32_t h;
	uint8_t slot;
	if (r->n_tags >= LN_FAST_MAX_TAGS) return -1;

	h = ln_fast_hash(tag);
	slot = h & (LN_FAST_TAG_HASH_SIZE - 1);
	
	/* Quick bitmap check for likely-unique */
	if (r->tag_hash[slot]) {
		/* Possible collision - linear scan existing tags */
		for (uint8_t i = 0; i < r->n_tags; i++) {
			if (r->tags[i].hash == h && r->tags[i].tag == tag) {
				return 0;  /* Already present (pointer equality for static) */
			}
		}
	}
	
	/* Add new tag */
	r->tag_hash[slot] = 1;
	r->tags[r->n_tags].tag = tag;
	r->tags[r->n_tags].hash = h;
	r->n_tags++;
	
	return 0;
}

/**
 * @brief Set rule ID (static string).
 */
static inline void
ln_fast_set_rule_id(ln_fast_result_t *r, const char *rule_id)
{
	r->rule_id = rule_id;
	r->flags |= LN_FRESULT_MATCHED;
}

/**
 * @brief Set original message pointer.
 */
static inline void
ln_fast_set_original(ln_fast_result_t *r, const char *msg, uint32_t len)
{
	r->original = msg;
	r->original_len = len;
	r->flags |= LN_FRESULT_HAS_ORIG;
}

/**
 * @brief Check if result has a specific tag.
 */
static inline int
ln_fast_has_tag(const ln_fast_result_t *r, const char *tag)
{
	if (!r || !tag) return 0;
	
	for (int i = 0; i < r->n_tags; i++) {
		if (r->tags[i].tag && strcmp(r->tags[i].tag, tag) == 0) {
			return 1;
		}
	}
	return 0;
}

/*============================================================================
 * JSON Serialization (Optimized)
 *============================================================================*/

/**
 * @brief Estimate JSON output size.
 */
size_t ln_fast_json_estimate(const ln_fast_result_t *r);

/**
 * @brief Serialize to JSON string.
 *
 * @param r      Result to serialize
 * @param buf    Output buffer
 * @param buflen Buffer size
 * @param outlen Receives actual length (may be NULL)
 * @return 0 on success, -1 if buffer too small
 *
 * This function creates nested objects from dotted field names:
 * "timestamp_netscaler.day" -> {"timestamp_netscaler": {"day": ...}}
 */
int ln_fast_to_json(const ln_fast_result_t *r,
					char *buf, size_t buflen, size_t *outlen);

/**
 * @brief Allocating version.
 */
int ln_fast_to_json_alloc(const ln_fast_result_t *r,
						  char **json_str, size_t *json_len);

#ifdef __cplusplus
}
#endif

#endif /* LIBLOGNORM_TURBO_RESULT_FAST_H_INCLUDED */
