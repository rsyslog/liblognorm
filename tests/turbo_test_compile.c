/**
 * @file turbo_test_compile.c
 * @brief Compile-time contract for the TurboVM bytecode compiler.
 *
 * These tests exist because the field-type parity harness cannot see them.
 * When TurboVM declines a rulebase or a message, ln_normalize_to_str() falls
 * back to the recursive walker and produces the correct answer, so an output
 * comparison passes whether or not the bytecode engine did any work. What
 * follows asserts the compiler's own contract instead:
 *
 *   - a rulebase turbo cannot compile leaves NO runnable program behind
 *     (a truncated program has no terminating OP_HALT, and executing it
 *     burned the whole VM instruction budget on every message);
 *   - opcode-buffer sizes are not user-visible limits: long annotation
 *     values and long literal texts compile like any other;
 *   - a result too wide for the fast-result array is never silently
 *     truncated on the string path.
 *
 * @author Jérémie Jourdin / Advens
 * @copyright 2026 Advens. Released under ASL 2.0.
 */

#include "config.h"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

#ifdef ENABLE_TURBO

#include "liblognorm.h"
#include "lognorm-turbo.h"
#include "turbo_result_fast.h"   /* LN_FAST_MAX_FIELDS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__); \
        return 0; \
    } \
} while (0)

#define RUN(fn) do { \
    tests_run++; \
    printf("  %-46s", #fn); \
    if (fn()) { \
        printf("PASS\n"); \
    } else { \
        printf("FAIL\n"); \
        tests_failed++; \
    } \
} while (0)

/* Surface rulebase syntax errors: a test that cannot load its own rulebase
 * should say why rather than just failing the setup assertion. */
static void
rb_errmsg(void *cookie, const char *msg, size_t len)
{
    (void)cookie;
    fprintf(stderr, "  rulebase error: %.*s\n", (int)len, msg);
}

/* Write a rulebase to a temporary file and load it into a turbo-enabled ctx.
 * Returns NULL on setup failure. Caller frees with ln_exitCtx(). */
static ln_ctx
load_rb_opts(const char *content, unsigned extra_opts)
{
    char path[] = "/tmp/ln_turbo_compile_XXXXXX";
    int fd = mkstemp(path);
    FILE *fp;
    ln_ctx ctx;
    int rc;

    if (fd < 0) return NULL;
    fp = fdopen(fd, "w");
    if (fp == NULL) { close(fd); unlink(path); return NULL; }
    fputs(content, fp);
    fclose(fp);

    ctx = ln_initCtx();
    if (ctx == NULL) { unlink(path); return NULL; }
    ln_setErrMsgCB(ctx, rb_errmsg, NULL);
    ln_setCtxOpts(ctx, LN_CTXOPT_TURBO | extra_opts);
    rc = ln_loadSamples(ctx, path);
    if (rc != 0) {
        fprintf(stderr, "  (ln_loadSamples returned %d)\n", rc);
        ln_exitCtx(ctx);
        ctx = NULL;
    }
    unlink(path);
    return ctx;
}

/* Default: strict, so ln_normalize_to_str() does NOT fall back to the walker.
 * Without this a test asserting turbo behaviour passes even when turbo
 * declines the message, because the walker quietly produces the right answer.
 * That is the failure mode this whole file exists to rule out. */
static ln_ctx
load_rb(const char *content)
{
    return load_rb_opts(content, LN_CTXOPT_TURBO_STRICT);
}

/* For the two tests that assert the FALLBACK itself works. */
static ln_ctx
load_rb_lenient(const char *content)
{
    return load_rb_opts(content, 0);
}

/* Normalize through the public string path and return the JSON, or NULL. */
static char *
norm(ln_ctx ctx, const char *line)
{
    char *js = NULL;
    size_t jl = 0;
    if (ln_normalize_to_str(ctx, line, strlen(line), &js, &jl) != 0) return NULL;
    return js;
}

/*============================================================================
 * A rulebase that compiles cleanly is available.
 *============================================================================*/
static int
test_clean_rulebase_is_available(void)
{
    ln_ctx ctx = load_rb("version=2\nrule=:%f:word% z\n");
    char *js;

    CHECK(ctx != NULL, "ctx setup");
    CHECK(ln_turbo_is_available(ctx) == 1, "turbo should be available");
    js = norm(ctx, "hello z");
    CHECK(js != NULL, "normalize succeeded");
    CHECK(strstr(js, "\"hello\"") != NULL, "field extracted");
    free(js);
    ln_exitCtx(ctx);
    return 1;
}

