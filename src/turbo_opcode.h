/**
 * @file turbo_opcode.h
 * @brief VM instruction set for TurboVM
 *//*
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_TURBO_OPCODE_H_INCLUDED
#define	LIBLOGNORM_TURBO_OPCODE_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/** Fixed instruction size (64 bytes) */
#define LN_INSTR_SIZE 64

/** Max inline data (literal or field name) */
#define LN_INSTR_MAX_INLINE 60

/*============================================================================
 * Opcodes
 *============================================================================*/

typedef enum {
	/*=== Control (0x00-0x0F) ===*/
	OP_HALT         = 0x00,  /**< Stop, no match */
	OP_MATCH        = 0x01,  /**< Rule matched, payload=rule_id */
	OP_JUMP         = 0x02,  /**< Unconditional jump, offset in payload */
	OP_FORK         = 0x03,  /**< Try path, alt on fail, offset=alt */
	OP_FAIL         = 0x04,  /**< Force backtrack */
	OP_CALL         = 0x05,  /**< Call subroutine */
	OP_RET          = 0x06,  /**< Return from subroutine */
	
	/*=== Literals (0x10-0x1F) ===*/
	OP_LITERAL      = 0x10,  /**< Match literal (inline), len in header */
	OP_LITERAL_EXT  = 0x11,  /**< Match literal (external ptr) */
	OP_LITERAL_CI   = 0x12,  /**< Match literal case-insensitive */
	OP_CHAR         = 0x13,  /**< Match single char */
	OP_ANY          = 0x14,  /**< Match any char (advance 1) */
	OP_CHARSET      = 0x15,  /**< Match char from set (external bitmap) */
	
	/*=== Fields (0x20-0x3F) ===*/
	OP_FIELD_WORD   = 0x20,  /**< Extract word (whitespace-delimited) */
	OP_FIELD_INT    = 0x21,  /**< Extract signed integer */
	OP_FIELD_UINT   = 0x22,  /**< Extract unsigned integer */
	OP_FIELD_FLOAT  = 0x23,  /**< Extract floating point */
	OP_FIELD_IPV4   = 0x24,  /**< Extract IPv4 address */
	OP_FIELD_IPV6   = 0x25,  /**< Extract IPv6 address */
	OP_FIELD_HEX    = 0x26,  /**< Extract hex number */
	OP_FIELD_QUOTED = 0x27,  /**< Extract quoted string */
	OP_FIELD_CHAR_TO= 0x28,  /**< Extract until delimiter char */
	OP_FIELD_STR_TO = 0x29,  /**< Extract until delimiter string */
	OP_FIELD_REST   = 0x2A,  /**< Extract rest of line */
	OP_FIELD_JSON   = 0x2B,  /**< Extract JSON value */
	OP_FIELD_MAC    = 0x2C,  /**< Extract MAC address */
	OP_FIELD_DATE   = 0x2D,  /**< Extract date/timestamp */
	OP_FIELD_REGEX  = 0x2E,  /**< Extract via regex (external) */
	OP_FIELD_NAME_VALUE = 0x2F, /**< Parse name=value pairs (name-value-list) */
	
	/*=== Skipping (0x40-0x4F) ===*/
	OP_SKIP_SPACE   = 0x40,  /**< Skip whitespace, 1+. This is what the
							  *   %field:whitespace% parser compiles to and
							  *   the standard parser requires at least one
							  *   space there, so the VM fails on an empty
							  *   run.  It is NOT a 0+ skip, whatever the
							  *   name suggests. */
	OP_SKIP_SPACE1  = 0x41,  /**< Skip whitespace, 1+, and never store it.
							  *   That is the one difference from
							  *   OP_SKIP_SPACE, which honours
							  *   LN_INSTR_F_STORE and keeps the matched run
							  *   as a field value.  The compiler does not
							  *   emit this opcode; the dispatch table and
							  *   disassembler still carry it. */
	OP_SKIP_N       = 0x42,  /**< Skip N bytes */
	OP_SKIP_TO      = 0x43,  /**< Skip to char (not including) */
	OP_SKIP_PAST    = 0x44,  /**< Skip past char (including) */
	OP_SKIP_LINE    = 0x45,  /**< Skip to end of line */
	
	/*=== Tags (0x50-0x5F) ===*/
	OP_TAG          = 0x50,  /**< Add tag, payload=tag name */
	OP_RULE_ID      = 0x51,  /**< Set rule ID */
	OP_STATIC_FIELD = 0x52,  /**< Add static key=value field from annotation.
							  *   Layout: data.kv.key[30] + data.kv.val[30],
							  *   aux = key length. Value is null-terminated. */
	
	/*=== Field Context (0x58-0x5F) - for ".." substitution ===*/
	OP_CTX_PUSH     = 0x58,  /**< Push field name context for custom types */
	OP_CTX_POP      = 0x59,  /**< Pop field name context */
	OP_CTX_NEST     = 0x5A,  /**< Start nested object (for multi-field types) */
	OP_CTX_UNNEST   = 0x5B,  /**< End nested object */
	
	/*=== Assertions (0x60-0x6F) ===*/
	OP_ASSERT_CHAR  = 0x60,  /**< Assert next char equals (no consume) */
	OP_ASSERT_END   = 0x61,  /**< Assert at end of input */
	OP_ASSERT_START = 0x62,  /**< Assert at start of input */
	
	/*=== Special (0x70-0x7F) ===*/
	OP_SYSLOG_PRI   = 0x70,  /**< Parse <PRI> field */
	OP_SYSLOG_TS    = 0x71,  /**< Parse syslog timestamp */
	OP_CEF_HDR      = 0x72,  /**< Parse CEF header */
	OP_V2_IPTABLES  = 0x73,  /**< Parse iptables name=value pairs */
	OP_CEE_SYSLOG   = 0x74,  /**< Parse CEE-syslog (@cee: + JSON) */
	OP_CHECKPOINT_LEA = 0x75, /**< Parse Checkpoint LEA name: value; */
	OP_FIELD_TIME   = 0x76,  /**< Extract a wall-clock time, HH:MM:SS.
                              *   aux selects the grammar: 0 reads a 24-hour
                              *   clock, 1 a 12-hour one. */

	/*=== Debug (0xF0-0xFF) ===*/
	OP_NOP          = 0xF0,  /**< No operation */
	OP_DEBUG        = 0xFE,  /**< Debug breakpoint */
	OP_INVALID      = 0xFF,  /**< Invalid marker */
} ln_opcode_t;

