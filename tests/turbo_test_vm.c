/**
 * @file turbo_test_vm.c
 * @brief Comprehensive test suite for ln_turbo VM execution engine
 *
 * Tests the full VM lifecycle, all instruction categories, backtracking,
 * call/ret subroutines, field context stack, and all field extraction
 * opcodes.
 *
 * @author Jérémie Jourdin / Advens
 * @copyright 2026 Advens. Released under ASL 2.0.
 */

#include "config.h"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

#ifdef ENABLE_TURBO

#include "turbo_vm.h"
#include "turbo_opcode.h"
#include "turbo_arena.h"
#include "turbo_result_fast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Test Framework
 *============================================================================*/

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__); \
        return 0; \
    } \
} while(0)

#define TEST_ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "  FAIL: %s - got %lld, expected %lld (line %d)\n", \
                msg, (long long)(a), (long long)(b), __LINE__); \
        return 0; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    tests_run++; \
    printf("Running %s... ", #test_func); \
    fflush(stdout); \
    if (test_func()) { \
        tests_passed++; \
        printf("OK\n"); \
    } else { \
        tests_failed++; \
        printf("FAILED\n"); \
    } \
} while(0)

/*============================================================================
 * Test Helpers
 *============================================================================*/

/** Common test setup: init arena + VM + result */
static void setup_vm(ln_vm_t *vm, ln_arena_t *arena, ln_fast_result_t *result)
{
    ln_arena_init_sized(arena, 8192);
    ln_vm_init(vm, arena);
    ln_fast_result_init(result, arena);
}

/** Common test teardown */
static void teardown_vm(ln_vm_t *vm, ln_arena_t *arena)
{
    ln_vm_destroy(vm);
    ln_arena_destroy(arena);
}

/** Helper: find field by name in result, return pointer or NULL */
static const ln_fast_field_t *
find_field(const ln_fast_result_t *r, const char *name)
{
    for (int i = 0; i < r->n_fields; i++) {
        if (r->fields[i].name && strcmp(r->fields[i].name, name) == 0)
            return &r->fields[i];
    }
    return NULL;
}

/** Helper: get string value from field (handles inline vs external) */
static const char *
field_str(const ln_fast_field_t *f, size_t *out_len)
{
    if (!f) { *out_len = 0; return NULL; }
    if (f->type == LN_FTYPE_STRING_INLINE) {
        *out_len = strlen(f->v.inl);
        return f->v.inl;
    } else if (f->type == LN_FTYPE_STRING) {
        *out_len = f->v.str.len;
        return f->v.str.ptr;
    }
    *out_len = 0;
    return NULL;
}

/*============================================================================
 * VM Lifecycle Tests
 *============================================================================*/

