/*
 * turbo.c -- liblognorm integration for TurboVM bytecode engine
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

/* liblognorm headers */
#include "liblognorm.h"
#include "lognorm.h"
#include "pdag.h"
#include "parser.h"
#include "annot.h"
#include "internal.h"

/* libfastjson */
#include <json.h>

/* Turbo headers */
#include "turbo.h"
#include "turbo_opcode.h"
#include "turbo_vm.h"
#include "turbo_result_fast.h"
#include "turbo_arena.h"
#include "turbo_simd.h"
#include "turbo_snapshot.h"
#include "turbo_json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/*============================================================================
 * Turbo Context Structure (OPTIMIZED)
 *============================================================================*/

struct ln_turbo_ctx_s {
	/* Compiled program */
	ln_instr_t     *code;
	uint32_t        code_len;
	uint32_t        code_cap;

	/* Arena for overflow allocations */
	ln_arena_t      arena;

	/* VM instance (reusable) */
	ln_vm_t         vm;

	/* FAST result structure (inline strings, static names) */
	ln_fast_result_t result;

	/* Pre-allocated JSON buffer */
	char           *json_buf;
	size_t          json_buf_cap;

	/* Statistics */
	ln_turbo_stats_t stats;

	/* Configuration */
	int             enabled;
	int             debug;
};

/*============================================================================
 * Parser ID Mapping
 *============================================================================*/

#define PRSID_LITERAL           0
#define PRSID_REPEAT            1
#define PRSID_RFC3164DATE       2
#define PRSID_RFC5424DATE       3
#define PRSID_NUMBER            4
#define PRSID_FLOAT             5
#define PRSID_HEXNUMBER         6
#define PRSID_KERNEL_TIMESTAMP  7
#define PRSID_WHITESPACE        8
#define PRSID_IPV4              9
#define PRSID_IPV6              10
#define PRSID_WORD              11
#define PRSID_ALPHA             12
#define PRSID_REST              13
#define PRSID_OPQUOTEDSTRING    14
#define PRSID_QUOTEDSTRING      15
#define PRSID_ISODATE           16
#define PRSID_TIME24HR          17
#define PRSID_TIME12HR          18
#define PRSID_DURATION          19
#define PRSID_CISCO_IFACE       20
#define PRSID_JSON              21
#define PRSID_CEE_SYSLOG        22
#define PRSID_MAC48             23
#define PRSID_CEF               24
#define PRSID_V2_IPTABLES       25
#define PRSID_NAMEVALUE         26
#define PRSID_CHECKPOINT        27
#define PRSID_STRINGTO          28
#define PRSID_CHARTO            29
#define PRSID_CHARSEP           30
#define PRSID_STRING            31

static ln_opcode_t
prsid_to_opcode(prsid_t prsid)
{
	switch (prsid) {
	case PRSID_LITERAL:        return OP_LITERAL;
	case PRSID_WORD:           return OP_FIELD_WORD;
	case PRSID_ALPHA:          return OP_FIELD_WORD;
	case PRSID_STRING:         return OP_FIELD_WORD;
	case PRSID_NUMBER:         return OP_FIELD_INT;
	case PRSID_FLOAT:          return OP_FIELD_FLOAT;
	case PRSID_HEXNUMBER:      return OP_FIELD_HEX;
	case PRSID_IPV4:           return OP_FIELD_IPV4;
	case PRSID_IPV6:           return OP_FIELD_IPV6;
	case PRSID_REST:           return OP_FIELD_REST;
	case PRSID_OPQUOTEDSTRING: return OP_FIELD_QUOTED;
	case PRSID_QUOTEDSTRING:   return OP_FIELD_QUOTED;
	case PRSID_WHITESPACE:     return OP_SKIP_SPACE;
	case PRSID_JSON:           return OP_FIELD_JSON;
	case PRSID_MAC48:          return OP_FIELD_MAC;
	case PRSID_CHARTO:         return OP_FIELD_CHAR_TO;
	case PRSID_CHARSEP:        return OP_FIELD_CHAR_TO;  /* Same behavior */
	case PRSID_STRINGTO:       return OP_FIELD_STR_TO;
	case PRSID_RFC3164DATE:    return OP_FIELD_DATE;
	case PRSID_RFC5424DATE:    return OP_FIELD_DATE;
	case PRSID_ISODATE:        return OP_FIELD_DATE;
	case PRSID_TIME24HR:       return OP_FIELD_DATE;
	case PRSID_TIME12HR:       return OP_FIELD_DATE;
	case PRSID_DURATION:       return OP_FIELD_WORD;     /* Parse as word */
	case PRSID_KERNEL_TIMESTAMP: return OP_FIELD_WORD;   /* Parse as word */
	case PRSID_CISCO_IFACE:    return OP_FIELD_WORD;     /* Parse as word */
	case PRSID_NAMEVALUE:      return OP_FIELD_NAME_VALUE;
	case PRSID_V2_IPTABLES:    return OP_V2_IPTABLES;
	case PRSID_CEE_SYSLOG:     return OP_CEE_SYSLOG;
	case PRSID_CEF:            return OP_CEF_HDR;
	case PRSID_CHECKPOINT:     return OP_CHECKPOINT_LEA;
	default:                   return OP_INVALID;
	}
}

