/**
 * @file turbo_vm_opt.h
 * @brief Optimized VM dispatch using computed goto
 *//*
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_TURBO_VM_OPT_H_INCLUDED
#define	LIBLOGNORM_TURBO_VM_OPT_H_INCLUDED

#include "turbo_opcode.h"

/*============================================================================
 * Computed Goto Dispatch (GCC/Clang Extension)
 *============================================================================
 *
 * Traditional switch dispatch:
 *   switch (op) {               // 1. Load op
 *       case OP_LITERAL: ...    // 2. Bounds check
 *       case OP_MATCH: ...      // 3. Jump through compiler table
 *   }                           // 4. Execute handler
 *                               // 5. Jump back to switch (break)
 *
 * Computed goto dispatch:
 *   void *dispatch[] = { [OP_LITERAL] = &&op_literal, ... };
 *   goto *dispatch[op];         // 1. Load op
 *   op_literal: ... NEXT();     // 2. Index into table (no bounds check)
 *   op_match: ... NEXT();       // 3. Direct jump to handler
 *                               // 4. Execute handler
 *                               // 5. Direct jump to next handler
 *
 * The key win: steps 2+3 collapse into a single indexed load+jump,
 * and the CPU branch predictor sees a unique indirect branch site
 * per opcode handler (better prediction than a single switch site).
 */

#if defined(__GNUC__) || defined(__clang__)
#define LN_VM_COMPUTED_GOTO 1
#else
#define LN_VM_COMPUTED_GOTO 0
#endif

/*
 * OP_COUNT must cover the full opcode byte range (0x00-0xFF).
 * Sparse opcodes are fine — unused slots point to op_invalid.
 * 256 × 8 bytes = 2KB table, fits in L1d cache.
 */
#define OP_COUNT 256

#if LN_VM_COMPUTED_GOTO

/*
 * DISPATCH_INIT: build the dispatch table as a local variable.
 *
 * We use procedural initialization instead of designated initializers
 * because Apple Clang requires labels to be visible before taking their
 * address in static initializers. A local array with procedural init
 * works correctly on both GCC and Clang.
 *
 * The compiler optimizes this into a single memcpy from rodata in
 * practice (verified with -O2 on both GCC 13 and Clang 17).
 *
 * Cost: ~2KB stack + memset + ~44 stores = negligible vs. per-message
 * VM execution time (50-500 instructions per message).
 */
#define DISPATCH_INIT() \
	void *dispatch_table[OP_COUNT]; \
	do { \
		/* Fill all slots with op_invalid first */ \
		for (int _i = 0; _i < OP_COUNT; _i++) \
			dispatch_table[_i] = &&op_invalid; \
		/* Control (0x00-0x06) */ \
		dispatch_table[OP_HALT]            = &&op_halt; \
		dispatch_table[OP_MATCH]           = &&op_match; \
		dispatch_table[OP_JUMP]            = &&op_jump; \
		dispatch_table[OP_FORK]            = &&op_fork; \
		dispatch_table[OP_FAIL]            = &&op_fail; \
		dispatch_table[OP_CALL]            = &&op_call; \
		dispatch_table[OP_RET]             = &&op_ret; \
		/* Literals (0x10-0x15) */ \
		dispatch_table[OP_LITERAL]         = &&op_literal; \
		dispatch_table[OP_LITERAL_EXT]     = &&op_literal_ext; \
		dispatch_table[OP_LITERAL_CI]      = &&op_literal_ci; \
		dispatch_table[OP_CHAR]            = &&op_char; \
		dispatch_table[OP_ANY]             = &&op_any; \
		dispatch_table[OP_CHARSET]         = &&op_charset; \
		/* Fields (0x20-0x2F) */ \
		dispatch_table[OP_FIELD_WORD]      = &&op_field_word; \
		dispatch_table[OP_FIELD_INT]       = &&op_field_int; \
		dispatch_table[OP_FIELD_UINT]      = &&op_field_uint; \
		dispatch_table[OP_FIELD_FLOAT]     = &&op_field_float; \
		dispatch_table[OP_FIELD_IPV4]      = &&op_field_ipv4; \
		dispatch_table[OP_FIELD_IPV6]      = &&op_field_ipv6; \
		dispatch_table[OP_FIELD_HEX]       = &&op_field_hex; \
		dispatch_table[OP_FIELD_QUOTED]    = &&op_field_quoted; \
		dispatch_table[OP_FIELD_CHAR_TO]   = &&op_field_char_to; \
		dispatch_table[OP_FIELD_STR_TO]    = &&op_field_str_to; \
		dispatch_table[OP_FIELD_REST]      = &&op_field_rest; \
		dispatch_table[OP_FIELD_JSON]      = &&op_field_json; \
		dispatch_table[OP_FIELD_MAC]       = &&op_field_mac; \
		dispatch_table[OP_FIELD_DATE]      = &&op_field_date; \
		dispatch_table[OP_FIELD_REGEX]     = &&op_field_regex; \
		dispatch_table[OP_FIELD_NAME_VALUE]= &&op_field_name_value; \
		/* Skipping (0x40-0x45) */ \
		dispatch_table[OP_SKIP_SPACE]      = &&op_skip_space; \
		dispatch_table[OP_SKIP_SPACE1]     = &&op_skip_space1; \
		dispatch_table[OP_SKIP_N]          = &&op_skip_n; \
		dispatch_table[OP_SKIP_TO]         = &&op_skip_to; \
		dispatch_table[OP_SKIP_PAST]       = &&op_skip_past; \
		dispatch_table[OP_SKIP_LINE]       = &&op_skip_line; \
		/* Tags (0x50-0x52) */ \
		dispatch_table[OP_TAG]             = &&op_tag; \
		dispatch_table[OP_RULE_ID]         = &&op_rule_id; \
		dispatch_table[OP_STATIC_FIELD]    = &&op_static_field; \
		/* Field Context (0x58-0x5B) */ \
		dispatch_table[OP_CTX_PUSH]        = &&op_ctx_push; \
		dispatch_table[OP_CTX_POP]         = &&op_ctx_pop; \
		dispatch_table[OP_CTX_NEST]        = &&op_ctx_nest; \
		dispatch_table[OP_CTX_UNNEST]      = &&op_ctx_unnest; \
		/* Assertions (0x60-0x62) */ \
		dispatch_table[OP_ASSERT_CHAR]     = &&op_assert_char; \
		dispatch_table[OP_ASSERT_END]      = &&op_assert_end; \
		dispatch_table[OP_ASSERT_START]    = &&op_assert_start; \
		/* Special (0x70-0x72) */ \
		dispatch_table[OP_SYSLOG_PRI]      = &&op_syslog_pri; \
		dispatch_table[OP_SYSLOG_TS]       = &&op_syslog_ts; \
		dispatch_table[OP_CEF_HDR]         = &&op_cef_hdr; \
		dispatch_table[OP_V2_IPTABLES]     = &&op_v2_iptables; \
		dispatch_table[OP_CEE_SYSLOG]      = &&op_cee_syslog; \
		dispatch_table[OP_CHECKPOINT_LEA]   = &&op_checkpoint_lea; \
		/* Debug (0xF0-0xFF) */ \
		dispatch_table[OP_NOP]             = &&op_nop; \
		dispatch_table[OP_DEBUG]           = &&op_debug; \
		dispatch_table[OP_INVALID]         = &&op_invalid; \
	} while (0)

