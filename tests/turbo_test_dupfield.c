/*
 * Strict parity test for repeated field names on the TurboVM fast path.
 *
 * A rulebase may bind the same name several times in one rule, so a result can
 * carry more than one field under that name. Every consumer must then agree on
 * which binding it sees. The walker answers a live lookup with the last one,
 * and so does any caller that materializes the fast result into a JSON object,
 * because adding a key replaces it. A by-name lookup on the fast result has to
 * give the same answer, or one engine reports two different values for one
 * field depending on how the caller reads it.
 *
 * This file is part of the liblognorm project, released under ASL 2.0.
 */
#include "config.h"

#include <stdio.h>
#include <string.h>

#include "liblognorm.h"
#include "lognorm.h"
#include "lognorm-turbo.h"

static int failures;
static int checks;

/* Walker: what a live consumer reads back for the name. */
static const char *
walker_lookup(const char *rulebase, const char *msg, const char *name)
{
	static char buf[256];
	ln_ctx ctx;
	struct json_object *json = NULL, *val = NULL;
	const char *s;

	buf[0] = '\0';
	if((ctx = ln_initCtx()) == NULL)
		return NULL;
	if(ln_loadSamplesFromString(ctx, rulebase) != 0) {
		ln_exitCtx(ctx);
		return NULL;
	}
	if(ln_normalize(ctx, msg, strlen(msg), &json) == 0 && json != NULL) {
		if(json_object_object_get_ex(json, name, &val) && val != NULL) {
			s = json_object_get_string(val);
			if(s != NULL)
				snprintf(buf, sizeof(buf), "%s", s);
		}
	}
	if(json != NULL)
		json_object_put(json);
	ln_exitCtx(ctx);
	return buf;
}

/* TurboVM: what the by-name accessor reports for the same name. */
static const char *
turbo_lookup(const char *rulebase, const char *msg, const char *name)
{
	static char buf[256];
	ln_ctx ctx;
	const ln_fast_result_t *res = NULL;
	const char *val = NULL;
	size_t vlen = 0;

	buf[0] = '\0';
	if((ctx = ln_initCtx()) == NULL)
		return NULL;
	ln_setCtxOpts(ctx, LN_CTXOPT_TURBO | LN_CTXOPT_TURBO_STRICT);
	if(ln_loadSamplesFromString(ctx, rulebase) != 0) {
		ln_exitCtx(ctx);
		return NULL;
	}
	if(ln_turbo_normalize_raw(ctx, (char*)msg, strlen(msg), &res) == 0 && res != NULL) {
		if(ln_fast_result_get_string(res, name, &val, &vlen) == 0 && val != NULL)
			snprintf(buf, sizeof(buf), "%.*s", (int)vlen, val);
	}
	ln_exitCtx(ctx);
	return buf;
}

static void
compare(const char *what, const char *rulebase, const char *msg,
	const char *name, const char *expected)
{
	const char *w, *t;
	char wcopy[256];

	++checks;
	w = walker_lookup(rulebase, msg, name);
	if(w == NULL) {
		printf("FAIL %s: walker did not load/normalize\n", what);
		++failures;
		return;
	}
	snprintf(wcopy, sizeof(wcopy), "%s", w);
	t = turbo_lookup(rulebase, msg, name);
	if(t == NULL) {
		printf("FAIL %s: turbo did not load/normalize\n", what);
		++failures;
		return;
	}
	if(strcmp(wcopy, t) != 0) {
		printf("FAIL %s: walker='%s' turbo='%s'\n", what, wcopy, t);
		++failures;
		return;
	}
	if(expected != NULL && strcmp(t, expected) != 0) {
		printf("FAIL %s: both gave '%s', expected '%s'\n", what, t, expected);
		++failures;
	}
}

int
main(void)
{
	static const char *rb_two =
		"rule=:%f:ipv4%:%f:number%\n";
	static const char *rb_three =
		"rule=:%f:word% %f:word% %f:word%\n";
	static const char *rb_one =
		"rule=:%f:ipv4%\n";
	static const char *rb_mixed =
		"rule=:%a:word% %f:word% %b:word% %f:word%\n";

	/* Two bindings of one name: the second must win on both engines. */
	compare("ipv4-then-number", rb_two, "192.168.1.100:443", "f", "443");
	compare("ipv4-then-number/2", rb_two, "10.0.0.1:5060", "f", "5060");

	/* Three bindings: still the last. */
	compare("three-words", rb_three, "AA BB CC", "f", "CC");

	/* A single binding must be unaffected. */
	compare("single", rb_one, "192.168.1.100", "f", "192.168.1.100");

	/* Repeats interleaved with other names must not disturb them. */
	compare("interleaved-f", rb_mixed, "AA BB CC DD", "f", "DD");
	compare("interleaved-a", rb_mixed, "AA BB CC DD", "a", "AA");
	compare("interleaved-b", rb_mixed, "AA BB CC DD", "b", "CC");

	printf("%d comparisons, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