/*============================================================================
 * Compiler State
 *============================================================================*/

typedef struct {
	ln_turbo_ctx_t *turbo;
	ln_ctx          ctx;
	int             n_rules;
	int             n_branches;
	int             max_depth;
	int             depth;
	int             in_custom_type;
	struct {
		uint32_t *offsets;
		int cap;
		int len;
	} visited;
} compiler_t;

/*============================================================================
 * Compiler Helpers
 *============================================================================*/

static uint32_t
emit(compiler_t *comp, ln_instr_t *instr)
{
	ln_turbo_ctx_t *turbo = comp->turbo;

	if (turbo->code_len >= turbo->code_cap) {
		uint32_t new_cap = turbo->code_cap * 2;
		if (new_cap == 0) new_cap = 256;
		ln_instr_t *new_code = realloc(turbo->code, new_cap * sizeof(ln_instr_t));
		if (!new_code) return UINT32_MAX;
		turbo->code = new_code;
		turbo->code_cap = new_cap;
	}

	uint32_t idx = turbo->code_len++;
	turbo->code[idx] = *instr;
	return idx;
}

static uint32_t
emit_literal(compiler_t *comp, const char *lit, size_t len)
{
	ln_instr_t instr = {0};
	instr.op = OP_LITERAL;
	if (len >= sizeof(instr.data.str)) return 0;  /* too long for inline — fall back to v1 */
	instr.aux = (uint16_t)len;
	memcpy(instr.data.str, lit, len);
	return emit(comp, &instr);
}

static uint32_t
emit_field(compiler_t *comp, ln_opcode_t op, const char *name, char delim)
{
	ln_instr_t instr = {0};
	instr.op = op;
	instr.flags = LN_INSTR_F_STORE;

	if (op == OP_FIELD_CHAR_TO || op == OP_FIELD_STR_TO) {
		instr.data.char_to.delim = (uint8_t)delim;
		if (name) {
			size_t nlen = strlen(name);
			if (nlen >= sizeof(instr.data.char_to.name))
				return 0;  /* field name too long — fall back to v1 */
			memcpy(instr.data.char_to.name, name, nlen);
		}
	} else {
		if (name) {
			size_t nlen = strlen(name);
			if (nlen >= sizeof(instr.data.str))
				return 0;  /* field name too long — fall back to v1 */
			memcpy(instr.data.str, name, nlen);
		}
	}

	return emit(comp, &instr);
}

static uint32_t
emit_fork(compiler_t *comp)
{
	ln_instr_t instr = {0};
	instr.op = OP_FORK;
	comp->n_branches++;
	return emit(comp, &instr);
}

/**
 * @brief Emit OP_TAG instructions for all tags on a terminal node.
 */
static int
emit_tags(compiler_t *comp, struct ln_pdag *node)
{
	if (!node->tags)
		return 0;  /* no tags */

	int n = json_object_array_length(node->tags);
	for (int i = 0; i < n; i++) {
		struct json_object *tagObj = json_object_array_get_idx(node->tags, i);
		if (!tagObj) continue;
		const char *tagStr = json_object_get_string(tagObj);
		if (!tagStr || !tagStr[0]) continue;

		ln_instr_t instr = {0};
		instr.op = OP_TAG;
		size_t len = strlen(tagStr);
		if (len >= sizeof(instr.data.str))
			return -1;  /* tag name too long — fall back to v1 */
		memcpy(instr.data.str, tagStr, len);

		if (emit(comp, &instr) == UINT32_MAX)
			return -1;
	}
	return 0;
}

/**
 * @brief Resolve annotations at compile time and emit OP_STATIC_FIELD.
 *
 * For each tag on the terminal node, look up the annotation set.
 * For each annotation operation (ADD), emit an OP_STATIC_FIELD instruction
 * with the field name and value baked into the instruction data.
 *
 * This eliminates the need for runtime annotation resolution entirely.
 * The annotation values (like event.kind="event") become part of the
 * compiled instruction stream — zero per-message overhead.
 */
