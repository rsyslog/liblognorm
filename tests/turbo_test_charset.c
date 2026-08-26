/*
 * Strict parity test for a char-to / char-sep terminator SET.
 *
 * Both parsers take a set of terminator characters and end the field at
 * whichever member appears first. The fast path used to keep only the first
 * character of the set and drop the rest, so a field ended by any other member
 * either ran on to the one that was kept or never matched, and the rule then
 * failed and handed the message to a different one.
 *
 * A single-character set is the common case and takes a different path in the
 * VM, so it is covered here too as a control.
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

static void
normalize(const int turbo, const char *rb, const char *msg,
	char *out, size_t outsz)
{
	ln_ctx ctx;
	struct json_object *json = NULL;

	snprintf(out, outsz, "<declined>");
	if((ctx = ln_initCtx()) == NULL)
		return;
	if(turbo)
		ln_setCtxOpts(ctx, LN_CTXOPT_TURBO | LN_CTXOPT_TURBO_STRICT);
	if(ln_loadSamplesFromString(ctx, rb) != 0) {
		ln_exitCtx(ctx);
		return;
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
}

static void
value_of(const char *doc, const char *name, char *out, size_t outsz)
{
	char pat[64];
	const char *p;
	size_t i = 0;

	snprintf(pat, sizeof(pat), "\"%s\":", name);
	snprintf(out, outsz, "<absent>");
	if(strstr(doc, "unparsed-data") != NULL || (p = strstr(doc, pat)) == NULL)
		return;
	p += strlen(pat);
	while(*p == ' ')
		++p;
	if(*p == '"')
		++p;
	while(*p != '\0' && *p != '"' && i + 1 < outsz)
		out[i++] = *p++;
	out[i] = '\0';
}

static void
compare(const char *what, const char *rb, const char *msg)
{
	char wdoc[2048], tdoc[2048], wval[256], tval[256];

	++checks;
	normalize(0, rb, msg, wdoc, sizeof(wdoc));
	normalize(1, rb, msg, tdoc, sizeof(tdoc));
	value_of(wdoc, "f", wval, sizeof(wval));
	value_of(tdoc, "f", tval, sizeof(tval));
	if(strcmp(wval, tval) != 0) {
		printf("FAIL %s: walker='%s' turbo='%s'\n", what, wval, tval);
		++failures;
	}
}

int
main(void)
{
	static const char *const one =
		"rule=:%f:{\"type\":\"char-to\",\"extradata\":\",\"}%%tail:rest%\n";
	static const char *const two =
		"rule=:%f:{\"type\":\"char-to\",\"extradata\":\",;\"}%%tail:rest%\n";
	static const char *const three =
		"rule=:%f:{\"type\":\"char-to\",\"extradata\":\",;|\"}%%tail:rest%\n";
	static const char *const sep =
		"rule=:%f:{\"type\":\"char-sep\",\"extradata\":\",;\"}%%tail:rest%\n";

	/* Control: a one-character set must behave exactly as before. */
	compare("one/hit",      one, "abc,def");
	compare("one/miss",     one, "abcdef");

	/* Every member of the set ends the field, not just the first. */
	compare("two/first",    two, "abc,def");
	compare("two/second",   two, "abc;def");
	/* Whichever member appears first wins, in either order. */
	compare("two/order-a",  two, "abc;x,y");
	compare("two/order-b",  two, "abc,x;y");
	/* No member present at all: both must refuse. */
	compare("two/none",     two, "abcdef");

	compare("three/first",  three, "abc,def");
	compare("three/second", three, "abc;def");
	compare("three/third",  three, "abc|def");
	compare("three/order",  three, "abc|x;y,z");

	/* char-sep shares the opcode and the same set handling. */
	compare("sep/first",    sep, "abc,def");
	compare("sep/second",   sep, "abc;def");

	printf("%d comparisons, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
