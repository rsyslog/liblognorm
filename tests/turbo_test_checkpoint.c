/*
 * Strict parity test for Checkpoint LEA on the TurboVM fast path.
 *
 * The walker stores a quoted value without the surrounding quotes, and a
 * semicolon inside those quotes belongs to the value. The fast path ended the
 * value at the first ';' and kept the quotes, so a value that contained a
 * semicolon left text the rest of the rule could not match, and a value that
 * did not still disagreed on every field.
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
compare(const char *what, const char *rb, const char *msg, const char *field)
{
	char wdoc[2048], tdoc[2048], wval[256], tval[256];

	++checks;
	normalize(0, rb, msg, wdoc, sizeof(wdoc));
	normalize(1, rb, msg, tdoc, sizeof(tdoc));
	value_of(wdoc, field, wval, sizeof(wval));
	value_of(tdoc, field, tval, sizeof(tval));
	if(strcmp(wval, tval) != 0) {
		printf("FAIL %s [%s]: walker='%s' turbo='%s'\n", what, field, wval, tval);
		++failures;
	}
}

int
main(void)
{
	static const char *const term =
		"rule=:[ %{\"name\":\"f\",\"type\":\"checkpoint-lea\",\"terminator\":\"]\"}%]\n";
	static const char *const named =
		"rule=:%f:checkpoint-lea%\n";

	/* Unquoted, terminator, with and without a trailing semicolon. */
	compare("unquoted/semi",    term, "[ tcp_flags: RST-ACK; src: 192.168.0.1; ]", "src");
	compare("unquoted/no-semi", term, "[ tcp_flags: RST-ACK; src: 192.168.0.1 ]", "src");

	/* Quoted values drop the quotes. */
	compare("quoted/semi",    term, "[ tcp_flags:\"RST-ACK\"; src:\"192.168.0.1\"; ]", "src");
	compare("quoted/no-semi", term, "[ tcp_flags:\"RST-ACK\"; src:\"192.168.0.1\" ]", "src");

	/* A semicolon inside quotes belongs to the value. */
	compare("quoted/semi-in-value", term, "[ desc:\"a;b\"; k:\"c\" ]", "desc");
	compare("quoted/semi-in-value/k", term, "[ desc:\"a;b\"; k:\"c\" ]", "k");

	/* No terminator: the original pair form. */
	compare("plain", named, "tcp_flags: RST-ACK; src: 192.168.0.1;", "src");

	printf("%d comparisons, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
