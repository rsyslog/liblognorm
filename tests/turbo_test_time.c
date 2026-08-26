/*
 * Strict parity test for the wall-clock time parsers on the TurboVM fast path.
 *
 * time-24hr and time-12hr match exactly eight characters, HH:MM:SS, and refuse
 * anything else. Both were compiled to the date opcode, which implements the
 * RFC 3164 and RFC 5424 grammars and not this one, so the field never matched.
 * The rulebase still compiled, so a rule holding such a field did not fall back
 * to the standard parser: it simply failed and another rule matched instead,
 * and every field of that message came from the wrong rule.
 *
 * Accepting and refusing therefore both matter here, and both are compared.
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

static int
refused(const char *doc)
{
	return strstr(doc, "unparsed-data") != NULL
	    || strcmp(doc, "<declined>") == 0;
}

static void
value_of(const char *doc, const char *name, char *out, size_t outsz)
{
	char pat[64];
	const char *p;
	size_t i = 0;

	snprintf(pat, sizeof(pat), "\"%s\":", name);
	snprintf(out, outsz, "<absent>");
	if((p = strstr(doc, pat)) == NULL)
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
compare(const char *what, const char *rb, const char *msg, const char *name)
{
	char wdoc[2048], tdoc[2048], wval[64], tval[64];

	++checks;
	normalize(0, rb, msg, wdoc, sizeof(wdoc));
	normalize(1, rb, msg, tdoc, sizeof(tdoc));
	if(refused(wdoc) != refused(tdoc)) {
		printf("FAIL %s: walker %s, turbo %s\n", what,
			refused(wdoc) ? "refused" : "accepted",
			refused(tdoc) ? "refused" : "accepted");
		++failures;
		return;
	}
	if(refused(wdoc))
		return;
	value_of(wdoc, name, wval, sizeof(wval));
	value_of(tdoc, name, tval, sizeof(tval));
	if(strcmp(wval, tval) != 0) {
		printf("FAIL %s [%s]: walker='%s' turbo='%s'\n", what, name, wval, tval);
		++failures;
	}
}

int
main(void)
{
	static const char *const t24 = "rule=:%t:time-24hr% end\n";
	static const char *const t12 = "rule=:%t:time-12hr% end\n";
	/*
	 * A rule whose time field fails must not quietly hand the message to
	 * another rule that binds different names to different text.
	 */
	static const char *const alt =
		"rule=:%t:time-24hr% %tail:rest%\n"
		"rule=:%other:char-to: % %tail:rest%\n";

	compare("24/midnight",   t24, "00:00:00 end", "t");
	compare("24/noon",       t24, "12:34:56 end", "t");
	compare("24/last",       t24, "23:59:59 end", "t");
	compare("24/hour-24",    t24, "24:00:00 end", "t");
	compare("24/minute-60",  t24, "12:60:00 end", "t");
	compare("24/second-60",  t24, "12:00:60 end", "t");
	compare("24/one-digit",  t24, "9:05:00 end",  "t");
	compare("24/truncated",  t24, "12:34 end",    "t");
	compare("24/no-colons",  t24, "12-34-56 end", "t");

	compare("12/one",        t12, "01:00:00 end", "t");
	compare("12/twelve",     t12, "12:59:59 end", "t");
	compare("12/thirteen",   t12, "13:00:00 end", "t");
	compare("12/zero-hour",  t12, "00:30:00 end", "t");

	/* The rule the message lands on must be the same on both engines. */
	compare("alt/time-wins", alt, "10:20:30 rest here", "t");
	compare("alt/other-when-not-a-time", alt, "notatime rest here", "other");
	compare("alt/no-other-when-time",    alt, "10:20:30 rest here", "other");

	printf("%d comparisons, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
