/*
 * Strict parity test for the "maxval" bound on the TurboVM fast path.
 *
 * A number parser may carry a bound, and the standard parser refuses a value
 * above it. Two rule alternatives that differ only in that bound are told
 * apart by it, so a fast path that ignores it does not merely accept one extra
 * message: it matches the wrong alternative and every field of that rule is
 * bound to the wrong part of the line.
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

/* A refused message carries "unparsed-data" on both engines. */
static int
refused(const char *doc)
{
	return strstr(doc, "unparsed-data") != NULL
	    || strcmp(doc, "<declined>") == 0;
}

static void
value_of(const char *doc, const char *name, char *out, size_t outsz)
{
	char pat[128];
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
	while(*p != '\0' && *p != '"' && *p != ',' && *p != '}' && i + 1 < outsz)
		out[i++] = *p++;
	out[i] = '\0';
}

static void
compare(const char *what, const char *rb, const char *msg, const char *name)
{
	char wdoc[2048], tdoc[2048], wval[128], tval[128];

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
		return; /* both refused: that is the agreement being tested */
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
	static const char *const bounded =
		"rule=:%n:{\"type\":\"number\",\"maxval\":\"31\"}% end\n";
	/*
	 * Two alternatives separated only by the bound. A leading value above it
	 * has to fall through to the second, which names the fields differently.
	 */
	static const char *const alt =
		"type=@ts:%day:{\"type\":\"number\",\"maxval\":\"31\"}%/%rest:number%\n"
		"type=@ts:%year:number%/%rest:number%\n"
		"rule=:%t:@ts% end\n";
	static const char *const hexbound =
		"rule=:%n:{\"type\":\"hexnumber\",\"maxval\":\"255\"}% end\n";
	static const char *const unbounded =
		"rule=:%n:number% end\n";

	/* At or below the bound both accept; above it both refuse. */
	compare("bounded/below",   bounded, "5 end",    "n");
	compare("bounded/at",      bounded, "31 end",   "n");
	compare("bounded/above",   bounded, "32 end",   "n");
	compare("bounded/far",     bounded, "2013 end", "n");

	/* The bound decides which alternative matches, so it decides the names. */
	compare("alt/day",  alt, "5/12 end",    "day");
	compare("alt/year", alt, "2013/12 end", "year");
	compare("alt/no-day-when-year",  alt, "2013/12 end", "day");
	compare("alt/no-year-when-day",  alt, "5/12 end",    "year");

	/* hexnumber carries the same bound. */
	compare("hex/below", hexbound, "ff end",   "n");
	compare("hex/above", hexbound, "1ff end",  "n");

	/* No bound means no constraint. */
	compare("unbounded", unbounded, "99999 end", "n");

	printf("%d comparisons, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