static int
emit_annotation_fields(compiler_t *comp, struct ln_pdag *node)
{
	if (!node->tags || !comp->ctx->pas)
		return 0;

	int n = json_object_array_length(node->tags);
	for (int i = 0; i < n; i++) {
		struct json_object *tagObj = json_object_array_get_idx(node->tags, i);
		if (!tagObj) continue;
		const char *tagStr = json_object_get_string(tagObj);
		if (!tagStr || !tagStr[0]) continue;

		/* Look up annotation for this tag */
		es_str_t *tag_es = es_newStrFromCStr(tagStr, strlen(tagStr));
		if (!tag_es) continue;

		ln_annot *annot = ln_findAnnot(comp->ctx->pas, tag_es);
		es_deleteStr(tag_es);

		if (!annot) continue;

		/* Emit OP_STATIC_FIELD for each ADD operation */
		for (ln_annot_op *op = annot->oproot; op; op = op->next) {
			if (op->opc != ln_annot_ADD) continue;

			char *name_cstr = ln_es_str2cstr(&op->name);
			char *val_cstr = op->value ? ln_es_str2cstr(&op->value) : NULL;

			if (!name_cstr || !name_cstr[0]) continue;

			ln_instr_t instr = {0};
			instr.op = OP_STATIC_FIELD;

			size_t klen = strlen(name_cstr);
			if (klen >= sizeof(instr.data.kv.key))
				klen = sizeof(instr.data.kv.key) - 1;
			memcpy(instr.data.kv.key, name_cstr, klen);
			instr.aux = (uint16_t)klen;

			if (val_cstr) {
				size_t vlen = strlen(val_cstr);
				if (vlen >= sizeof(instr.data.kv.val))
					vlen = sizeof(instr.data.kv.val) - 1;
				memcpy(instr.data.kv.val, val_cstr, vlen);
			}

			if (emit(comp, &instr) == UINT32_MAX)
				return -1;
		}
	}
	return 0;
}

static uint32_t
emit_match(compiler_t *comp, struct ln_pdag *node)
{
	/* A top-level rule matches only when it has consumed the ENTIRE input:
	 * the recursive normalizer accepts a terminal for normalization only at
	 * end-of-input.  Assert that first so turbo agrees — otherwise a rule
	 * whose last parser stops before EOL, or a strict-prefix rule reached via
	 * the fallback fork, would falsely match a longer line on its trailing
	 * bytes.  (Custom-type sub-matches end in OP_RET, not here, so they are
	 * unaffected.) */
	ln_instr_t end_instr = {0};
	end_instr.op = OP_ASSERT_END;
	uint32_t entry = emit(comp, &end_instr);
	if (entry == UINT32_MAX)
		return UINT32_MAX;

	/* Emit tags first — these populate result.tags[] */
	if (emit_tags(comp, node) != 0)
		return UINT32_MAX;

	/* Emit resolved annotation fields as static key-value pairs */
	if (emit_annotation_fields(comp, node) != 0)
		return UINT32_MAX;

	/* Emit the MATCH instruction */
	ln_instr_t instr = {0};
	instr.op = OP_MATCH;

	if (node->rb_id) {
		size_t len = strlen(node->rb_id);
		if (len >= sizeof(instr.data.str)) len = sizeof(instr.data.str) - 1;
		memcpy(instr.data.str, node->rb_id, len);
	}

	comp->n_rules++;
	if (emit(comp, &instr) == UINT32_MAX)
		return UINT32_MAX;
	/* Entry is the ASSERT_END — every path into this terminal (sequential,
	 * fallback fork, or a sibling-branch fork) goes through the EOL gate. */
	return entry;
}

static uint32_t
emit_halt(compiler_t *comp)
{
	ln_instr_t instr = {0};
	instr.op = OP_HALT;
	return emit(comp, &instr);
}

static uint32_t
emit_ctx_push(compiler_t *comp, const char *name)
{
	ln_instr_t instr = {0};
	instr.op = OP_CTX_PUSH;
	if (name) {
		size_t nlen = strlen(name);
		if (nlen >= sizeof(instr.data.str))
			return 0;  /* context name too long — fall back to v1 */
		memcpy(instr.data.str, name, nlen);
	}
	return emit(comp, &instr);
}

static uint32_t
emit_ctx_pop(compiler_t *comp)
{
	ln_instr_t instr = {0};
	instr.op = OP_CTX_POP;
	return emit(comp, &instr);
}

/*============================================================================
 * PDAG Traversal
 *============================================================================*/

static int compile_node(compiler_t *comp, struct ln_pdag *node, uint32_t *entry);

/* Forward declaration for name-value-list parser data (defined in parser.c).
 * Layout must match struct data_NameValue in parser.c. */
struct data_NameValue {
	char sep;   /* separator (between key/value pairs) */
	char ass;   /* assignator (between key and value) */
	bool ignore_whitespaces; /* trim surrounding whitespace for key/value */
};

/* Mirrors the layout-prefix of parser.c's private data_CharTo /
 * data_CharSeparated (no shared header). Both start with the same two fields;
 * types must match parser.c exactly -- n_term_chars is size_t, not int (an int
 * mirror only happens to work on little-endian LP64). Keep in lockstep. */
struct data_CharSeparated {
	char  *term_chars;
	size_t n_term_chars;
};

/* Forward declaration for Checkpoint LEA parser data (defined in parser.c). */
struct data_CheckpointLEA {
	char terminator;
};