static int test_vm_init(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    TEST_ASSERT(vm.arena != NULL, "arena should be set");
    TEST_ASSERT_EQ(vm.pc, 0, "pc should be 0");
    TEST_ASSERT_EQ(vm.fork_sp, 0, "fork_sp should be 0");
    TEST_ASSERT_EQ(vm.call_sp, 0, "call_sp should be 0");
    TEST_ASSERT_EQ(vm.field_ctx_sp, 0, "field_ctx_sp should be 0");
    TEST_ASSERT(vm.matched_rule == NULL, "no match yet");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_init_null(void)
{
    ln_arena_t arena;
    ln_arena_init_sized(&arena, 4096);

    TEST_ASSERT_EQ(ln_vm_init(NULL, &arena), LN_VM_ERROR, "NULL vm");
    TEST_ASSERT_EQ(ln_vm_init((ln_vm_t[1]){{}}, NULL), LN_VM_ERROR, "NULL arena");

    ln_arena_destroy(&arena);
    return 1;
}

static int test_vm_reset(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    /* Simulate some state */
    vm.pc = 42;
    vm.fork_sp = 3;
    vm.matched_rule = "test";

    ln_vm_reset(&vm);

    TEST_ASSERT_EQ(vm.pc, 0, "pc reset");
    TEST_ASSERT_EQ(vm.fork_sp, 0, "fork_sp reset");
    TEST_ASSERT(vm.matched_rule == NULL, "matched_rule reset");
    TEST_ASSERT(vm.prog == NULL, "prog reset");

    /* Reset NULL should not crash */
    ln_vm_reset(NULL);

    teardown_vm(&vm, &arena);
    return 1;
}

/*============================================================================
 * Instruction Builder Tests
 *============================================================================*/

static int test_instruction_builders(void)
{
    /* HALT */
    ln_instr_t i = ln_i_halt();
    TEST_ASSERT_EQ(i.op, OP_HALT, "halt opcode");

    /* MATCH */
    i = ln_i_match("rule1");
    TEST_ASSERT_EQ(i.op, OP_MATCH, "match opcode");
    TEST_ASSERT(strncmp(i.data.str, "rule1", 5) == 0, "match rule name");

    /* LITERAL */
    i = ln_i_literal("hello", 5);
    TEST_ASSERT_EQ(i.op, OP_LITERAL, "literal opcode");
    TEST_ASSERT_EQ(i.aux, 5, "literal length");
    TEST_ASSERT(memcmp(i.data.str, "hello", 5) == 0, "literal content");

    /* CHAR */
    i = ln_i_char(':');
    TEST_ASSERT_EQ(i.op, OP_CHAR, "char opcode");
    TEST_ASSERT_EQ(i.data.str[0], ':', "char value");

    /* JUMP */
    i = ln_i_jump(5);
    TEST_ASSERT_EQ(i.op, OP_JUMP, "jump opcode");
    TEST_ASSERT_EQ(i.data.jump.offset, 5, "jump offset");

    /* FORK */
    i = ln_i_fork(3);
    TEST_ASSERT_EQ(i.op, OP_FORK, "fork opcode");
    TEST_ASSERT_EQ(i.data.jump.offset, 3, "fork alt offset");

    /* FAIL */
    i = ln_i_fail();
    TEST_ASSERT_EQ(i.op, OP_FAIL, "fail opcode");

    /* FIELD */
    i = ln_i_field(OP_FIELD_WORD, "hostname");
    TEST_ASSERT_EQ(i.op, OP_FIELD_WORD, "field_word opcode");
    TEST_ASSERT_EQ(i.flags & LN_INSTR_F_STORE, LN_INSTR_F_STORE, "store flag");
    TEST_ASSERT(strncmp(i.data.str, "hostname", 8) == 0, "field name");

    /* FIELD_CHAR_TO */
    i = ln_i_field_char_to("msg", ':');
    TEST_ASSERT_EQ(i.op, OP_FIELD_CHAR_TO, "field_char_to opcode");
    TEST_ASSERT_EQ(i.data.char_to.delim, ':', "delimiter");
    TEST_ASSERT(strncmp(i.data.char_to.name, "msg", 3) == 0, "field name");

    /* FIELD_NAME_VALUE */
    i = ln_i_field_name_value("pairs", ' ', '=', 0);
    TEST_ASSERT_EQ(i.op, OP_FIELD_NAME_VALUE, "field_name_value opcode");
    TEST_ASSERT_EQ(i.data.char_to.delim, ' ', "separator");
    TEST_ASSERT_EQ(i.data.char_to.ass, '=', "assignator");
    TEST_ASSERT_EQ(i.data.char_to.ignore_ws, 0, "ignore_ws default");

    /* FIELD_NAME_VALUE with ignore_whitespaces */
    i = ln_i_field_name_value("pairs", ',', ':', 1);
    TEST_ASSERT_EQ(i.data.char_to.ignore_ws, 1, "ignore_ws set");

    /* SKIP_SPACE */
    i = ln_i_skip_space();
    TEST_ASSERT_EQ(i.op, OP_SKIP_SPACE, "skip_space opcode");

    /* SKIP_N */
    i = ln_i_skip_n(10);
    TEST_ASSERT_EQ(i.op, OP_SKIP_N, "skip_n opcode");
    TEST_ASSERT_EQ(i.aux, 10, "skip count");

    /* TAG */
    i = ln_i_tag("syslog");
    TEST_ASSERT_EQ(i.op, OP_TAG, "tag opcode");
    TEST_ASSERT(strncmp(i.data.str, "syslog", 6) == 0, "tag name");

    /* CTX_PUSH */
    i = ln_i_ctx_push("parent");
    TEST_ASSERT_EQ(i.op, OP_CTX_PUSH, "ctx_push opcode");
    TEST_ASSERT(strncmp(i.data.str, "parent", 6) == 0, "ctx name");

    /* CTX_POP */
    i = ln_i_ctx_pop();
    TEST_ASSERT_EQ(i.op, OP_CTX_POP, "ctx_pop opcode");

    /* NOP */
    i = ln_i_nop();
    TEST_ASSERT_EQ(i.op, OP_NOP, "nop opcode");

    return 1;
}

static int test_instruction_size(void)
{
    TEST_ASSERT_EQ(sizeof(ln_instr_t), 64, "instruction must be 64 bytes");
    return 1;
}

/*============================================================================
 * Basic Execution Tests
 *============================================================================*/

static int test_vm_halt(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = { ln_i_halt() };
    ln_program_t prog = ln_program_make(code, 1, "test_halt");

    int r = ln_vm_exec(&vm, &prog, "hello", 5, &result);
    TEST_ASSERT_EQ(r, LN_VM_NOMATCH, "halt = no match");
    TEST_ASSERT(!ln_vm_matched(&vm), "not matched");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_literal_match(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_literal("hello", 5),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 2, "test_literal");

    int r = ln_vm_exec(&vm, &prog, "hello", 5, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "should match");
    TEST_ASSERT(ln_vm_matched(&vm), "should be matched");
    TEST_ASSERT(strcmp(vm.matched_rule, "rule1") == 0, "rule id");
    TEST_ASSERT_EQ(ln_vm_consumed(&vm), 5, "consumed all input");
    TEST_ASSERT_EQ(ln_vm_remaining(&vm), 0, "no remaining");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_literal_no_match(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_literal("hello", 5),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 2, "test_literal_no");

    int r = ln_vm_exec(&vm, &prog, "world", 5, &result);
    TEST_ASSERT_EQ(r, LN_VM_NOMATCH, "should not match");
    TEST_ASSERT(!ln_vm_matched(&vm), "not matched");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_char_match(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_char('A'),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 2, "test_char");

    int r = ln_vm_exec(&vm, &prog, "A", 1, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "should match");

    ln_vm_reset(&vm);
    ln_fast_result_clear(&result);
    r = ln_vm_exec(&vm, &prog, "B", 1, &result);
    TEST_ASSERT_EQ(r, LN_VM_NOMATCH, "should not match");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_nop(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_nop(),
        ln_i_nop(),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 3, "test_nop");

    int r = ln_vm_exec(&vm, &prog, "", 0, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "NOPs don't block");

    teardown_vm(&vm, &arena);
    return 1;
}

/*============================================================================
 * Jump and Fork Tests
 *============================================================================*/

static int test_vm_jump(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_jump(2),       /* [0] jump to [2] */
        ln_i_halt(),        /* [1] skipped */
        ln_i_match("rule1"),/* [2] match */
    };
    ln_program_t prog = ln_program_make(code, 3, "test_jump");

    int r = ln_vm_exec(&vm, &prog, "", 0, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "jump should skip halt");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_fork_first_path(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    /* FORK: try literal "hello", alt -> literal "world" */
    ln_instr_t code[] = {
        ln_i_fork(3),            /* [0] try [1], alt -> [3] */
        ln_i_literal("hello", 5),/* [1] */
        ln_i_match("rule_hello"),/* [2] */
        ln_i_literal("world", 5),/* [3] alt path */
        ln_i_match("rule_world"),/* [4] */
    };
    ln_program_t prog = ln_program_make(code, 5, "test_fork");

    int r = ln_vm_exec(&vm, &prog, "hello", 5, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "should match first path");
    TEST_ASSERT(strcmp(vm.matched_rule, "rule_hello") == 0, "first path rule");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_fork_second_path(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_fork(3),            /* [0] try [1], alt -> [3] */
        ln_i_literal("hello", 5),/* [1] */
        ln_i_match("rule_hello"),/* [2] */
        ln_i_literal("world", 5),/* [3] alt path */
        ln_i_match("rule_world"),/* [4] */
    };
    ln_program_t prog = ln_program_make(code, 5, "test_fork2");

    int r = ln_vm_exec(&vm, &prog, "world", 5, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "should match second path");
    TEST_ASSERT(strcmp(vm.matched_rule, "rule_world") == 0, "second path rule");
    TEST_ASSERT(vm.backtrack_count > 0, "backtrack happened");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_fork_neither(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_fork(3),            /* [0] try [1], alt -> [3] */
        ln_i_literal("hello", 5),/* [1] */
        ln_i_match("rule_hello"),/* [2] */
        ln_i_literal("world", 5),/* [3] alt path */
        ln_i_match("rule_world"),/* [4] */
    };
    ln_program_t prog = ln_program_make(code, 5, "test_fork_fail");

    int r = ln_vm_exec(&vm, &prog, "xxxxx", 5, &result);
    TEST_ASSERT_EQ(r, LN_VM_NOMATCH, "neither path matches");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_fail_opcode(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    /* FORK with FAIL on first path forces backtrack */
    ln_instr_t code[] = {
        ln_i_fork(3),       /* [0] try [1], alt -> [3] */
        ln_i_fail(),        /* [1] force backtrack */
        ln_i_halt(),        /* [2] unreachable */
        ln_i_match("alt"),  /* [3] alt path */
    };
    ln_program_t prog = ln_program_make(code, 4, "test_fail");

    int r = ln_vm_exec(&vm, &prog, "", 0, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "should take alt path");
    TEST_ASSERT(strcmp(vm.matched_rule, "alt") == 0, "alt rule matched");

    teardown_vm(&vm, &arena);
    return 1;
}

/*============================================================================
 * Skip Instruction Tests
 *============================================================================*/

static int test_vm_skip_space(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_literal("hello", 5),
        ln_i_skip_space(),
        ln_i_literal("world", 5),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 4, "test_skip_space");

    int r = ln_vm_exec(&vm, &prog, "hello   world", 13, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "skip space match");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_skip_n(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_skip_n(3),
        ln_i_literal("world", 5),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 3, "test_skip_n");

    int r = ln_vm_exec(&vm, &prog, "123world", 8, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "skip N match");

    teardown_vm(&vm, &arena);
    return 1;
}

/*============================================================================
 * Field Extraction Tests
 *============================================================================*/

static int test_vm_field_word(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_field(OP_FIELD_WORD, "hostname"),
        ln_i_char(' '),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 3, "test_field_word");

    int r = ln_vm_exec(&vm, &prog, "myhost rest", 11, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "field word match");

    const ln_fast_field_t *f = find_field(&result, "hostname");
    TEST_ASSERT(f != NULL, "field found");
    size_t vlen;
    const char *val = field_str(f, &vlen);
    TEST_ASSERT(val != NULL, "value not null");
    TEST_ASSERT_EQ(vlen, 6, "value length");
    TEST_ASSERT(memcmp(val, "myhost", 6) == 0, "value content");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_field_int(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_field(OP_FIELD_INT, "pid"),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 2, "test_field_int");

    int r = ln_vm_exec(&vm, &prog, "-42", 3, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "field int match");

    const ln_fast_field_t *f = find_field(&result, "pid");
    TEST_ASSERT(f != NULL, "field found");
    TEST_ASSERT_EQ(f->type, LN_FTYPE_INT, "type is INT");
    TEST_ASSERT_EQ(f->v.i, -42, "value is -42");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_field_rest(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_literal("prefix:", 7),
        ln_i_field(OP_FIELD_REST, "msg"),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 3, "test_field_rest");

    int r = ln_vm_exec(&vm, &prog, "prefix:everything else", 22, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "field rest match");

    const ln_fast_field_t *f = find_field(&result, "msg");
    TEST_ASSERT(f != NULL, "field found");
    size_t vlen;
    const char *val = field_str(f, &vlen);
    TEST_ASSERT(val != NULL, "value not null");
    TEST_ASSERT_EQ(vlen, 15, "rest length");
    TEST_ASSERT(memcmp(val, "everything else", 15) == 0, "rest content");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_field_char_to(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_field_char_to("key", ':'),
        ln_i_char(':'),
        ln_i_field(OP_FIELD_REST, "val"),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 4, "test_field_char_to");

    int r = ln_vm_exec(&vm, &prog, "mykey:myvalue", 13, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "field char_to match");

    const ln_fast_field_t *fk = find_field(&result, "key");
    TEST_ASSERT(fk != NULL, "key field found");
    size_t vlen;
    const char *val = field_str(fk, &vlen);
    TEST_ASSERT(memcmp(val, "mykey", 5) == 0, "key value");

    const ln_fast_field_t *fv = find_field(&result, "val");
    TEST_ASSERT(fv != NULL, "val field found");
    val = field_str(fv, &vlen);
    TEST_ASSERT(memcmp(val, "myvalue", 7) == 0, "val value");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_field_ipv4(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_field(OP_FIELD_IPV4, "src_ip"),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 2, "test_field_ipv4");

    int r = ln_vm_exec(&vm, &prog, "192.168.1.1", 11, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "field ipv4 match");

    const ln_fast_field_t *f = find_field(&result, "src_ip");
    TEST_ASSERT(f != NULL, "field found");
    size_t vlen;
    const char *val = field_str(f, &vlen);
    TEST_ASSERT(val != NULL, "value not null");
    TEST_ASSERT(memcmp(val, "192.168.1.1", 11) == 0, "ipv4 value");

    teardown_vm(&vm, &arena);
    return 1;
}