/*============================================================================
 * A rulebase turbo cannot compile must leave nothing runnable.
 *
 * "repeat" is the one v2 parser with no bytecode form, so compile_node()
 * bails out part-way through emitting the program. Before the fix the
 * half-emitted code stayed in place, ln_turbo_is_available() reported ready
 * because code_len was non-zero, and the VM then ran a program with no
 * terminating OP_HALT: every message that did not match it backtracked until
 * it hit MAX_INSTRUCTIONS (100M) before falling back to the walker.
 *============================================================================*/
static int
test_failed_compile_leaves_nothing_runnable(void)
{
    ln_ctx ctx = load_rb_lenient(
        "version=2\n"
        "rule=:%f:word% ok\n"
        "rule=:x %r:repeat{ \"parser\": {\"type\":\"word\", \"name\":\".\"}, "
            "\"while\": {\"type\":\"literal\", \"text\":\" \"} }%\n");
    char *js;

    CHECK(ctx != NULL, "ctx setup");
    CHECK(ln_turbo_is_available(ctx) == 0,
          "turbo must NOT report ready after a failed compile");

    /* The walker still parses the rulebase correctly. */
    js = norm(ctx, "hello ok");
    CHECK(js != NULL, "walker still normalizes");
    CHECK(strstr(js, "\"hello\"") != NULL, "walker extracted the field");
    free(js);

    /* And a non-matching message returns promptly instead of burning the
     * VM instruction budget. */
    js = norm(ctx, "nothing matches this line at all");
    free(js);
    ln_exitCtx(ctx);
    return 1;
}

/*============================================================================
 * Opcode inline buffers are not user-visible limits.
 *
 * A named literal stores its own matched text as a static field. The inline
 * key/value buffers hold 29 bytes; anything longer is interned in the program
 * string pool. It used to abort compilation of the WHOLE rulebase, which is
 * how a 38-byte literal text turned a working rulebase into the case above.
 *============================================================================*/
static int
test_long_named_literal_compiles(void)
{
    static const char text[] = "WildFire Communications Status Changed";
    ln_ctx ctx = load_rb(
        "version=2\n"
        "rule=:%{\"name\":\"act\",\"type\":\"literal\","
        "\"text\":\"WildFire Communications Status Changed\"}%,z\n");
    char *js;

    CHECK(ctx != NULL, "ctx setup");
    CHECK(ln_turbo_is_available(ctx) == 1,
          "a long named literal must not abort compilation");

    js = norm(ctx, "WildFire Communications Status Changed,z");
    CHECK(js != NULL, "normalize succeeded");
    CHECK(strstr(js, text) != NULL, "literal text stored as a field value");
    free(js);
    ln_exitCtx(ctx);
    return 1;
}

static int
test_long_annotation_value_compiles(void)
{
    static const char val[] = "0123456789012345678901234567890123456789";
    ln_ctx ctx = load_rb(
        "version=2\n"
        "rule=t:A,%f:rest%\n"
        "annotate=t:+long=\"0123456789012345678901234567890123456789\"\n");
    char *js;

    CHECK(ctx != NULL, "ctx setup");
    CHECK(ln_turbo_is_available(ctx) == 1,
          "a long annotation value must not abort compilation");

    js = norm(ctx, "A,x");
    CHECK(js != NULL, "normalize succeeded");
    CHECK(strstr(js, val) != NULL, "annotation value emitted in full");
    free(js);
    ln_exitCtx(ctx);
    return 1;
}

/*============================================================================
 * string-to matches up to a delimiter STRING.
 *
 * The compiler used to extract a delimiter only for char-to/char-sep, leaving
 * string-to with the default single space, so it silently behaved as
 * char-to:' '. Anchored inputs hid it (the wrong parse simply failed and the
 * walker took over); this asserts the value itself.
 *============================================================================*/
static int
test_string_to_uses_its_delimiter(void)
{
    ln_ctx ctx = load_rb("version=2\nrule=:%f:string-to:XY%%r:rest%\n");
    char *js;

    CHECK(ctx != NULL, "ctx setup");
    CHECK(ln_turbo_is_available(ctx) == 1, "turbo available");

    /* A space appears before the delimiter: a char-to:' ' would stop at it. */
    js = norm(ctx, "a bXYc");
    CHECK(js != NULL, "normalize succeeded");
    CHECK(strstr(js, "\"a b\"") != NULL, "value runs up to the delimiter");
    free(js);
    ln_exitCtx(ctx);
    return 1;
}