static int
compile_parser(compiler_t *comp, ln_parser_t *prs, uint32_t *out_pc)
{
	uint32_t pc;

	/* Handle custom types with context push/pop */
	if (prs->prsid == PRS_CUSTOM_TYPE) {
		/* Push field name context BEFORE the CALL */
		/* Skip if name is "." (root context placeholder) or empty */
		if (prs->name && prs->name[0] &&
			!(prs->name[0] == '.' && prs->name[1] == '\0')) {
			uint32_t ctx_pc = emit_ctx_push(comp, prs->name);
			if (ctx_pc == UINT32_MAX) return -1;
			*out_pc = ctx_pc;
		}

		if (prs->custType >= 0 && (int)prs->custType < comp->ctx->nTypes) {
			ln_pdag *type_pdag = comp->ctx->type_pdags[prs->custType].pdag;

			if (type_pdag) {
				ln_instr_t call_instr = {0};
				call_instr.op = OP_CALL;
				uint32_t call_pc = emit(comp, &call_instr);
				if (call_pc == UINT32_MAX) return -1;
				if (*out_pc == 0) *out_pc = call_pc;

				ln_instr_t jump_instr = {0};
				jump_instr.op = OP_JUMP;
				uint32_t jump_pc = emit(comp, &jump_instr);
				if (jump_pc == UINT32_MAX) return -1;

				uint32_t type_start = comp->turbo->code_len;
				comp->turbo->code[call_pc].data.jump.offset =
					(int32_t)type_start - (int32_t)call_pc;

				comp->in_custom_type++;
				uint32_t type_entry;
				int r = compile_node(comp, type_pdag, &type_entry);
				comp->in_custom_type--;
				if (r != 0) return r;

				uint32_t type_end = comp->turbo->code_len;
				comp->turbo->code[jump_pc].data.jump.offset =
					(int32_t)type_end - (int32_t)jump_pc;
			}
		}

		/* Pop field name context AFTER the call */
		/* Only pop if we pushed (skip if name was "." or empty) */
		if (prs->name && prs->name[0] &&
			!(prs->name[0] == '.' && prs->name[1] == '\0')) {
			if (emit_ctx_pop(comp) == UINT32_MAX) return -1;
		}

		if (prs->node) {
			uint32_t cont_entry;
			int r = compile_node(comp, prs->node, &cont_entry);
			if (r != 0) return r;
			if (*out_pc == 0) *out_pc = cont_entry;
		}
		return 0;
	}

	ln_opcode_t op = prsid_to_opcode(prs->prsid);

	if (op == OP_INVALID) {
		LN_DBGPRINTF(comp->ctx, "turbo: unsupported parser %d", prs->prsid);
		return -1;
	}

	if (op == OP_LITERAL) {
		const char *litstr = ln_DataForDisplayLiteral(comp->ctx, prs->parser_data);
		if (litstr && *litstr) {
			pc = emit_literal(comp, litstr, strlen(litstr));
			/* Named literal: also emit the matched text as a static field.
			 * e.g. %{"name":"network.type","type":"literal","text":"4"}%
			 * matches "4" AND stores network.type="4" in the result. */
			if (prs->name && prs->name[0]) {
				ln_instr_t sf = {0};
				sf.op = OP_STATIC_FIELD;
				size_t klen = strlen(prs->name);
				if (klen >= sizeof(sf.data.kv.key))
					klen = sizeof(sf.data.kv.key) - 1;
				memcpy(sf.data.kv.key, prs->name, klen);
				sf.aux = (uint16_t)klen;
				size_t vlen = strlen(litstr);
				if (vlen >= sizeof(sf.data.kv.val))
					vlen = sizeof(sf.data.kv.val) - 1;
				memcpy(sf.data.kv.val, litstr, vlen);
				if (emit(comp, &sf) == UINT32_MAX) return -1;
			}
		} else {
			ln_instr_t nop = {0};
			nop.op = OP_NOP;
			pc = emit(comp, &nop);
		}
	} else if (op == OP_SKIP_SPACE) {
		ln_instr_t instr = {0};
		instr.op = OP_SKIP_SPACE;
		pc = emit(comp, &instr);
	} else if (op == OP_FIELD_NAME_VALUE) {
		/* name-value-list: extract sep/ass/ignore_whitespaces from parser_data */
		char sep = 0, ass = 0;  /* 0 = default (whitespace sep, '=' ass) */
		uint8_t ignore_ws = 0;
		if (prs->parser_data) {
			struct data_NameValue *nvdata = (struct data_NameValue *)prs->parser_data;
			sep = nvdata->sep;
			ass = nvdata->ass;
			ignore_ws = nvdata->ignore_whitespaces ? 1 : 0;
		}
		ln_instr_t instr = {0};
		instr.op = OP_FIELD_NAME_VALUE;
		instr.flags = LN_INSTR_F_STORE;
		instr.data.char_to.delim = (uint8_t)sep;
		instr.data.char_to.ass   = (uint8_t)ass;
		instr.data.char_to.ignore_ws = ignore_ws;
		if (prs->name) {
			size_t nlen = strlen(prs->name);
			if (nlen >= sizeof(instr.data.char_to.name))
				return -1;  /* field name too long — fall back to v1 */
			memcpy(instr.data.char_to.name, prs->name, nlen);
		}
		pc = emit(comp, &instr);
	} else if (op == OP_FIELD_CHAR_TO || op == OP_FIELD_STR_TO) {
		char delim = ' ';  /* Default to space */
		/* Both char-to and char-sep store their delimiter in the same
		 * memory layout: term_chars[0..n_term_chars-1].  We cast through
		 * data_CharSeparated which mirrors the first two fields of the
		 * upstream data_CharTo struct (ABI-safe).
		 *
		 * NOTE: Do NOT use ln_DataForDisplayCharTo() here — it returns
		 * the display string "char-to{X}", not the raw delimiter char. */
		if (prs->parser_data &&
			(prs->prsid == PRSID_CHARTO || prs->prsid == PRSID_CHARSEP)) {
			struct data_CharSeparated *csdata =
				(struct data_CharSeparated *)prs->parser_data;
			if (csdata->n_term_chars > 0 && csdata->term_chars) {
				delim = csdata->term_chars[0];
			}
		}
		pc = emit_field(comp, op, prs->name, delim);
	} else if (op == OP_V2_IPTABLES || op == OP_CEE_SYSLOG || op == OP_CEF_HDR) {
		/* Simple opcodes: no parser_data config, just emit with field name */
		pc = emit_field(comp, op, prs->name, ' ');
	} else if (op == OP_CHECKPOINT_LEA) {
		/* Checkpoint LEA: extract terminator from parser_data */
		ln_instr_t instr = {0};
		instr.op = OP_CHECKPOINT_LEA;
		instr.flags = LN_INSTR_F_STORE;
		instr.data.char_to.delim = 0; /* no terminator by default */
		if (prs->parser_data) {
			struct data_CheckpointLEA *cpdata =
				(struct data_CheckpointLEA *)prs->parser_data;
			instr.data.char_to.delim = (uint8_t)cpdata->terminator;
		}
		if (prs->name) {
			size_t nlen = strlen(prs->name);
			if (nlen >= sizeof(instr.data.char_to.name))
				return -1;  /* field name too long — fall back to v1 */
			memcpy(instr.data.char_to.name, prs->name, nlen);
		}
		pc = emit(comp, &instr);
	} else {
		pc = emit_field(comp, op, prs->name, ' ');
	}

	if (pc == UINT32_MAX) return -1;
	*out_pc = pc;

	if (prs->node) {
		uint32_t cont_entry;
		int r = compile_node(comp, prs->node, &cont_entry);
		if (r != 0) return r;
	}

	return 0;
}