static int test_vm_field_quoted(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_field(OP_FIELD_QUOTED, "msg"),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 2, "test_field_quoted");

    int r = ln_vm_exec(&vm, &prog, "\"hello world\"", 13, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "field quoted match");

    const ln_fast_field_t *f = find_field(&result, "msg");
    TEST_ASSERT(f != NULL, "field found");

    teardown_vm(&vm, &arena);
    return 1;
}

/*============================================================================
 * Tag Tests
 *============================================================================*/

static int test_vm_tag(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_tag("syslog"),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 2, "test_tag");

    int r = ln_vm_exec(&vm, &prog, "", 0, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "should match");
    TEST_ASSERT_EQ(result.n_tags, 1, "one tag");
    TEST_ASSERT(ln_fast_has_tag(&result, "syslog"), "tag 'syslog' present");
    TEST_ASSERT(!ln_fast_has_tag(&result, "other"), "tag 'other' absent");

    teardown_vm(&vm, &arena);
    return 1;
}

/*============================================================================
 * Call/Ret (Subroutine) Tests
 *============================================================================*/

static int test_vm_call_ret(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    /* Program: match "hello", call subroutine that matches " ", then match "world" */
    ln_instr_t code[] = {
        /* [0] */ ln_i_literal("hello", 5),  /* main: match "hello" */
        /* [1] */ {.op = OP_CALL, .data.jump.offset = 3}, /* call [4] */
        /* [2] */ ln_i_literal("world", 5),  /* match "world" */
        /* [3] */ ln_i_match("rule1"),        /* done */
        /* [4] */ ln_i_char(' '),             /* subroutine: match space */
        /* [5] */ {.op = OP_RET},             /* return */
    };
    ln_program_t prog = ln_program_make(code, 6, "test_call_ret");

    int r = ln_vm_exec(&vm, &prog, "hello world", 11, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "call/ret should match");

    teardown_vm(&vm, &arena);
    return 1;
}

