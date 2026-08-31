/*
 * Strict parity test for CEF escape handling on the TurboVM fast path.
 *
 * CEF writes a delimiter belonging to a value as "\|" or "\=", and a literal
 * backslash as "\\". A value therefore ends at the first delimiter that is not
 * itself escaped, and the stored string must have the escape characters
 * removed. Missing either half is worse than a wrong string: a value that does
 * not end where it should swallows the key=value pairs that follow it, and
 * those fields disappear from the event.
 *
 * The walker is the reference. This drives both engines over the same input
 * and compares the whole document.
 *
 * This file is part of the liblognorm project, released under ASL 2.0.
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "liblognorm.h"
#include "lognorm.h"

static int failures;
static int checks;

static const char *const rulebase = "rule=:%.:cef%\n";

static int
normalize(const int turbo, const char *msg, char *out, size_t outsz)
{
	ln_ctx ctx;
	struct json_object *json = NULL;
	const char *s;
	int r = -1;

	snprintf(out, outsz, "<declined>");
	if((ctx = ln_initCtx()) == NULL)
		return -1;
	if(turbo)
		ln_setCtxOpts(ctx, LN_CTXOPT_TURBO | LN_CTXOPT_TURBO_STRICT);
	if(ln_loadSamplesFromString(ctx, rulebase) != 0) {
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
			r = 0;
		}
	} else if(ln_normalize(ctx, msg, strlen(msg), &json) == 0 && json != NULL) {
		s = json_object_to_json_string_ext(json, JSON_C_TO_STRING_PLAIN);
		if(s != NULL) {
			snprintf(out, outsz, "%s", s);
			r = 0;
		}
	}
	if(json != NULL)
		json_object_put(json);
	ln_exitCtx(ctx);
	return r;
}

/* Read one extension value out of either engine's document. */
static int
extract(const char *doc, const char *key, char *out, size_t outsz)
{
	char pat[128];
	const char *at, *p;
	size_t n = 0;

	snprintf(pat, sizeof(pat), "\"%s\":", key);
	if((at = strstr(doc, pat)) == NULL) {
		snprintf(out, outsz, "<absent>");
		return -1;
	}
	p = at + strlen(pat);
	while(*p == ' ')
		++p;
	if(*p != '"') {
		snprintf(out, outsz, "<not-a-string>");
		return -1;
	}
	++p;
	while(*p != '\0' && *p != '"' && n + 1 < outsz) {
		if(*p == '\\' && p[1] != '\0') {
			++p;
			switch(*p) {
			case 'n':  out[n++] = '\n'; break;
			case 'r':  out[n++] = '\r'; break;
			case 't':  out[n++] = '\t'; break;
			case 'b':  out[n++] = '\b'; break;
			case 'f':  out[n++] = '\f'; break;
			default:   out[n++] = *p;   break; /* \\ \" \/ are literal */
			}
			++p;
			continue;
		}
		out[n++] = *p++;
	}
	out[n] = '\0';
	return 0;
}

static void
compare_buf(const char *what, const char *msg, const char *key,
	char *wdoc, size_t wdocsz, char *tdoc, size_t tdocsz,
	char *wval, size_t wvalsz, char *tval, size_t tvalsz)
{
	++checks;
	/*
	 * A refused message is a result in its own right: both engines have to
	 * refuse the same input, so normalize() reports "<declined>" rather than
	 * failing the comparison.
	 */
	(void)normalize(0, msg, wdoc, wdocsz);
	(void)normalize(1, msg, tdoc, tdocsz);
	extract(wdoc, key, wval, wvalsz);
	extract(tdoc, key, tval, tvalsz);
	if(strcmp(wval, tval) != 0) {
		printf("FAIL %s [%s]: walker='%s' turbo='%s'\n", what, key, wval, tval);
		++failures;
	}
}

static void
compare(const char *what, const char *msg, const char *key)
{
	char wdoc[4096], tdoc[4096], wval[512], tval[512];

	compare_buf(what, msg, key,
		wdoc, sizeof(wdoc), tdoc, sizeof(tdoc),
		wval, sizeof(wval), tval, sizeof(tval));
}

