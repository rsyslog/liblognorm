/*
 * Strict parity test for annotations on the TurboVM fast path.
 *
 * A rule may carry several tags, and two of their annotations may write the
 * same field name. The standard parser applies an annotation with a replacing
 * add while walking the tag bucket from its end, so one value survives and it
 * is the one from the first tag declared on the rule. The fast path resolves
 * annotations when the program is compiled, so it has to reach that same
 * single value rather than leave two entries under one name.
 *
 * This file is part of the liblognorm project, released under ASL 2.0.
 */
#include "config.h"

#include <stdio.h>
#include <string.h>

#include "liblognorm.h"
#include "lognorm.h"

static int failures;
static int checks;

/* Count how many times the document carries `name`, and read the value. */
static int
field(const char *doc, const char *name, char *out, size_t outsz)
{
	char pat[128];
	const char *p = doc;
	int n = 0;

	snprintf(pat, sizeof(pat), "\"%s\":", name);
	snprintf(out, outsz, "<absent>");
	while((p = strstr(p, pat)) != NULL) {
		const char *v = p + strlen(pat);
		++n;
		while(*v == ' ')
			++v;
		if(*v == '"') {
			size_t i = 0;
			++v;
			while(*v != '\0' && *v != '"' && i + 1 < outsz)
				out[i++] = *v++;
			out[i] = '\0';
		}
		p += strlen(pat);
	}
	return n;
}

static int
normalize(const int turbo, const char *rb, const char *msg,
	char *out, size_t outsz)
{
	ln_ctx ctx;
	struct json_object *json = NULL;

	snprintf(out, outsz, "<declined>");
	if((ctx = ln_initCtx()) == NULL)
		return -1;
	if(turbo)
		ln_setCtxOpts(ctx, LN_CTXOPT_TURBO | LN_CTXOPT_TURBO_STRICT);
	if(ln_loadSamplesFromString(ctx, rb) != 0) {
		ln_exitCtx(ctx);
		return -1;
	}
	if(turbo) {
		char *rendered = NULL;
		size_t len = 0;
		if(ln_normalize_to_str(ctx, msg, strlen(msg), &rendered, &len) == 0
		   && rendered != NULL) {
			snprintf(out, outsz, "%s", rendered);
			free(rendered);
		}
	} else if(ln_normalize(ctx, msg, strlen(msg), &json) == 0 && json != NULL) {
		const char *s = json_object_to_json_string_ext(json,
					JSON_C_TO_STRING_PLAIN);
		if(s != NULL)
			snprintf(out, outsz, "%s", s);
	}
	if(json != NULL)
		json_object_put(json);
	ln_exitCtx(ctx);
	return 0;
}

static void
compare(const char *what, const char *rb, const char *msg, const char *name,
	const char *expected)
{
	char wdoc[4096], tdoc[4096], wval[256], tval[256];
	int wn, tn;

	++checks;
	normalize(0, rb, msg, wdoc, sizeof(wdoc));
	normalize(1, rb, msg, tdoc, sizeof(tdoc));
	wn = field(wdoc, name, wval, sizeof(wval));
	tn = field(tdoc, name, tval, sizeof(tval));

	if(tn > 1) {
		printf("FAIL %s: turbo carries '%s' %d times, one expected\n",
			what, name, tn);
		++failures;
		return;
	}
	if(wn != tn) {
		printf("FAIL %s: '%s' appears %d time(s) in the walker, %d in turbo\n",
			what, name, wn, tn);
		++failures;
		return;
	}
	if(strcmp(wval, tval) != 0) {
		printf("FAIL %s [%s]: walker='%s' turbo='%s'\n", what, name, wval, tval);
		++failures;
		return;
	}
	if(expected != NULL && strcmp(tval, expected) != 0) {
		printf("FAIL %s: both gave '%s', expected '%s'\n", what, tval, expected);
		++failures;
	}
}

int
main(void)
{
	/* Two tags whose annotations collide on one field name. */
	static const char *const collide =
		"annotate=t_a:+f=\"A\"\n"
		"annotate=t_b:+f=\"B\"\n"
		"rule=t_a,t_b:test %x:word%\n";
	/* Reversed declaration order must pick the other one. */
	static const char *const collide_rev =
		"annotate=t_a:+f=\"A\"\n"
		"annotate=t_b:+f=\"B\"\n"
		"rule=t_b,t_a:test %x:word%\n";
	/* Three tags colliding. */
	static const char *const collide3 =
		"annotate=t_a:+f=\"A\"\n"
		"annotate=t_b:+f=\"B\"\n"
		"annotate=t_c:+f=\"C\"\n"
		"rule=t_a,t_b,t_c:test %x:word%\n";
	/* Annotations on distinct names must all survive. */
	static const char *const distinct =
		"annotate=t_a:+one=\"1\"\n"
		"annotate=t_b:+two=\"2\"\n"
		"rule=t_a,t_b:test %x:word%\n";
	/* A single annotation is the ordinary case. */
	static const char *const single =
		"annotate=t_a:+f=\"A\"\n"
		"rule=t_a:test %x:word%\n";

	compare("collide/first-wins",  collide,     "test hello", "f", "A");
	compare("collide/reversed",    collide_rev, "test hello", "f", "B");
	compare("collide/three-tags",  collide3,    "test hello", "f", "A");
	compare("distinct/one",        distinct,    "test hello", "one", "1");
	compare("distinct/two",        distinct,    "test hello", "two", "2");
	compare("single",              single,      "test hello", "f", "A");
	/* The parsed field must be untouched by any of this. */
	compare("parsed-field",        collide,     "test hello", "x", "hello");

	printf("%d comparisons, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