/*============================================================================
 * Security Regression Tests (audit #4 #5 #6 #7)
 *============================================================================*/

/* #5/#4: a JUMP whose target lands outside [0, code_len) is rejected
 * cleanly with LN_VM_ERROR rather than reading off the end of code. */
static int test_vm_jump_out_of_range(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_jump(1000),    /* [0] target far past end of program */
        ln_i_match("rule1"),/* [1] */
    };
    ln_program_t prog = ln_program_make(code, 2, "test_jump_oob");

    int r = ln_vm_exec(&vm, &prog, "", 0, &result);
    TEST_ASSERT_EQ(r, LN_VM_ERROR, "out-of-range jump must error");
    TEST_ASSERT(vm.error != NULL, "error message set");

    teardown_vm(&vm, &arena);
    return 1;
}

/* #5: a negative JUMP offset that would wrap uint32 arithmetic is caught
 * by the int64 range check (lands < 0 -> LN_VM_ERROR). */
static int test_vm_jump_negative_wrap(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_jump(-100),    /* [0] target = -100, below 0 */
        ln_i_match("rule1"),/* [1] */
    };
    ln_program_t prog = ln_program_make(code, 2, "test_jump_neg");

    int r = ln_vm_exec(&vm, &prog, "", 0, &result);
    TEST_ASSERT_EQ(r, LN_VM_ERROR, "negative jump target must error");

    teardown_vm(&vm, &arena);
    return 1;
}