int
main(void)
{
	static const char *const plain =
		"CEF:0|Vend|Prod|1.0|100|Test|5|src=1.2.3.4 msg=plain dst=5.6.7.8";
	static const char *const esc_eq =
		"CEF:0|Vend|Prod|1.0|100|Test|5|src=1.2.3.4 msg=has\\=equals dst=5.6.7.8";
	static const char *const esc_bs =
		"CEF:0|Vend|Prod|1.0|100|Test|5|src=1.2.3.4 msg=back\\\\slash dst=5.6.7.8";
	static const char *const esc_many =
		"CEF:0|Vend|Prod|1.0|100|Test|5|src=1.2.3.4 msg=a\\=b\\=c dst=5.6.7.8";
	static const char *const esc_hdr =
		"CEF:0|Ve\\|nd|Pr\\\\od|1.0|100|Te\\|st|5|src=1.2.3.4 dst=5.6.7.8";
	static const char *const esc_trail =
		"CEF:0|Vend|Prod|1.0|100|Test|5|src=1.2.3.4 msg=trail\\=";
	static const char *const esc_bad =
		"CEF:0|Vend|Prod|1.0|100|Test|5|src=1.2.3.4 msg=bad\\xescape dst=5.6.7.8";
	static const char *const esc_bad_hdr =
		"CEF:0|Ve\\xnd|Prod|1.0|100|Test|5|src=1.2.3.4";
	static const char *const esc_nl =
		"CEF:0|Vend|Prod|1.0|100|Test|5|msg=line\\nbreak";
	static const char *const esc_cr =
		"CEF:0|Vend|Prod|1.0|100|Test|5|msg=car\\rriage";
	static const char *const esc_sl =
		"CEF:0|Vend|Prod|1.0|100|Test|5|msg=sl\\/ash";
	/* A trailing unescaped '=' after "\\\\" is a pair delimiter, not data. */
	static const char *const trail_eq =
		"CEF:0|Vend|Prod|1.0|100|Test|5|data=abc\\\\=";
	static const char *const trail_eq_next =
		"CEF:0|Vend|Prod|1.0|100|Test|5|data=abc\\\\= next=1";
	static const char *const one_sp =
		"CEF:0|Vend|Prod|1.0|100|Test|5|msg=hello src=1.2.3.4";
	static const char *const two_sp =
		"CEF:0|Vend|Prod|1.0|100|Test|5|msg=hello  src=1.2.3.4";
	static const char *const three_sp =
		"CEF:0|Vend|Prod|1.0|100|Test|5|msg=hello   src=1.2.3.4";

	/* An escaped value must not swallow the pairs that follow it. */
	compare("plain/msg",       plain,     "msg");
	compare("plain/dst",       plain,     "dst");
	compare("escaped-eq/msg",  esc_eq,    "msg");
	compare("escaped-eq/dst",  esc_eq,    "dst");
	compare("escaped-bs/msg",  esc_bs,    "msg");
	compare("escaped-bs/dst",  esc_bs,    "dst");
	compare("many-eq/msg",     esc_many,  "msg");
	compare("many-eq/dst",     esc_many,  "dst");

	/* Header fields carry the same escapes. */
	compare("escaped-hdr/vendor",  esc_hdr, "DeviceVendor");
	compare("escaped-hdr/product", esc_hdr, "DeviceProduct");
	compare("escaped-hdr/name",    esc_hdr, "Name");
	compare("escaped-hdr/src",     esc_hdr, "src");

	/* A trailing escape must not read past the end. */
	compare("trailing-esc/msg", esc_trail, "msg");

	/*
	 * CEF defines "\\=", "\\\\", "\\n", "\\r" and "\\/" in an extension value, and
	 * only the delimiter and the backslash in a header. Anything else is not
	 * CEF: the standard parser refuses the message, so the fast path must
	 * refuse it too rather than invent a value.
	 */
	compare("newline-esc/msg",  esc_nl,  "msg");
	compare("return-esc/msg",   esc_cr,  "msg");
	compare("solidus-esc/msg",  esc_sl,  "msg");
	compare("bad-esc/msg",      esc_bad, "msg");
	compare("bad-esc/dst",      esc_bad, "dst");
	compare("bad-esc-hdr",      esc_bad_hdr, "DeviceVendor");

	/* "\\\\=" at the end of a value is a backslash then a pair delimiter. */
	compare("trail-eq/data",      trail_eq,      "data");
	compare("trail-eq-next/data", trail_eq_next, "data");
	compare("trail-eq-next/next", trail_eq_next, "next");

	compare("one-sp/msg",   one_sp,   "msg");
	compare("one-sp/src",   one_sp,   "src");
	compare("two-sp/msg",   two_sp,   "msg");
	compare("two-sp/src",   two_sp,   "src");
	compare("three-sp/msg", three_sp, "msg");

	/* Names are alnum, '_' or '.'. A hyphen refuses both engines. */
	{
		static const char *const hyphen_key =
			"CEF:0|Vend|Prod|1.0|100|Test|5|foo=a bar-baz=1";
		compare("hyphen-key/foo", hyphen_key, "foo");
	}

	/*
	 * Duplicate extension names: the walker uses replacing object_add, so
	 * the last pair wins. HTML in a value is scanned as further pairs, and
	 * a second href= must overwrite the first. Rule-field first-wins does
	 * not apply here.
	 */
	{
		static const char *const dup_src =
			"CEF:0|Vend|Prod|1.0|100|Test|5|"
			"src=192.0.2.1 msg=hello src=192.0.2.9";
		compare("dup-src/src", dup_src, "src");
		compare("dup-src/msg", dup_src, "msg");
	}
	{
		static const char *const html_href =
			"CEF:0|Vend|Prod|1.0|100|Test|5|"
			"cs2=pre href=\"http://a.example.test/x\" "
			"href=\"http://b.example.test/path/file.html\"";
		compare("html-href/href", html_href, "href");
		compare("html-href/cs2", html_href, "cs2");
	}

	/*
	 * A rewritten CEF value larger than the 16k starting arena: the arena
	 * grows once after reset, unescape runs, both engines agree.
	 */
	{
		enum { NESC = 9000 };
		char *big;
		size_t hdr;
		int i;

		static const char *const pre =
			"CEF:0|Vend|Prod|1.0|100|Test|5|msg=";
		hdr = strlen(pre);
		big = malloc(hdr + (size_t)NESC * 2 + 1);
		if (big == NULL) {
			printf("FAIL long-unescape: malloc\n");
			++failures;
		} else {
			memcpy(big, pre, hdr);
			for (i = 0; i < NESC; i++) {
				big[hdr + (size_t)i * 2] = '\\';
				big[hdr + (size_t)i * 2 + 1] = 'n';
			}
			big[hdr + (size_t)NESC * 2] = '\0';
			{
				char *wdoc = malloc(65536), *tdoc = malloc(65536);
				char *wval = malloc(16384), *tval = malloc(16384);

				if (wdoc && tdoc && wval && tval) {
					compare_buf("long-unescape/msg", big, "msg",
						wdoc, 65536, tdoc, 65536,
						wval, 16384, tval, 16384);
				} else {
					printf("FAIL long-unescape: malloc docs\n");
					++failures;
				}
				free(wdoc);
				free(tdoc);
				free(wval);
				free(tval);
			}
			free(big);
		}
	}

	printf("%d comparisons, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