/*============================================================================
 * Instruction Structure
 *============================================================================*/

/**
 * @brief VM instruction (64 bytes fixed).
 *
 * Layout:
 *   [0]      opcode (ln_opcode_t)
 *   [1]      flags
 *   [2-3]    aux (length, char, etc.)
 *   [4-63]   payload (inline data or structured)
 */
typedef struct {
	uint8_t  op;              /**< Opcode */
	uint8_t  flags;           /**< Flags */
	uint16_t aux;             /**< Auxiliary (length, etc.) */
	union {
		/* Raw bytes for inline data */
		char     str[60];     /**< Inline string (literal, name, etc.) */
		uint8_t  bytes[60];   /**< Raw byte access */
		
		/* Jump/fork target */
		struct {
			int32_t  offset;  /**< Relative instruction offset */
			int32_t  _pad[14];
		} jump;
		
		/* Field with delimiter.
		 *
		 * char-to and char-sep terminate on a SET of characters, not on one.
		 * A single-character set is the common case and rides in `delim`, so
		 * the scan stays a plain byte search. A larger set is interned in the
		 * program string pool and `set_off` points at it, with aux carrying
		 * its length; the pool copy is NUL-terminated, which is what the
		 * set-matching scan expects. */
		struct {
			char     name[52]; /**< Field name */
			uint32_t set_off;  /**< String-pool offset of the terminator set,
			                     *   valid only with LN_INSTR_F_CHARSET */
			uint8_t  delim;    /**< Delimiter char (single-character set) */
			uint8_t  ass;      /**< Assignator char (for name-value-list) */
			uint8_t  ignore_ws;/**< name-value-list: trim surrounding whitespace */
			uint8_t  _pad[1];
		} char_to;

		/* Field with a multi-byte delimiter string (OP_FIELD_STR_TO).
		 * The delimiter lives in the program string pool because it does
		 * not fit next to the field name; aux carries its length. */
		struct {
			char     name[56]; /**< Field name */
			uint32_t delim_off;/**< String-pool offset of the delimiter */
		} str_to;

		/* Static key-value pair (for OP_STATIC_FIELD) */
		struct {
			char     key[30];  /**< Field name, null-terminated */
			char     val[30];  /**< Field value, null-terminated */
		} kv;

		/* Static key-value pair too long to store inline: both strings
		 * live in the program string pool.  Selected by LN_INSTR_F_KV_POOL;
		 * aux carries the key length, as in the inline form. */
		struct {
			uint32_t key_off;  /**< String-pool offset of the key */
			uint32_t val_off;  /**< String-pool offset of the value */
			uint32_t val_len;  /**< Value length */
			uint8_t  _pad[48];
		} kv_pool;
	} data;
} ln_instr_t;

