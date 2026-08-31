/*
 * Strict parity test for a message that begins with whitespace.
 *
 * The standard parser starts at the first byte. The fast path used to skip
 * leading whitespace before it ran the first instruction, so a word or a
 * number at the start of a rule accepted a message the standard parser
 * refused, and a char-to field dropped a leading space the standard parser
 * kept. Either way the extracted document (and sometimes the chosen rule)
 * disagreed.
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
	static const char *const word =
		"rule=:%f:word%\n";
	static const char *const number =
		"rule=:%f:number%\n";
	static const char *const charto =
		"rule=:%f:char-to:,%%tail:rest%\n";
	static const char *const rest =
		"rule=:%f:rest%\n";
	static const char *const lit =
		"rule=:hello%f:rest%\n";
	static const char *const alt =
		"rule=:%a:word%\n"
		"rule=:%whole:rest%\n";

	/* A word or a number at the start of a rule refuses a leading space. */
	compare("word/space",   word,   " abc", "f");
	compare("number/space", number, " 123", "f");

	/* char-to keeps the leading space in the value. */
	compare("char-to/space", charto, " a,b", "f");

	/* rest keeps it too: the trim was not a property of any one parser. */
	compare("rest/space", rest, " abc", "f");

	/* A literal at the start of a rule does not match through a space. */
	compare("literal/space", lit, " hello", "f");

	/* The refusal decides which rule the message lands on. */
	compare("alt/space-to-rest", alt, " abc", "whole");
	compare("alt/space-no-word", alt, " abc", "a");

	/* A leading tab is not a space. The word parser keeps it. */
	compare("word/tab", word, "\tabc", "f");

	printf("%d comparisons, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
