/*
 * Strict parity test for the TurboVM date opcode.
 *
 * The VM must not build a json_object per message, so it converts dates through
 * ln_turbo_date2unix() rather than through the standard parser's value path.
 * That is only safe if both paths agree exactly, for every accepted spelling of
 * a date and for both timestamp formats. This test drives the two paths over
 * the same inputs and compares the consumed length and the epoch value.
 *
 * This file is part of the liblognorm project, released under ASL 2.0.
 */
#include "config.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "liblognorm.h"
#include "lognorm.h"
#include "pdag.h"
#include "parser.h"
#include "turbo.h"
#include "turbo_vm.h"

#define FMT_UX     2
#define FMT_UX_MS  3

static int failures;
static int checks;

/* The parser's own data struct is private; its only member is the format. */
struct date_pdata { int fmt_mode; };

/* Walker path: the standard parser, which yields a json_object. */
static int
walker_date(const int kind, const char *str, const int fmt,
	size_t *parsed, int64_t *ts)
{
	struct date_pdata pd;
	struct json_object *val = NULL;
	npb_t npb;
	size_t offs = 0;
	int r;

	memset(&npb, 0, sizeof(npb));
	npb.str = str;
	npb.strLen = strlen(str);
	pd.fmt_mode = fmt;
	*parsed = 0;

	r = (kind == 1)
	  ? ln_v2_parseRFC5424Date(&npb, &offs, &pd, NULL, parsed, &val)
	  : ln_v2_parseRFC3164Date(&npb, &offs, &pd, NULL, parsed, &val);

	if(r != 0 || val == NULL) {
		if(val != NULL) json_object_put(val);
		return -1;
	}
	*ts = json_object_get_int64(val);
	json_object_put(val);
	return 0;
}

static void
compare(const int kind, const char *str, const int fmt)
{
	size_t wparsed = 0, tparsed = 0;
	int64_t wts = 0, tts = 0;
	int wrc, trc;

	checks++;
	wrc = walker_date(kind, str, fmt, &wparsed, &wts);
	trc = ln_turbo_date2unix(kind, str, strlen(str),
			fmt == FMT_UX_MS, &tparsed, &tts);

	if((wrc == 0) != (trc == 0)) {
		printf("FAIL rfc%s fmt=%d '%s': walker rc=%d, turbo rc=%d\n",
			kind == 1 ? "5424" : "3164", fmt, str, wrc, trc);
		failures++;
		return;
	}
	if(wrc != 0)                /* both rejected: agreement is what matters */
		return;
	if(wparsed != tparsed) {
		printf("FAIL rfc%s fmt=%d '%s': consumed %zu vs %zu\n",
			kind == 1 ? "5424" : "3164", fmt, str, wparsed, tparsed);
		failures++;
		return;
	}
	if(wts != tts) {
		printf("FAIL rfc%s fmt=%d '%s': %" PRId64 " vs %" PRId64 "\n",
			kind == 1 ? "5424" : "3164", fmt, str, wts, tts);
		failures++;
	}
}

static void
both_formats(const int kind, const char *str)
{
	compare(kind, str, FMT_UX);
	compare(kind, str, FMT_UX_MS);
}

int
main(void)
{
	static const char *const rfc3164[] = {
		"Aug 10 12:00:00 x", "Jan  1 00:00:00 x", "Feb 29 23:59:60 x",
		"Mar  5 01:02:03 x", "Apr 30 12:00:00 x", "May 15 06:07:08 x",
		"Jun 26 2019 09:22:07 x",          /* year variant */
		"Jul  4 12:00:00: x",              /* tolerated extra colon */
		"Sep  9 09:09:09 x", "Oct 31 18:30:00 x", "Nov  1 00:00:01 x",
		"Dec 25 23:59:59 x",
		"Xyz 10 12:00:00 x",               /* rejected by both */
		"Aug 99 12:00:00 x", "Aug 10 25:00:00 x", "Aug 10 12:60:00 x",
		"Aug", "", NULL
	};
	static const char *const rfc5424[] = {
		"2026-08-10T12:00:00Z x", "2026-08-10T12:00:00.1Z x",
		"2026-08-10T12:00:00.25Z x", "2026-08-10T12:00:00.250Z x",
		"2026-08-10T12:00:00.250500Z x", "2026-08-10T12:00:00.7Z x",
		"2026-08-10T12:00:00+02:30 x", "2026-08-10T12:00:00-05:00 x",
		"2026-08-10T12:00:00.125+01:00 x",
		"2003-9-1T1:0:0Z x",               /* tolerated loose spelling */
		"2024-02-29T00:00:00Z x",          /* leap day */
		"1970-01-01T00:00:00Z x",
		"2026-13-10T12:00:00Z x",          /* rejected by both */
		"2026-08-10T24:00:00Z x", "2026-08-10T12:00:00 x",
		"2026-08-10", "", NULL
	};
	int i;

	for(i = 0 ; rfc3164[i] != NULL ; ++i)
		both_formats(0, rfc3164[i]);
	for(i = 0 ; rfc5424[i] != NULL ; ++i)
		both_formats(1, rfc5424[i]);

	printf("%d comparisons, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
