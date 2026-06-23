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
	OP_SKIP_SPACE   = 0x40,  /**< Skip whitespace (0+) */
	OP_SKIP_SPACE1  = 0x41,  /**< Skip whitespace (1+, fail if none) */
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
		
		/* Field with delimiter */
		struct {
			char     name[56]; /**< Field name */
			uint8_t  delim;    /**< Delimiter char */
			uint8_t  ass;      /**< Assignator char (for name-value-list) */
			uint8_t  ignore_ws;/**< name-value-list: trim surrounding whitespace */
			uint8_t  _pad[1];
		} char_to;

		/* Static key-value pair (for OP_STATIC_FIELD) */
		struct {
			char     key[30];  /**< Field name, null-terminated */
			char     val[30];  /**< Field value, null-terminated */
		} kv;
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
	 * inline (security audit #6 — VM also re-validates len <= inline size). */
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
		/* Reserve a NUL terminator (security audit #6). name[56]. */
		for (int j = 0; j < 56 - 1 && name[j]; j++)
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
		/* Reserve a NUL terminator (security audit #6). name[56]. */
		for (int j = 0; j < 56 - 1 && name[j]; j++)
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