static int
compile_node(compiler_t *comp, struct ln_pdag *node, uint32_t *entry)
{
	if (!node) return -1;

	comp->depth++;
	if (comp->depth > comp->max_depth) {
		comp->max_depth = comp->depth;
	}

	if (comp->depth > 200) {
		comp->depth--;
		return -1;
	}

	uint32_t first = comp->turbo->code_len;
	int r;

	if (node->nparsers == 0 && node->flags.isTerminal) {
		if (comp->in_custom_type == 0) {
			*entry = emit_match(comp, node);
			comp->depth--;
			return (*entry == UINT32_MAX) ? -1 : 0;
		} else {
			ln_instr_t ret_instr = {0};
			ret_instr.op = OP_RET;
			*entry = emit(comp, &ret_instr);
			comp->depth--;
			return (*entry == UINT32_MAX) ? -1 : 0;
		}
	}

	if (node->nparsers == 0) {
		/* Non-terminal dead-end: no parsers to continue matching.
		 * This is valid in the pdag (the recursive walker just fails to
		 * match here and backtracks).  Emit OP_FAIL so the VM does the
		 * same instead of aborting the whole compilation. */
		LN_DBGPRINTF(comp->ctx, "turbo: non-terminal dead-end node %p "
					 "(nparsers=0, depth=%d) — emitting OP_FAIL",
					 (void *)node, comp->depth);
		ln_instr_t fail_instr = {0};
		fail_instr.op = OP_FAIL;
		*entry = emit(comp, &fail_instr);
		comp->depth--;
		return (*entry == UINT32_MAX) ? -1 : 0;
	}

	*entry = first;

	/* A node can be terminal AND still have continuation parsers when its
	 * rule is a strict prefix of a longer rule.  The continuations are tried
	 * first; if they all fail the terminal OP_MATCH below must still be
	 * reachable, so reserve a fallback fork to it here (lowest priority). */
	uint32_t term_fallback_pc = UINT32_MAX;
	if (node->flags.isTerminal) {
		term_fallback_pc = emit_fork(comp);
		if (term_fallback_pc == UINT32_MAX) { comp->depth--; return -1; }
	}

	if (node->nparsers == 1) {
		uint32_t pc;
		r = compile_parser(comp, &node->parsers[0], &pc);
		if (r != 0) { comp->depth--; return r; }

		if (node->flags.isTerminal) {
			comp->turbo->code[term_fallback_pc].data.jump.offset =
				(int32_t)comp->turbo->code_len - (int32_t)term_fallback_pc;
			if (comp->in_custom_type == 0) {
				if (emit_match(comp, node) == UINT32_MAX) {
					comp->depth--;
					return -1;
				}
			} else {
				ln_instr_t ret_instr = {0};
				ret_instr.op = OP_RET;
				if (emit(comp, &ret_instr) == UINT32_MAX) {
					comp->depth--;
					return -1;
				}
			}
		}
		comp->depth--;
		return 0;
	}

	uint32_t *fork_pcs = malloc(sizeof(uint32_t) * node->nparsers);
	if (!fork_pcs) { comp->depth--; return -1; }

	for (int i = 0; i < node->nparsers; i++) {
		if (i < node->nparsers - 1) {
			fork_pcs[i] = emit_fork(comp);
			if (fork_pcs[i] == UINT32_MAX) {
				free(fork_pcs);
				comp->depth--;
				return -1;
			}
		}

		uint32_t parser_pc;
		r = compile_parser(comp, &node->parsers[i], &parser_pc);
		if (r != 0) {
			free(fork_pcs);
			comp->depth--;
			return r;
		}

		if (i < node->nparsers - 1) {
			comp->turbo->code[fork_pcs[i]].data.jump.offset =
				(int32_t)comp->turbo->code_len - (int32_t)fork_pcs[i];
		}
	}

	free(fork_pcs);

	if (node->flags.isTerminal) {
		comp->turbo->code[term_fallback_pc].data.jump.offset =
			(int32_t)comp->turbo->code_len - (int32_t)term_fallback_pc;
		if (comp->in_custom_type == 0) {
			if (emit_match(comp, node) == UINT32_MAX) {
				comp->depth--;
				return -1;
			}
		} else {
			ln_instr_t ret_instr = {0};
			ret_instr.op = OP_RET;
			if (emit(comp, &ret_instr) == UINT32_MAX) {
				comp->depth--;
				return -1;
			}
		}
	}

	comp->depth--;
	return 0;
}