/* #4: a program with no terminating HALT/MATCH (PC runs off the end via
 * NOP fall-through) is caught by the DISPATCH bounds guard, not an OOB
 * read + wild indirect jump. */
static int test_vm_pc_runs_off_end(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_nop(),         /* [0] */
        ln_i_nop(),         /* [1] -> pc advances to 2 == code_len */
    };
    ln_program_t prog = ln_program_make(code, 2, "test_runoff");

    int r = ln_vm_exec(&vm, &prog, "x", 1, &result);
    TEST_ASSERT_EQ(r, LN_VM_ERROR, "PC off-end must error, not crash");
    TEST_ASSERT(vm.error != NULL, "error message set");

    teardown_vm(&vm, &arena);
    return 1;
}

/* #7: an always-succeeding self-loop (JUMP offset=0) must hit the
 * instruction limit and return LN_VM_LIMIT instead of spinning forever. */
static int test_vm_self_loop_instruction_limit(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t code[] = {
        ln_i_jump(0),       /* [0] jump to self forever */
    };
    ln_program_t prog = ln_program_make(code, 1, "test_self_loop");

    int r = ln_vm_exec(&vm, &prog, "", 0, &result);
    TEST_ASSERT_EQ(r, LN_VM_LIMIT, "self-loop must hit instruction limit");

    teardown_vm(&vm, &arena);
    return 1;
}

/* #6b: a LITERAL whose aux length exceeds the inline buffer must not
 * over-read data.str. With no input it simply fails to match (NOMATCH),
 * and crucially performs no out-of-bounds compare. */