/*============================================================================
 * A result wider than the fast-result array is never silently truncated.
 *
 * The vehicle is "%.:json%", which inlines every object key as its own field.
 * A chain of char-sep fields cannot reach the cap: compile_node() recurses
 * once per parser and once per literal and gives up past depth 200, so a rule
 * runs out of compile depth around 99 sequential fields, well before
 * LN_FAST_MAX_FIELDS. A test built on such a chain never compiles, the walker
 * quietly does all the work, and it passes whatever the cap is set to.
 *============================================================================*/

/* Build "{"k000":0,...}" with n keys, plus the matching rulebase. */
static ln_ctx
wide_json_ctx(void)
{
    return load_rb("version=2\nrule=:%.:json%\n");
}

/* Same rulebase, but allowed to fall back: used where the point IS the
 * fallback. */
static ln_ctx
wide_json_ctx_lenient(void)
{
    return load_rb_lenient("version=2\nrule=:%.:json%\n");
}

static void
wide_json_input(char *buf, size_t bufsz, int n)
{
    int i, off = 0;
    off += snprintf(buf + off, bufsz - off, "{");
    for (i = 0; i < n; i++)
        off += snprintf(buf + off, bufsz - off, "%s\"k%03d\":%d",
                        i ? "," : "", i, i);
    snprintf(buf + off, bufsz - off, "}");
}

/* Exactly LN_FAST_MAX_FIELDS fields fit, and turbo itself serves them. */
static int
test_cap_is_reachable(void)
{
    char line[16384];
    const ln_fast_result_t *res = NULL;
    ln_ctx ctx = wide_json_ctx();

    CHECK(ctx != NULL, "ctx setup");
    CHECK(ln_turbo_is_available(ctx) == 1, "turbo available");

    wide_json_input(line, sizeof(line), LN_FAST_MAX_FIELDS);
    CHECK(ln_turbo_normalize_raw(ctx, line, strlen(line), &res) == 0,
          "turbo normalized a result exactly at the cap");
    CHECK(res != NULL, "result returned");
    CHECK(ln_fast_result_is_truncated(res) == 0, "not truncated at the cap");
    CHECK(ln_fast_result_field_count(res) == LN_FAST_MAX_FIELDS,
          "every field stored");
    ln_exitCtx(ctx);
    return 1;
}

/* Past the cap the raw API reports truncation, and the string path refuses
 * the result rather than emitting a document with fields missing. */
static int
test_past_cap_is_not_silently_truncated(void)
{
    const int n = LN_FAST_MAX_FIELDS + 50;
    char line[16384], want[64];
    const ln_fast_result_t *res = NULL;
    ln_ctx ctx = wide_json_ctx_lenient();
    char *js;

    CHECK(ctx != NULL, "ctx setup");
    wide_json_input(line, sizeof(line), n);

    CHECK(ln_turbo_normalize_raw(ctx, line, strlen(line), &res) == 0,
          "raw API still returns a result");
    CHECK(ln_fast_result_is_truncated(res) == 1,
          "raw API reports the truncation to its caller");

    /* The string path has nowhere to report it, so it must refuse and let
     * the walker produce the complete document. */
    js = norm(ctx, line);
    CHECK(js != NULL, "normalize succeeded");
    snprintf(want, sizeof(want), "\"k%03d\":%d", n - 1, n - 1);
    CHECK(strstr(js, want) != NULL, "the last field past the cap survived");
    free(js);
    ln_exitCtx(ctx);
    return 1;
}

int
main(void)
{
    printf("=== TurboVM compiler contract ===\n");
    RUN(test_clean_rulebase_is_available);
    RUN(test_failed_compile_leaves_nothing_runnable);
    RUN(test_long_named_literal_compiles);
    RUN(test_long_annotation_value_compiles);
    RUN(test_string_to_uses_its_delimiter);
    RUN(test_cap_is_reachable);
    RUN(test_past_cap_is_not_silently_truncated);

    printf("\nTests run: %d, failed: %d\n", tests_run, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

#else /* !ENABLE_TURBO */

int main(void)
{
    printf("Turbo mode not enabled, skipping tests.\n");
    return 0;
}

#endif /* ENABLE_TURBO */