/*============================================================================
 * Public API
 *============================================================================*/

ln_turbo_ctx_t *
ln_turbo_ctx_init(void)
{
	ln_turbo_ctx_t *turbo = calloc(1, sizeof(*turbo));
	if (!turbo) return NULL;

	if (ln_arena_init(&turbo->arena) != 0) {
		free(turbo);
		return NULL;
	}
	if (ln_vm_init(&turbo->vm, &turbo->arena) != 0) {
		ln_arena_destroy(&turbo->arena);
		free(turbo);
		return NULL;
	}
	ln_fast_result_init(&turbo->result, &turbo->arena);

	/* Pre-allocate JSON buffer (8KB initial) */
	turbo->json_buf_cap = 8192;
	turbo->json_buf = malloc(turbo->json_buf_cap);

	turbo->enabled = 1;

	return turbo;
}

void
ln_turbo_ctx_free(ln_turbo_ctx_t *turbo)
{
	if (!turbo) return;

	if (turbo->code) {
		free(turbo->code);
		turbo->code = NULL;
	}
	if (turbo->json_buf) {
		free(turbo->json_buf);
		turbo->json_buf = NULL;
	}
	ln_vm_destroy(&turbo->vm);
	if (turbo->arena.base) {
		ln_arena_destroy(&turbo->arena);
	}
	free(turbo);
}

int
ln_turbo_compile(ln_ctx ctx)
{
	if (!ctx || !ctx->turbo) return -1;
	if (!ctx->pdag) return -1;

	LN_DBGPRINTF(ctx, "turbo: compile entry — version=%d pdag=%p "
				 "nparsers=%d isTerminal=%d nNodes=%d",
				 ctx->version, (void *)ctx->pdag,
				 ctx->pdag->nparsers,
				 ctx->pdag->flags.isTerminal,
				 ctx->nNodes);

	ln_turbo_ctx_t *turbo = ctx->turbo;

	free(turbo->code);
	turbo->code = NULL;
	turbo->code_len = 0;
	turbo->code_cap = 0;

	compiler_t comp = {0};
	comp.turbo = turbo;
	comp.ctx = ctx;

	comp.visited.cap = 256;
	comp.visited.offsets = calloc(comp.visited.cap, sizeof(uint32_t));
	if (!comp.visited.offsets) return -1;

	uint32_t entry;
	int r = compile_node(&comp, ctx->pdag, &entry);

	free(comp.visited.offsets);

	if (r != 0) {
		LN_DBGPRINTF(ctx, "turbo: compilation failed");
		return -1;
	}

	emit_halt(&comp);

	turbo->stats.n_instructions = turbo->code_len;
	turbo->stats.n_rules = comp.n_rules;
	turbo->stats.n_branches = comp.n_branches;
	turbo->stats.max_depth = comp.max_depth;

	LN_DBGPRINTF(ctx, "turbo: compiled %u instructions, %u rules, %u branches",
				 turbo->code_len, comp.n_rules, comp.n_branches);

	return 0;
}

int
ln_turbo_is_available(ln_ctx ctx)
{
	if(!ctx || !ctx->turbo)
		return 0;
	ln_turbo_ctx_t *turbo = (ln_turbo_ctx_t *)ctx->turbo;
	return turbo->enabled && turbo->code_len > 0;
}

/**
 * @brief Normalize using turbo VM and return JSON string (FAST PATH).
 */