static int test_vm_literal_oversized_aux(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    ln_instr_t lit = ln_i_literal("hello", 5);
    lit.aux = 60000; /* malformed: far beyond LN_INSTR_MAX_INLINE */
    ln_instr_t code[] = {
        lit,                 /* [0] */
        ln_i_match("rule1"), /* [1] */
    };
    ln_program_t prog = ln_program_make(code, 2, "test_lit_oversized");

    /* Provide a long input so the REMAINING() check alone would pass;
     * the len<=inline guard is what must reject the over-read. */
    static const char big[100] = {0};
    int r = ln_vm_exec(&vm, &prog, big, sizeof(big), &result);
    TEST_ASSERT_EQ(r, LN_VM_NOMATCH, "oversized literal must not match/over-read");

    teardown_vm(&vm, &arena);
    return 1;
}

/* #6a: an inline literal builder must clamp aux to the inline capacity so
 * it can never describe more bytes than were actually stored. */
static int test_inline_literal_len_clamped(void)
{
    char huge[200];
    memset(huge, 'A', sizeof(huge));
    ln_instr_t i = ln_i_literal(huge, 200);
    TEST_ASSERT(i.aux <= LN_INSTR_MAX_INLINE, "literal aux clamped to inline size");
    return 1;
}

/* #6a: name-based builders must leave a NUL terminator even for a
 * maximally long name (no strlen over-read). */
static int test_inline_name_nul_terminated(void)
{
    char huge[200];
    memset(huge, 'B', sizeof(huge));
    huge[sizeof(huge) - 1] = '\0';

    ln_instr_t f = ln_i_field(OP_FIELD_WORD, huge);
    TEST_ASSERT(f.data.str[LN_INSTR_MAX_INLINE - 1] == '\0',
                "field name reserves NUL terminator");

    ln_instr_t c = ln_i_field_char_to(huge, ',');
    TEST_ASSERT(c.data.char_to.name[55] == '\0',
                "char_to name reserves NUL terminator");
    return 1;
}

/*============================================================================
 * Field Context Tests (".." substitution)
 *============================================================================*/

static int test_vm_field_context(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    /* Simulate custom type: push context "src_ip", extract field "..",
       then pop. The ".." should resolve to "src_ip". */
    ln_instr_t code[] = {
        ln_i_ctx_push("src_ip"),
        ln_i_field(OP_FIELD_IPV4, ".."),
        ln_i_ctx_pop(),
        ln_i_match("rule1"),
    };
    ln_program_t prog = ln_program_make(code, 4, "test_field_ctx");

    int r = ln_vm_exec(&vm, &prog, "10.0.0.1", 8, &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "context match");

    /* The field should be stored as "src_ip" (resolved from "..") */
    const ln_fast_field_t *f = find_field(&result, "src_ip");
    TEST_ASSERT(f != NULL, "field 'src_ip' found (resolved from '..')");

    teardown_vm(&vm, &arena);
    return 1;
}

/*============================================================================
 * Inline VM Helpers Tests
 *============================================================================*/

static int test_vm_inline_helpers(void)
{
    ln_vm_t vm;
    memset(&vm, 0, sizeof(vm));

    /* ln_vm_matched */
    TEST_ASSERT(!ln_vm_matched(&vm), "initially not matched");
    TEST_ASSERT(!ln_vm_matched(NULL), "NULL not matched");

    /* ln_vm_remaining */
    TEST_ASSERT_EQ(ln_vm_remaining(NULL), 0, "NULL remaining");
    TEST_ASSERT_EQ(ln_vm_remaining(&vm), 0, "no input remaining");

    /* ln_vm_consumed */
    TEST_ASSERT_EQ(ln_vm_consumed(NULL), 0, "NULL consumed");
    TEST_ASSERT_EQ(ln_vm_consumed(&vm), 0, "no input consumed");

    /* ln_vm_get_field_context */
    TEST_ASSERT(ln_vm_get_field_context(NULL) == NULL, "NULL context");
    TEST_ASSERT(ln_vm_get_field_context(&vm) == NULL, "empty context");

    return 1;
}

/*============================================================================
 * Program Make Helper
 *============================================================================*/

static int test_program_make(void)
{
    ln_instr_t code[] = { ln_i_halt() };
    ln_program_t prog = ln_program_make(code, 1, "test");

    TEST_ASSERT(prog.code == code, "code pointer");
    TEST_ASSERT_EQ(prog.code_len, 1, "code length");
    TEST_ASSERT(strcmp(prog.name, "test") == 0, "program name");

    /* NULL name */
    prog = ln_program_make(code, 1, NULL);
    TEST_ASSERT(prog.name == NULL, "NULL name ok");

    return 1;
}

