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

/** Maximum fields per result.
 *
 * Real-world CSV-shaped rulebases go well past 64: a PAN-OS TRAFFIC record is
 * 97 columns, and an ECS mapping adds annotations on top. Parse-time writes
 * stop at n_fields. A snapshot still allocates the full array (arena_data[]
 * sits after sizeof(ln_fast_result_t)); only the used prefix is copied.
 * Anything beyond this is not truncated silently: the JSON string path
 * refuses the result and the caller falls back to the recursive walker.
 * n_fields is uint8_t, so this cap must stay at or below 255. */
#define LN_FAST_MAX_FIELDS 128

/** Maximum tags per result */
#define LN_FAST_MAX_TAGS 16

/** Maximum JSON object nesting depth for turbo output. Shared by the VM
 * flattener (turbo_vm.c) and the JSON serializer (turbo_json_impl.c) so the
 * two never disagree on how deep a dotted field name may nest. */
#define LN_JSON_MAX_DEPTH 64

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

/* Field flags. LN_FFIELD_NESTED 0x04 and LN_FFIELD_RAW_JSON 0x08 are public
 * (see lognorm-turbo.h, included above); only the STATIC_* bits are private. */
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
#define LN_FRESULT_TRUNCATED 0x08  /* a field or tag was dropped: the result hit
									  LN_FAST_MAX_FIELDS / LN_FAST_MAX_TAGS */

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

static inline int
ln_fast_name_taken(const ln_fast_result_t *r, const char *name, uint16_t name_len)
{
	uint8_t i;

	for (i = 0; i < r->n_fields; i++) {
		const ln_fast_field_t *f = &r->fields[i];

		if (f->name_len != name_len)
			continue;
		if (f->name == name)
			return 1;
		if (f->name != NULL && memcmp(f->name, name, name_len) == 0)
			return 1;
	}
	return 0;
}

static inline void
ln_fast_store_string(ln_fast_field_t *f,
					 const char *name, uint16_t name_len,
					 const char *val, uint32_t val_len)
{
	f->name = name;
	f->name_len = name_len;
	f->flags = LN_FFIELD_STATIC_NAME | LN_FFIELD_STATIC_VAL
			 | ln_ffield_detect_nested(name, name_len);
	if (val_len < LN_FAST_INLINE_SIZE) {
		f->type = LN_FTYPE_STRING_INLINE;
		memcpy(f->v.inl, val, val_len);
		f->v.inl[val_len] = '\0';
	} else {
		f->type = LN_FTYPE_STRING;
		f->v.str.ptr = val;
		f->v.str.len = val_len;
	}
}

/**
 * @brief Add string field with static name (no copy).
 *
 * FAST PATH: For known field names, pass static string literal.
 * Appends even when the name is already present. Sequential parsers
 * that share a name (``%f:ipv4%:%f:number%``) must all land so a
 * last-wins lookup matches the walker. Use ln_fast_set_string_static
 * when a later binding should overwrite in place (CEF, annotations).
 */

static inline int
ln_fast_add_string_static(ln_fast_result_t *r,
						  const char *name, uint16_t name_len,
						  const char *val, uint32_t val_len)
{
	if (r->n_fields >= LN_FAST_MAX_FIELDS) { r->flags |= LN_FRESULT_TRUNCATED; return -1; }
	ln_fast_store_string(&r->fields[r->n_fields++], name, name_len, val, val_len);
	return 0;
}

/*
 * Replace the value of an existing field, or add it when absent (last-wins).
 *
 * CEF extensions and annotations use json_object_object_add, which overwrites
 * a name already present. One scan: a hit stores in place, a miss appends
 * without going back through first-wins.
 *
 * @return 0 on success, -1 if the result is full.
 */