int
ln_turbo_normalize_to_str(ln_ctx ctx, const char *str, size_t strLen,
						  char **json_str, size_t *json_len)
{
	if (!ln_turbo_is_available(ctx)) return -1;

	ln_turbo_ctx_t *turbo = ctx->turbo;

	/* Reset fast result */
	ln_fast_result_clear(&turbo->result);
	ln_arena_reset(&turbo->arena);
	ln_vm_reset(&turbo->vm);

	ln_program_t prog = {
		.code = turbo->code,
		.code_len = turbo->code_len,
		.name = "turbo"
	};

	/* Execute VM with fast result */
	int r = ln_vm_exec(&turbo->vm, &prog, str, strLen, &turbo->result);

	if (r != LN_VM_OK) {
		LN_DBGPRINTF(ctx, "turbo VM exec returned %d, error: %s",
					 r, turbo->vm.error ? turbo->vm.error : "(none)");
		*json_str = NULL;
		return -1;
	}

	/* Serialize using fast JSON (with nested object support) */
	size_t est = ln_fast_json_estimate(&turbo->result);

	/* Grow buffer if needed */
	if (est > turbo->json_buf_cap) {
		size_t new_cap = est + 1024;
		char *new_buf = realloc(turbo->json_buf, new_cap);
		if (!new_buf) {
			*json_str = NULL;
			return -1;
		}
		turbo->json_buf = new_buf;
		turbo->json_buf_cap = new_cap;
	}

	size_t outlen;
	if (ln_fast_to_json(&turbo->result, turbo->json_buf, turbo->json_buf_cap, &outlen) != 0) {
		*json_str = NULL;
		return -1;
	}

	/* Return allocated copy (caller will free) */
	*json_str = strdup(turbo->json_buf);
	if (!*json_str) {
		return -1;
	}
	if (json_len) *json_len = outlen;

	/* Update stats */
	turbo->stats.messages_processed++;
	turbo->stats.total_bytes += strLen;

	return 0;
}

/**
 * @brief Legacy function returning fastjson structure.
 */
int
ln_turbo_normalize(ln_ctx ctx, const char *str, size_t strLen,
				   struct json_object **json_p)
{
	char *json_str;
	size_t json_len;

	if (ln_turbo_normalize_to_str(ctx, str, strLen, &json_str, &json_len) != 0) {
		*json_p = NULL;
		return -1;
	}

	/* Parse JSON string to json_object */
	*json_p = json_tokener_parse(json_str);
	free(json_str);
	if (!*json_p) {
		return -1;
	}

	return 0;
}

int
ln_turbo_get_stats(ln_ctx ctx, ln_turbo_stats_t *stats)
{
	if (!ctx || !ctx->turbo || !stats) return -1;
	ln_turbo_ctx_t *turbo = (ln_turbo_ctx_t *)ctx->turbo;
	*stats = turbo->stats;
	return 0;
}

/*============================================================================
 * Direct Result Access API (zero JSON overhead)
 *============================================================================*/

/**
 * @brief Normalize using turbo VM - direct result access (FASTEST PATH).
 *
 * Returns a pointer to the internal result structure. No JSON serialization,
 * no string conversion, no memory allocation. The result is valid until the
 * next normalize call on the same context.
 *
 * This is the optimal API for rsyslog integration: the caller can iterate
 * over typed fields and build json_object* directly, avoiding the
 * serialize-to-JSON-string + parse-JSON-string roundtrip.
 */
int
ln_turbo_normalize_raw(ln_ctx ctx, const char *str, size_t strLen,
					   const ln_fast_result_t **result)
{
	if (!ln_turbo_is_available(ctx)) return -1;
	if (!result) return -1;

	ln_turbo_ctx_t *turbo = ctx->turbo;

	/* Reset per-message state */
	ln_fast_result_clear(&turbo->result);
	ln_arena_reset(&turbo->arena);
	ln_vm_reset(&turbo->vm);

	ln_program_t prog = {
		.code = turbo->code,
		.code_len = turbo->code_len,
		.name = "turbo"
	};

	/* Execute VM */
	int r = ln_vm_exec(&turbo->vm, &prog, str, strLen, &turbo->result);
	if (r != LN_VM_OK) {
		LN_DBGPRINTF(ctx, "turbo VM exec returned %d, error: %s",
					 r, turbo->vm.error ? turbo->vm.error : "(none)");
		*result = NULL;
		return -1;
	}

	*result = &turbo->result;

	/* Update stats */
	turbo->stats.messages_processed++;
	turbo->stats.total_bytes += strLen;

	return 0;
}

/*============================================================================
 * Fast Result Accessor Functions
 *============================================================================*/

int
ln_fast_result_field_count(const ln_fast_result_t *r)
{
	return r ? r->n_fields : 0;
}

int
ln_fast_result_get_field(const ln_fast_result_t *r, int idx,
						 const char **name, size_t *nlen,
						 const char **value, size_t *vlen)
{
	if (!r || idx < 0 || idx >= r->n_fields) return -1;
	const ln_fast_field_t *f = &r->fields[idx];
	*name = f->name;
	*nlen = f->name_len;
	switch (f->type) {
	case LN_FTYPE_STRING:
		*value = f->v.str.ptr;
		*vlen = f->v.str.len;
		break;
	case LN_FTYPE_STRING_INLINE:
		*value = f->v.inl;
		*vlen = strlen(f->v.inl);
		break;
	default:
		/* Non-string types: caller should use typed accessors */
		*value = NULL;
		*vlen = 0;
		break;
	}
	return 0;
}