/*============================================================================
 * Disassembly Tests
 *============================================================================*/

static int test_opcode_names(void)
{
    TEST_ASSERT(strcmp(ln_opcode_name(OP_HALT), "HALT") == 0, "HALT name");
    TEST_ASSERT(strcmp(ln_opcode_name(OP_MATCH), "MATCH") == 0, "MATCH name");
    TEST_ASSERT(strcmp(ln_opcode_name(OP_LITERAL), "LITERAL") == 0, "LITERAL name");
    TEST_ASSERT(strcmp(ln_opcode_name(OP_FIELD_WORD), "FIELD_WORD") == 0, "FIELD_WORD name");
    TEST_ASSERT(strcmp(ln_opcode_name(OP_FIELD_IPV4), "FIELD_IPV4") == 0, "FIELD_IPV4 name");
    TEST_ASSERT(strcmp(ln_opcode_name(OP_SKIP_SPACE), "SKIP_SPACE") == 0, "SKIP_SPACE name");
    TEST_ASSERT(strcmp(ln_opcode_name(OP_TAG), "TAG") == 0, "TAG name");
    TEST_ASSERT(strcmp(ln_opcode_name(OP_NOP), "NOP") == 0, "NOP name");
    TEST_ASSERT(strcmp(ln_opcode_name(OP_SYSLOG_PRI), "SYSLOG_PRI") == 0, "SYSLOG_PRI name");
    TEST_ASSERT(strcmp(ln_opcode_name(OP_STATIC_FIELD), "STATIC_FIELD") == 0, "STATIC_FIELD name");
    TEST_ASSERT(strcmp(ln_opcode_name(OP_CTX_PUSH), "CTX_PUSH") == 0, "CTX_PUSH name");
    TEST_ASSERT(strcmp(ln_opcode_name(OP_CTX_POP), "CTX_POP") == 0, "CTX_POP name");

    /* Unknown opcode */
    const char *name = ln_opcode_name(0x99);
    TEST_ASSERT(name != NULL, "unknown opcode returns string");

    return 1;
}

static int test_disasm(void)
{
    char buf[128];

    ln_instr_t inst = ln_i_literal("hello", 5);
    int n = ln_instr_disasm(&inst, buf, sizeof(buf));
    TEST_ASSERT(n > 0, "disasm produces output");
    TEST_ASSERT(strstr(buf, "LITERAL") != NULL, "contains LITERAL");
    TEST_ASSERT(strstr(buf, "hello") != NULL, "contains literal text");

    /* CHAR disasm */
    inst = ln_i_char(':');
    n = ln_instr_disasm(&inst, buf, sizeof(buf));
    TEST_ASSERT(n > 0, "char disasm");
    TEST_ASSERT(strstr(buf, "CHAR") != NULL, "contains CHAR");

    /* JUMP disasm */
    inst = ln_i_jump(5);
    n = ln_instr_disasm(&inst, buf, sizeof(buf));
    TEST_ASSERT(n > 0, "jump disasm");
    TEST_ASSERT(strstr(buf, "JUMP") != NULL, "contains JUMP");

    /* NULL safety */
    TEST_ASSERT_EQ(ln_instr_disasm(NULL, buf, sizeof(buf)), 0, "NULL inst");
    TEST_ASSERT_EQ(ln_instr_disasm(&inst, NULL, 128), 0, "NULL buf");
    TEST_ASSERT_EQ(ln_instr_disasm(&inst, buf, 0), 0, "zero len");

    return 1;
}

/*============================================================================
 * Syslog-Realistic Test
 *============================================================================*/