_Static_assert(sizeof(ln_instr_t) == LN_INSTR_SIZE, 
			   "Instruction must be 64 bytes");

/*============================================================================
 * Instruction Flags
 *============================================================================*/

#define LN_INSTR_F_OPTIONAL  0x01  /**< Optional (don't fail if no match) */
#define LN_INSTR_F_GREEDY    0x02  /**< Greedy matching */
#define LN_INSTR_F_STORE     0x04  /**< Store extracted field */
#define LN_INSTR_F_CASE_INS  0x08  /**< Case-insensitive */
#define LN_INSTR_F_NAME_POOL 0x10  /**< Field/context name lives in the program
									  string pool: the opcode's inline name buffer
									  holds a uint32 pool offset instead of the
									  name. Lets turbo carry names longer than the
									  inline buffer, like the standard parser. */
#define LN_INSTR_F_NUMERIC   0x20  /**< Numeric field parsed with format="number":
									  emit a native JSON number instead of a string,
									  matching the standard parser. */
#define LN_INSTR_F_DATE_FMT  0x80  /**< Date parser with format="timestamp-unix"
									  or "timestamp-unix-ms". aux carries the
									  FMT_MODE in its low byte and 0=rfc3164 /
									  1=rfc5424 in its high byte. */
/*
 * OP_STATIC_FIELD only ever carries LN_INSTR_F_KV_POOL, so the remaining bits
 * are free for it to reuse. This one shares its value with
 * LN_INSTR_F_OPTIONAL, which has no meaning for a field that always stores.
 */
#define LN_INSTR_F_CHARSEP   0x08  /**< OP_FIELD_CHAR_TO: char-sep semantics.
                                    *   char-sep always matches: with no
                                    *   terminator in range it takes the rest,
                                    *   and an empty value is valid. char-to
                                    *   refuses both. Shares its value with
                                    *   LN_INSTR_F_CASE_INS, which this opcode
                                    *   never carries. */
#define LN_INSTR_F_CHARSET   0x02  /**< OP_FIELD_CHAR_TO: the terminator set does
                                    *   not fit in `delim` and lives in the
                                    *   program string pool at data.char_to
                                    *   .set_off, aux holding its length.
                                    *   Shares its value with
                                    *   LN_INSTR_F_GREEDY, which this opcode
                                    *   never carries. */
#define LN_INSTR_F_ANNOT     0x01  /**< OP_STATIC_FIELD: the field was resolved
                                    *   from an annotation. The standard parser
                                    *   applies an annotation with a replacing
                                    *   add, so this one overwrites a field of
                                    *   the same name rather than appending a
                                    *   second entry under it. */
#define LN_INSTR_F_KV_POOL   0x40  /**< OP_STATIC_FIELD: the key and the value are
									  too long for the inline buffers and live in
									  the string pool instead (data.kv_pool).
									  Lets turbo carry annotation values of any
									  length, matching the standard parser. */

/*============================================================================
 * Instruction Builders
 *============================================================================*/

/** Create HALT instruction */
static inline ln_instr_t ln_i_halt(void) {
	ln_instr_t i = {0};
	i.op = OP_HALT;
	return i;
}

/** Create MATCH instruction */
static inline ln_instr_t ln_i_match(const char *rule) {
	ln_instr_t i = {0};
	i.op = OP_MATCH;
	if (rule) {
		/* Reserve a NUL terminator: copy at most size-1 bytes so the
		 * inline string is always NUL-terminated (security audit #6). */
		for (int j = 0; j < LN_INSTR_MAX_INLINE - 1 && rule[j]; j++)
			i.data.str[j] = rule[j];
	}
	return i;
}

/** Create LITERAL instruction (inline) */
static inline ln_instr_t ln_i_literal(const char *lit, uint16_t len) {
	ln_instr_t i = {0};
	i.op = OP_LITERAL;
	/* Clamp the inline copy to the buffer; aux carries the match length
	 * the VM compares, so it must never exceed what we actually stored
	 * inline (security audit #6; VM also re-validates len <= inline size). */
	if (len > LN_INSTR_MAX_INLINE)
		len = LN_INSTR_MAX_INLINE;
	i.aux = len;
	for (uint16_t j = 0; j < len && j < LN_INSTR_MAX_INLINE; j++)
		i.data.str[j] = lit[j];
	return i;
}

/** Create CHAR instruction */
static inline ln_instr_t ln_i_char(char c) {
	ln_instr_t i = {0};
	i.op = OP_CHAR;
	i.data.str[0] = c;
	return i;
}

