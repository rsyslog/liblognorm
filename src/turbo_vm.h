/**
 * @file turbo_vm.h
 * @brief Virtual machine for executing TurboVM bytecode
 *//*
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_TURBO_VM_H_INCLUDED
#define	LIBLOGNORM_TURBO_VM_H_INCLUDED

#include "turbo_opcode.h"
#include "turbo_result_fast.h"
#include "turbo_arena.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/** Maximum fork stack depth (backtracking limit) */
#define LN_VM_MAX_FORKS 64

/** Maximum call stack depth (subroutine nesting) */
#define LN_VM_MAX_CALLS 16

/** Maximum field context depth (for ".." substitution) */
#define LN_VM_MAX_FIELD_CTX 16

/** VM return codes */
#define LN_VM_OK        0      /**< Match successful */
#define LN_VM_NOMATCH  -1      /**< No rule matched */
#define LN_VM_ERROR    -2      /**< Execution error */
#define LN_VM_LIMIT    -3      /**< Resource limit exceeded */

/*============================================================================
 * Program Structure
 *============================================================================*/

typedef struct {
	const ln_instr_t *code;     /**< Instruction array */
	uint32_t          code_len; /**< Number of instructions */
	const char       *name;     /**< Program name (optional) */
	uint32_t          flags;    /**< Program flags */
} ln_program_t;

/*============================================================================
 * Field Context (for ".." substitution)
 *============================================================================*/

/**
 * @brief Field name context for custom type inheritance.
 *
 * When entering %net_src_ip:@netscaler_ip%, push "net_src_ip".
 * If the custom type has %..:ipv4%, resolve ".." to "net_src_ip".
 */
typedef struct {
	const char *name;           /**< Parent field name */
	uint16_t    name_len;       /**< Name length */
	uint8_t     is_nested;      /**< Create nested object for this context */
	uint8_t     _pad;
} ln_field_ctx_t;

/*============================================================================
 * Fork State
 *============================================================================*/

typedef struct {
	uint32_t    pc;             /**< Saved program counter */
	const char *ip;             /**< Saved input position */
	uint8_t     n_fields;       /**< Saved field count */
	uint8_t     n_tags;         /**< Saved tag count */
	uint8_t     call_sp;        /**< Saved call stack pointer */
	uint8_t     field_ctx_sp;   /**< Saved field context stack pointer */
} ln_fork_t;

/*============================================================================
 * VM State
 *============================================================================*/

typedef struct {
	/* Program */
	const ln_program_t *prog;   /**< Current program */

	/* Input */
	const char *input;          /**< Input buffer (log message) */
	const char *input_end;      /**< End of input */

	/* Execution state */
	uint32_t    pc;             /**< Program counter (instruction index) */
	const char *ip;             /**< Input pointer */

	/* Backtracking */
	ln_fork_t   forks[LN_VM_MAX_FORKS];
	uint32_t    fork_sp;        /**< Fork stack pointer */

	/* Call stack (for subroutines) */
	uint32_t    calls[LN_VM_MAX_CALLS];
	uint32_t    call_sp;        /**< Call stack pointer */

	/* Field context stack (for ".." substitution) */
	ln_field_ctx_t field_ctx[LN_VM_MAX_FIELD_CTX];
	uint32_t    field_ctx_sp;   /**< Field context stack pointer */

	/* Output - FAST RESULT */
	ln_fast_result_t *result;   /**< Parse result (optimized) */
	ln_arena_t  *arena;         /**< Arena for overflow storage */
	void        *c_locale;      /**< C locale_t for locale-independent number
	                              *  parsing (strtod_l); owned, created in
	                              *  ln_vm_init, freed in ln_vm_destroy. void* so
	                              *  this header needs no <locale.h>. */

	/* Statistics */
	uint64_t    instr_count;    /**< Instructions executed */
	uint64_t    backtrack_count;/**< Backtrack operations */

	/* Matched rule info */
	const char *matched_rule;   /**< Rule ID if matched */

	/* Error info */
	const char *error;          /**< Error message if failed */
	char        error_buf[64];  /**< Per-instance error buffer (thread-safe) */
} ln_vm_t;

/*============================================================================
 * VM Lifecycle
 *============================================================================*/

int ln_vm_init(ln_vm_t *vm, ln_arena_t *arena);
void ln_vm_destroy(ln_vm_t *vm);
void ln_vm_reset(ln_vm_t *vm);

/*============================================================================
 * Execution
 *============================================================================*/

int ln_vm_exec(ln_vm_t *vm, const ln_program_t *prog,
			   const char *input, size_t len,
			   ln_fast_result_t *result);

int ln_vm_continue(ln_vm_t *vm);

/*============================================================================
 * Debug
 *============================================================================*/

void ln_vm_dump(const ln_vm_t *vm, FILE *fp);
void ln_program_disasm(const ln_program_t *prog, FILE *fp);
void ln_vm_set_trace(ln_vm_t *vm, bool enable);

/*============================================================================
 * Inline Helpers
 *============================================================================*/

static inline ln_program_t
ln_program_make(const ln_instr_t *code, uint32_t code_len, const char *name)
{
	ln_program_t p = {0};
	p.code = code;
	p.code_len = code_len;
	p.name = name;
	return p;
}

static inline bool
ln_vm_matched(const ln_vm_t *vm)
{
	return vm && vm->matched_rule != NULL;
}

static inline size_t
ln_vm_remaining(const ln_vm_t *vm)
{
	if (!vm || vm->ip >= vm->input_end) return 0;
	return (size_t)(vm->input_end - vm->ip);
}

static inline size_t
ln_vm_consumed(const ln_vm_t *vm)
{
	if (!vm || !vm->input) return 0;
	return (size_t)(vm->ip - vm->input);
}

static inline const char *
ln_vm_get_field_context(const ln_vm_t *vm)
{
	if (!vm || vm->field_ctx_sp == 0) return NULL;
	return vm->field_ctx[vm->field_ctx_sp - 1].name;
}

#ifdef __cplusplus
}
#endif

#endif /* LIBLOGNORM_TURBO_VM_H_INCLUDED */