static inline int
ln_fast_set_string_static(ln_fast_result_t *r,
						  const char *name, uint16_t name_len,
						  const char *val, uint32_t val_len)
{
	uint8_t i;

	for (i = 0; i < r->n_fields; i++) {
		ln_fast_field_t *const f = &r->fields[i];

		if (f->name_len != name_len)
			continue;
		if (f->name != name
			&& (f->name == NULL || memcmp(f->name, name, name_len) != 0))
			continue;
		ln_fast_store_string(f, name, name_len, val, val_len);
		return 0;
	}
	if (r->n_fields >= LN_FAST_MAX_FIELDS) { r->flags |= LN_FRESULT_TRUNCATED; return -1; }
	ln_fast_store_string(&r->fields[r->n_fields++], name, name_len, val, val_len);
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
	if (r->n_fields >= LN_FAST_MAX_FIELDS) { r->flags |= LN_FRESULT_TRUNCATED; return -1; }

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
 * @brief Add boolean field with static name.
 *
 * The JSON flattener used to store booleans as the strings "true"/"false",
 * which the serializer then quoted; the standard parser emits a JSON boolean.
 */
static inline int
ln_fast_add_bool_static(ln_fast_result_t *r,
						const char *name, uint16_t name_len,
						int val)
{
	ln_fast_field_t *f;
	if (r->n_fields >= LN_FAST_MAX_FIELDS) { r->flags |= LN_FRESULT_TRUNCATED; return -1; }

	f = &r->fields[r->n_fields++];
	f->name = name;
	f->name_len = name_len;
	f->type = LN_FTYPE_BOOL;
	f->flags = LN_FFIELD_STATIC_NAME
			 | ln_ffield_detect_nested(name, name_len);
	f->v.b = val ? 1 : 0;

	return 0;
}

/**
 * @brief Add JSON null field with static name.
 *
 * A null member is part of the document: dropping it makes the field absent
 * rather than null, which is a different result from the standard parser's.
 */
static inline int
ln_fast_add_null_static(ln_fast_result_t *r,
						const char *name, uint16_t name_len)
{
	ln_fast_field_t *f;
	if (r->n_fields >= LN_FAST_MAX_FIELDS) { r->flags |= LN_FRESULT_TRUNCATED; return -1; }

	f = &r->fields[r->n_fields++];
	f->name = name;
	f->name_len = name_len;
	f->type = LN_FTYPE_NULL;
	f->flags = LN_FFIELD_STATIC_NAME
			 | ln_ffield_detect_nested(name, name_len);
	f->v.i = 0;

	return 0;
}

static inline int
ln_fast_set_null_static(ln_fast_result_t *r,
						const char *name, uint16_t name_len)
{
	uint8_t i;

	for (i = 0; i < r->n_fields; i++) {
		ln_fast_field_t *const f = &r->fields[i];

		if (f->name_len != name_len)
			continue;
		if (f->name != name
			&& (f->name == NULL || memcmp(f->name, name, name_len) != 0))
			continue;
		f->name = name;
		f->name_len = name_len;
		f->type = LN_FTYPE_NULL;
		f->flags = LN_FFIELD_STATIC_NAME
				 | ln_ffield_detect_nested(name, name_len);
		f->v.i = 0;
		return 0;
	}
	if (r->n_fields >= LN_FAST_MAX_FIELDS) { r->flags |= LN_FRESULT_TRUNCATED; return -1; }
	{
		ln_fast_field_t *f = &r->fields[r->n_fields++];
		f->name = name;
		f->name_len = name_len;
		f->type = LN_FTYPE_NULL;
		f->flags = LN_FFIELD_STATIC_NAME
				 | ln_ffield_detect_nested(name, name_len);
		f->v.i = 0;
	}
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
	if (r->n_fields >= LN_FAST_MAX_FIELDS) { r->flags |= LN_FRESULT_TRUNCATED; return -1; }

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
 * @brief Add a raw JSON value with static name.
 *
 * The value is stored as a string and tagged LN_FFIELD_RAW_JSON so the
 * serializer emits it verbatim (no quotes, no escaping). @p val must already be
 * a well-formed JSON value; the caller is responsible for validating it.
 */
static inline int
ln_fast_add_rawjson_static(ln_fast_result_t *r,
						   const char *name, uint16_t name_len,
						   const char *val, uint32_t val_len)
{
	if (ln_fast_add_string_static(r, name, name_len, val, val_len) != 0)
		return -1;
	r->fields[r->n_fields - 1].flags |= LN_FFIELD_RAW_JSON;
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
	if (r->n_tags >= LN_FAST_MAX_TAGS) { r->flags |= LN_FRESULT_TRUNCATED; return -1; }

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