static int test_vm_syslog_like(void)
{
    ln_arena_t arena;
    ln_vm_t vm;
    ln_fast_result_t result;

    setup_vm(&vm, &arena, &result);

    /* Parse: "hostname program[1234]: message text" */
    ln_instr_t code[] = {
        ln_i_field(OP_FIELD_WORD, "hostname"),       /* [0] */
        ln_i_char(' '),                              /* [1] */
        ln_i_field_char_to("program", '['),           /* [2] */
        ln_i_char('['),                              /* [3] */
        ln_i_field(OP_FIELD_INT, "pid"),             /* [4] */
        ln_i_literal("]: ", 3),                      /* [5] */
        ln_i_field(OP_FIELD_REST, "msg"),            /* [6] */
        ln_i_tag("syslog"),                          /* [7] */
        ln_i_match("syslog_rule"),                   /* [8] */
    };
    ln_program_t prog = ln_program_make(code, 9, "syslog_like");

    const char *input = "myhost sshd[1234]: Failed password for root";
    int r = ln_vm_exec(&vm, &prog, input, strlen(input), &result);
    TEST_ASSERT_EQ(r, LN_VM_OK, "syslog-like match");

    /* Check hostname */
    const ln_fast_field_t *f = find_field(&result, "hostname");
    TEST_ASSERT(f != NULL, "hostname found");
    size_t vlen;
    const char *val = field_str(f, &vlen);
    TEST_ASSERT(memcmp(val, "myhost", 6) == 0, "hostname value");

    /* Check program */
    f = find_field(&result, "program");
    TEST_ASSERT(f != NULL, "program found");
    val = field_str(f, &vlen);
    TEST_ASSERT(memcmp(val, "sshd", 4) == 0, "program value");

    /* Check PID */
    f = find_field(&result, "pid");
    TEST_ASSERT(f != NULL, "pid found");
    TEST_ASSERT_EQ(f->v.i, 1234, "pid value");

    /* Check msg */
    f = find_field(&result, "msg");
    TEST_ASSERT(f != NULL, "msg found");
    val = field_str(f, &vlen);
    TEST_ASSERT(memcmp(val, "Failed password for root", 24) == 0, "msg value");

    /* Check tag */
    TEST_ASSERT(ln_fast_has_tag(&result, "syslog"), "syslog tag");

    /* Check rule */
    TEST_ASSERT(strcmp(result.rule_id, "syslog_rule") == 0, "rule id");
    TEST_ASSERT(result.flags & LN_FRESULT_MATCHED, "matched flag");

    teardown_vm(&vm, &arena);
    return 1;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void)
{
    printf("=== ln_vm Test Suite ===\n\n");

    printf("VM lifecycle tests:\n");
    RUN_TEST(test_vm_init);
    RUN_TEST(test_vm_init_null);
    RUN_TEST(test_vm_reset);
    printf("\n");

    printf("Instruction builder tests:\n");
    RUN_TEST(test_instruction_builders);
    RUN_TEST(test_instruction_size);
    printf("\n");

    printf("Basic execution tests:\n");
    RUN_TEST(test_vm_halt);
    RUN_TEST(test_vm_literal_match);
    RUN_TEST(test_vm_literal_no_match);
    RUN_TEST(test_vm_char_match);
    RUN_TEST(test_vm_nop);
    printf("\n");

    printf("Jump and fork tests:\n");
    RUN_TEST(test_vm_jump);
    RUN_TEST(test_vm_fork_first_path);
    RUN_TEST(test_vm_fork_second_path);
    RUN_TEST(test_vm_fork_neither);
    RUN_TEST(test_vm_fail_opcode);
    printf("\n");

    printf("Skip instruction tests:\n");
    RUN_TEST(test_vm_skip_space);
    RUN_TEST(test_vm_skip_n);
    printf("\n");

    printf("Field extraction tests:\n");
    RUN_TEST(test_vm_field_word);
    RUN_TEST(test_vm_field_int);
    RUN_TEST(test_vm_field_rest);
    RUN_TEST(test_vm_field_char_to);
    RUN_TEST(test_vm_field_ipv4);
    RUN_TEST(test_vm_field_quoted);
    printf("\n");

    printf("Tag tests:\n");
    RUN_TEST(test_vm_tag);
    printf("\n");

    printf("Call/ret tests:\n");
    RUN_TEST(test_vm_call_ret);
    printf("\n");

    printf("Security regression tests:\n");
    RUN_TEST(test_vm_jump_out_of_range);
    RUN_TEST(test_vm_jump_negative_wrap);
    RUN_TEST(test_vm_pc_runs_off_end);
    RUN_TEST(test_vm_self_loop_instruction_limit);
    RUN_TEST(test_vm_literal_oversized_aux);
    RUN_TEST(test_inline_literal_len_clamped);
    RUN_TEST(test_inline_name_nul_terminated);
    printf("\n");

    printf("Field context tests:\n");
    RUN_TEST(test_vm_field_context);
    printf("\n");

    printf("Inline helper tests:\n");
    RUN_TEST(test_vm_inline_helpers);
    RUN_TEST(test_program_make);
    printf("\n");

    printf("Disassembly tests:\n");
    RUN_TEST(test_opcode_names);
    RUN_TEST(test_disasm);
    printf("\n");

    printf("Integration tests:\n");
    RUN_TEST(test_vm_syslog_like);
    printf("\n");

    /* Summary */
    printf("=== Summary ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}

#else /* !ENABLE_TURBO */

int main(void)
{
    printf("Turbo mode not enabled, skipping tests.\n");
    return 0;
}

#endif /* ENABLE_TURBO */