/*
 * DISPATCH: jump to the handler for the current instruction at pc.
 * The prefetch of the NEXT instruction's cache line overlaps with
 * the current handler's execution.
 *
 * Two safety guards run before every fetch (security audit #4, #7):
 *   - PC bounds: a program that runs off the end (e.g. missing HALT,
 *     or a wild jump target) must not perform an OOB read of
 *     prog->code[pc] and a wild indirect jump. This restores parity
 *     with the legacy switch path (vm_exec_instr), which guards pc.
 *   - Instruction limit: an always-succeeding self-loop (e.g. JUMP
 *     offset=0) never reaches the backtrack: label, so the limit must
 *     be enforced here on the dispatch path to bound execution.
 *
 * Requires `vm` and `MAX_INSTRUCTIONS` in scope (ln_vm_continue).
 */
#define DISPATCH() \
	do { \
		if (UNLIKELY(pc >= prog->code_len)) { \
			vm->pc = pc; vm->ip = ip; \
			vm->error = "PC out of bounds"; \
			return LN_VM_ERROR; \
		} \
		if (UNLIKELY(++vm->instr_count > MAX_INSTRUCTIONS)) { \
			vm->pc = pc; vm->ip = ip; \
			vm->error = "instruction limit exceeded"; \
			return LN_VM_LIMIT; \
		} \
		PREFETCH(&prog->code[pc + 1]); \
		goto *dispatch_table[prog->code[pc].op]; \
	} while (0)

#define DISPATCH_NEXT()    do { pc++; DISPATCH(); } while(0)
#define CASE(op)           op_##op:
#define NEXT()             DISPATCH_NEXT()
#define BACKTRACK()        goto backtrack

#else /* !LN_VM_COMPUTED_GOTO (MSVC fallback) */

#define DISPATCH_INIT()    /* nothing */
#define DISPATCH()         continue
#define DISPATCH_NEXT()    do { pc++; continue; } while(0)
#define CASE(op)           case op:
#define NEXT()             break
#define BACKTRACK()        goto backtrack

#endif /* LN_VM_COMPUTED_GOTO */

/*============================================================================
 * Branch Prediction Hints
 *============================================================================*/

#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define PREFETCH(addr)    __builtin_prefetch((addr), 0, 3) /* read, high locality */
#define PREFETCH_W(addr)  __builtin_prefetch((addr), 1, 3) /* write, high locality */
#else
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#define PREFETCH(addr)   /* nothing */
#define PREFETCH_W(addr) /* nothing */
#endif

/*============================================================================
 * Hot/Cold Function Attributes
 *============================================================================*/

#if defined(__GNUC__) || defined(__clang__)
#define HOT_FUNC    __attribute__((hot))
#define COLD_FUNC   __attribute__((cold))
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define NOINLINE    __attribute__((noinline))
#else
#define HOT_FUNC
#define COLD_FUNC
#define ALWAYS_INLINE inline
#define NOINLINE
#endif

#endif /* LIBLOGNORM_TURBO_VM_OPT_H_INCLUDED */