int
ln_fast_result_get_field_typed(const ln_fast_result_t *r, int idx,
							   const char **name, size_t *nlen,
							   ln_ftype_t *type, unsigned *flags,
							   const char **sval, size_t *slen,
							   int64_t *ival, double *dval)
{
	if (!r || idx < 0 || idx >= r->n_fields) return -1;
	const ln_fast_field_t *f = &r->fields[idx];
	if (name)  *name  = f->name;
	if (nlen)  *nlen  = f->name_len;
	if (type)  *type  = (ln_ftype_t)f->type;
	if (flags) *flags = (unsigned)(f->flags & LN_FFIELD_NESTED);
	switch (f->type) {
	case LN_FTYPE_STRING:
		if (sval) *sval = f->v.str.ptr;
		if (slen) *slen = f->v.str.len;
		break;
	case LN_FTYPE_STRING_INLINE:
		if (sval) *sval = f->v.inl;
		if (slen) *slen = strlen(f->v.inl);
		break;
	case LN_FTYPE_INT:
		if (ival) *ival = f->v.i;
		break;
	case LN_FTYPE_BOOL:
		if (ival) *ival = f->v.b ? 1 : 0;
		break;
	case LN_FTYPE_DOUBLE:
		if (dval) *dval = f->v.d;
		break;
	default: /* LN_FTYPE_NULL */
		break;
	}
	return 0;
}

int
ln_fast_result_get_string(const ln_fast_result_t *r, const char *name,
						  const char **value, size_t *vlen)
{
	if (!r || !name) return -1;
	for (int i = 0; i < r->n_fields; i++) {
		if (r->fields[i].name &&
			strcmp(r->fields[i].name, name) == 0) {
			const ln_fast_field_t *f = &r->fields[i];
			switch (f->type) {
			case LN_FTYPE_STRING:
				*value = f->v.str.ptr;
				*vlen = f->v.str.len;
				return 0;
			case LN_FTYPE_STRING_INLINE:
				*value = f->v.inl;
				*vlen = strlen(f->v.inl);
				return 0;
			default:
				return -1;
			}
		}
	}
	return -1;
}

int
ln_fast_result_get_int(const ln_fast_result_t *r, const char *name,
					   int64_t *value)
{
	if (!r || !name) return -1;
	for (int i = 0; i < r->n_fields; i++) {
		if (r->fields[i].type == LN_FTYPE_INT &&
			r->fields[i].name &&
			strcmp(r->fields[i].name, name) == 0) {
			*value = r->fields[i].v.i;
			return 0;
		}
	}
	return -1;
}

int
ln_fast_result_tag_count(const ln_fast_result_t *r)
{
	return r ? r->n_tags : 0;
}

const char *
ln_fast_result_get_tag(const ln_fast_result_t *r, int idx)
{
	if (!r || idx < 0 || idx >= r->n_tags) return NULL;
	return r->tags[idx].tag;
}

int
ln_fast_result_has_tag(const ln_fast_result_t *r, const char *tag)
{
	return ln_fast_has_tag(r, tag);
}

const char *
ln_fast_result_get_rule_id(const ln_fast_result_t *r)
{
	return r ? r->rule_id : NULL;
}

/*============================================================================
 * Snapshot API (for rsyslog zero-JSON hot path)
 *============================================================================*/

/**
 * @brief Create a snapshot of the current turbo result.
 *
 * Must be called after a successful ln_turbo_normalize_raw() and before
 * the next normalize call (which resets the arena). The snapshot is a
 * self-contained deep copy that can be attached to smsg_t.
 *
 * @param ctx  liblognorm context with a valid turbo result
 * @return Snapshot (caller owns), or NULL on failure
 */
ln_fast_result_snapshot_t *
ln_turbo_snapshot_result(ln_ctx ctx)
{
	if (!ctx || !ctx->turbo) return NULL;

	ln_turbo_ctx_t *turbo = ctx->turbo;
	return ln_fast_result_snapshot_create(&turbo->result, &turbo->arena);
}

/*============================================================================
 * Bytecode Disassembly (for diagnostic / LN_VM_TRACE builds)
 *============================================================================*/

void
ln_turbo_disasm(ln_ctx ctx, FILE *fp, const char *label)
{
#ifdef LN_VM_TRACE
	if (!ctx || !ctx->turbo || !fp) return;

	ln_turbo_ctx_t *turbo = ctx->turbo;
	if (turbo->code_len == 0) {
		fprintf(fp, "[%s] (no compiled bytecode)\n", label ? label : "turbo");
		return;
	}

	ln_program_t p = ln_program_make(turbo->code, turbo->code_len,
									 label ? label : "turbo");
	ln_program_disasm(&p, fp);
#else
	(void)ctx; (void)fp; (void)label;
#endif
}

#endif /* ENABLE_TURBO */
