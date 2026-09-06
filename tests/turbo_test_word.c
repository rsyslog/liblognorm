/*
 * Strict parity test for the word parser on the TurboVM fast path.
 *
 * The standard parser ends a word at a space and at nothing else, and refuses
 * a word that is empty. The fast path ended it at a tab, a newline or a
 * carriage return as well, which cut the word short and left text the rest of
 * the rule could not match, and it accepted an empty word, which let a rule
 * match where the standard parser hands the message to the next one.
 *
 * Accepting and refusing both matter here, so both are compared.
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
	static const char *const one  = "rule=:%f:word% end\n";
	static const char *const two  = "rule=:%a:word% %b:word%\n";
	/* An alternative behind the word is reached only when the word refuses. */
	static const char *const alt =
		"rule=:%a:word% %b:word%\n"
		"rule=:%whole:rest%\n";

	/* A space ends the word. */
	compare("plain",        one, "abc end", "f");

	/* Nothing else does: these characters belong to the word. */
	compare("tab-inside",   one, "ab\tcd end", "f");
	compare("nl-inside",    one, "ab\ncd end", "f");
	compare("cr-inside",    one, "ab\rcd end", "f");

	/*
	 * An empty word is refused. A message that begins with a space is
	 * covered in turbo_test_leadws.c: that used to be a VM-wide trim, not
	 * a property of this parser.
	 */
	compare("empty-at-end",   two, "one ", "a");
	compare("empty-at-end/b", two, "one ", "b");

	/* The refusal decides which rule the message lands on. */
	compare("alt/word-wins",     alt, "one two", "a");
	compare("alt/rest-when-empty", alt, "one ",  "whole");
	compare("alt/no-a-when-empty", alt, "one ",  "a");

	printf("%d comparisons, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