/** Create JUMP instruction */
static inline ln_instr_t ln_i_jump(int32_t offset) {
	ln_instr_t i = {0};
	i.op = OP_JUMP;
	i.data.jump.offset = offset;
	return i;
}

/** Create FORK instruction */
static inline ln_instr_t ln_i_fork(int32_t alt_offset) {
	ln_instr_t i = {0};
	i.op = OP_FORK;
	i.data.jump.offset = alt_offset;
	return i;
}

/** Create FAIL instruction */
static inline ln_instr_t ln_i_fail(void) {
	ln_instr_t i = {0};
	i.op = OP_FAIL;
	return i;
}

/** Create field instruction with name */
static inline ln_instr_t ln_i_field(ln_opcode_t op, const char *name) {
	ln_instr_t i = {0};
	i.op = op;
	i.flags = LN_INSTR_F_STORE;
	if (name) {
		/* Reserve a NUL terminator (security audit #6). */
		for (int j = 0; j < LN_INSTR_MAX_INLINE - 1 && name[j]; j++)
			i.data.str[j] = name[j];
	}
	return i;
}

/** Create FIELD_CHAR_TO instruction */
static inline ln_instr_t ln_i_field_char_to(const char *name, char delim) {
	ln_instr_t i = {0};
	i.op = OP_FIELD_CHAR_TO;
	i.flags = LN_INSTR_F_STORE;
	i.data.char_to.delim = (uint8_t)delim;
	if (name) {
		/* Reserve a NUL terminator (security audit #6). The bound comes
		 * from the array so it cannot drift from the layout. */
		for (size_t j = 0; j + 1 < sizeof(i.data.char_to.name) && name[j]; j++)
			i.data.char_to.name[j] = name[j];
	}
	return i;
}

/** Create FIELD_NAME_VALUE instruction */
static inline ln_instr_t ln_i_field_name_value(const char *name, char sep, char ass,
											   uint8_t ignore_ws) {
	ln_instr_t i = {0};
	i.op = OP_FIELD_NAME_VALUE;
	i.flags = LN_INSTR_F_STORE;
	i.data.char_to.delim = (uint8_t)sep;
	i.data.char_to.ass   = (uint8_t)ass;
	i.data.char_to.ignore_ws = ignore_ws;
	if (name) {
		/* Reserve a NUL terminator (security audit #6). The bound comes
		 * from the array so it cannot drift from the layout. */
		for (size_t j = 0; j + 1 < sizeof(i.data.char_to.name) && name[j]; j++)
			i.data.char_to.name[j] = name[j];
	}
	return i;
}

/** Create SKIP_SPACE instruction */
static inline ln_instr_t ln_i_skip_space(void) {
	ln_instr_t i = {0};
	i.op = OP_SKIP_SPACE;
	return i;
}

/** Create SKIP_N instruction */
static inline ln_instr_t ln_i_skip_n(uint16_t n) {
	ln_instr_t i = {0};
	i.op = OP_SKIP_N;
	i.aux = n;
	return i;
}

/** Create TAG instruction */
static inline ln_instr_t ln_i_tag(const char *tag) {
	ln_instr_t i = {0};
	i.op = OP_TAG;
	if (tag) {
		/* Reserve a NUL terminator (security audit #6). */
		for (int j = 0; j < LN_INSTR_MAX_INLINE - 1 && tag[j]; j++)
			i.data.str[j] = tag[j];
	}
	return i;
}

/** Create CTX_PUSH instruction */
static inline ln_instr_t ln_i_ctx_push(const char *name) {
	ln_instr_t i = {0};
	i.op = OP_CTX_PUSH;
	if (name) {
		/* Reserve a NUL terminator (security audit #6). */
		for (int j = 0; j < LN_INSTR_MAX_INLINE - 1 && name[j]; j++)
			i.data.str[j] = name[j];
	}
	return i;
}

/** Create CTX_POP instruction */
static inline ln_instr_t ln_i_ctx_pop(void) {
	ln_instr_t i = {0};
	i.op = OP_CTX_POP;
	return i;
}

/** Create NOP instruction */
static inline ln_instr_t ln_i_nop(void) {
	ln_instr_t i = {0};
	i.op = OP_NOP;
	return i;
}

/*============================================================================
 * Debug Helpers
 *============================================================================*/

/**
 * @brief Get opcode name.
 */
const char *ln_opcode_name(ln_opcode_t op);

/**
 * @brief Disassemble instruction to string.
 */
int ln_instr_disasm(const ln_instr_t *inst, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* LIBLOGNORM_TURBO_OPCODE_H_INCLUDED */
