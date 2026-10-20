/*
 * rewrite= renames keys of an inlined %.:json% object.
 *
 * Each engine has its own document shape and the test locks that shape.
 * The walker stores a dotted destination as one key ("source.ip"), the same
 * way it stores %source.ip:word%. Turbo nests it ({"source":{"ip":...}}).
 * The values must be equal. A check that accepts either shape is not a
 * parity check: it stays green if an engine switches shape.
 *
 * This file is part of the liblognorm project, released under ASL 2.0.
 */
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "liblognorm.h"
#include "lognorm.h"
#include "rewrite.h"

static int failures;

/* Non-turbo CI builds still run this test. ln_normalize_to_str is the
 * walker there, so the nested-key checks must not run. */
static int
turbo_built(void)
{
#ifdef ENABLE_TURBO
	return 1;
#else
	return 0;
#endif
}

static struct json_object *
key_of(struct json_object *o, const char *key)
{
	struct json_object *v = NULL;

	if (o == NULL || !json_object_object_get_ex(o, key, &v))
		return NULL;
	return v;
}

static const char *
dump(struct json_object *o)
{
	if (o == NULL)
		return "null";
	return json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN);
}

/*
 * The returned object is only valid until ln_exitCtx(ctx). libfastjson keeps
 * the field-name pointer the parser passed, and that name is owned by the
 * context.
 */
static struct json_object *
normalize(ln_ctx *ctx_out, int turbo, const char *rb, const char *msg)
{
	ln_ctx ctx;
	struct json_object *json = NULL;

	*ctx_out = NULL;
	ctx = ln_initCtx();
	if (ctx == NULL)
		return NULL;
	if (turbo)
		ln_setCtxOpts(ctx, LN_CTXOPT_TURBO | LN_CTXOPT_TURBO_STRICT);
	if (ln_loadSamplesFromString(ctx, rb) != 0) {
		ln_exitCtx(ctx);
		return NULL;
	}
	if (turbo) {
		char *rendered = NULL;
		size_t len = 0;

		if (ln_normalize_to_str(ctx, msg, strlen(msg), &rendered, &len) == 0
		    && rendered != NULL) {
			json = json_tokener_parse(rendered);
			free(rendered);
		}
	} else if (ln_normalize(ctx, msg, strlen(msg), &json) != 0) {
		/* A rejected line still carries unparsed-data. Drop the
		 * object only when that marker is absent. */
		if (json != NULL && !json_object_object_get_ex(json, "unparsed-data", NULL)) {
			json_object_put(json);
			json = NULL;
		}
	}
	*ctx_out = ctx;
	return json;
}

/* Number of top-level "<key>": members in turbo's rendered document. The
 * re-parsed document cannot show a duplicate: the tokener keeps one. */
static int
turbo_key_count(const char *rb, const char *msg, const char *key)
{
	ln_ctx ctx = ln_initCtx();
	char *rendered = NULL, pat[128];
	size_t len = 0;
	int n = -1;
	const char *p;

	if (ctx == NULL)
		return -1;
	ln_setCtxOpts(ctx, LN_CTXOPT_TURBO | LN_CTXOPT_TURBO_STRICT);
	snprintf(pat, sizeof(pat), "\"%s\":", key);
	if (ln_loadSamplesFromString(ctx, rb) == 0
	    && ln_normalize_to_str(ctx, msg, strlen(msg), &rendered, &len) == 0
	    && rendered != NULL) {
		n = 0;
		for (p = strstr(rendered, pat); p != NULL; p = strstr(p + 1, pat))
			n++;
	}
	free(rendered);
	ln_exitCtx(ctx);
	return n;
}

static void
release(ln_ctx ctx, struct json_object *doc)
{
	if (doc != NULL)
		json_object_put(doc);
	if (ctx != NULL)
		ln_exitCtx(ctx);
}

static int
str_eq(struct json_object *o, const char *value)
{
	return o != NULL && json_object_is_type(o, json_type_string)
		&& strcmp(json_object_get_string(o), value) == 0;
}

static int
int_eq(struct json_object *o, int64_t value)
{
	return o != NULL && json_object_is_type(o, json_type_int)
		&& json_object_get_int64(o) == value;
}

/* Walker: the dotted name is one key. A nested object under the first
 * segment is the turbo shape and must not be accepted here. */
static struct json_object *
walker_leaf(const char *what, struct json_object *doc, const char *dotted)
{
	struct json_object *leaf = key_of(doc, dotted);
	const char *dot = strchr(dotted, '.');
	char head[128];

	if (leaf == NULL) {
		printf("FAIL %s/walker: flat key '%s' absent (%s)\n",
			what, dotted, dump(doc));
		failures++;
		return NULL;
	}
	if (dot != NULL && (size_t)(dot - dotted) < sizeof(head)) {
		struct json_object *nested;

		memcpy(head, dotted, (size_t)(dot - dotted));
		head[dot - dotted] = '\0';
		nested = key_of(doc, head);
		if (nested != NULL && json_object_is_type(nested, json_type_object)) {
			printf("FAIL %s/walker: '%s' is nested, want flat '%s' (%s)\n",
				what, head, dotted, dump(doc));
			failures++;
			return NULL;
		}
	}
	return leaf;
}

/* Turbo: the dotted name is nested. A flat key of the whole dotted
 * string is the walker shape and must not be accepted here. */
static struct json_object *
turbo_leaf(const char *what, struct json_object *doc, const char *dotted)
{
	struct json_object *cur = doc;
	const char *p = dotted;

	/* strict turbo refusing the line must not pass as an absent leaf */
	if (doc == NULL) {
		printf("FAIL %s/turbo: no document for '%s'\n", what, dotted);
		failures++;
		return NULL;
	}
	if (key_of(doc, dotted) != NULL && strchr(dotted, '.') != NULL) {
		printf("FAIL %s/turbo: flat key '%s' present (%s)\n",
			what, dotted, dump(doc));
		failures++;
		return NULL;
	}
	while (cur != NULL && *p != '\0') {
		char key[128];
		const char *dot = strchr(p, '.');
		size_t n = dot != NULL ? (size_t)(dot - p) : strlen(p);
		struct json_object *next;

		if (n == 0 || n >= sizeof(key)) {
			printf("FAIL %s/turbo: bad path '%s'\n", what, dotted);
			failures++;
			return NULL;
		}
		memcpy(key, p, n);
		key[n] = '\0';
		next = key_of(cur, key);
		if (next == NULL) {
			printf("FAIL %s/turbo: missing '%s' in '%s' (%s)\n",
				what, key, dotted, dump(doc));
			failures++;
			return NULL;
		}
		cur = next;
		p = dot != NULL ? dot + 1 : p + n;
	}
	return cur;
}

static void
same_leaf(const char *what, struct json_object *w, struct json_object *t)
{
	if (w == NULL || t == NULL)
		return;
	if (json_object_get_type(w) != json_object_get_type(t)
	    || strcmp(dump(w), dump(t)) != 0) {
		printf("FAIL %s: walker value %s, turbo value %s\n",
			what, dump(w), dump(t));
		failures++;
	}
}

static void
absent(const char *what, int turbo, struct json_object *doc, const char *key)
{
	/* a key cannot be shown absent from a line that was not parsed */
	if (doc == NULL) {
		printf("FAIL %s/%s: no document\n", what, turbo ? "turbo" : "walker");
		failures++;
		return;
	}
	if (key_of(doc, key) != NULL) {
		printf("FAIL %s/%s: '%s' present (%s)\n",
			what, turbo ? "turbo" : "walker", key, dump(doc));
		failures++;
	}
}

static void
pair(const char *what, const char *rb, const char *msg, const char *dotted,
    int kind, const char *sval, int64_t ival)
{
	ln_ctx wc = NULL, tc = NULL;
	struct json_object *wd, *td, *wl, *tl;

	td = NULL;
	tl = NULL;
	wd = normalize(&wc, 0, rb, msg);
	wl = walker_leaf(what, wd, dotted);
	if (turbo_built()) {
		td = normalize(&tc, 1, rb, msg);
		tl = turbo_leaf(what, td, dotted);
	}
	if (kind == 0) {
		if (wl != NULL && !str_eq(wl, sval)) {
			printf("FAIL %s/walker: '%s' is %s, want '%s'\n",
				what, dotted, dump(wl), sval);
			failures++;
		}
		if (tl != NULL && !str_eq(tl, sval)) {
			printf("FAIL %s/turbo: '%s' is %s, want '%s'\n",
				what, dotted, dump(tl), sval);
			failures++;
		}
	} else {
		if (wl != NULL && !int_eq(wl, ival)) {
			printf("FAIL %s/walker: '%s' is %s, want %lld\n",
				what, dotted, dump(wl), (long long)ival);
			failures++;
		}
		if (tl != NULL && !int_eq(tl, ival)) {
			printf("FAIL %s/turbo: '%s' is %s, want %lld\n",
				what, dotted, dump(tl), (long long)ival);
			failures++;
		}
	}
	same_leaf(what, wl, tl);
	release(wc, wd);
	release(tc, td);
}

static void
absent_both(const char *what, const char *rb, const char *msg, const char *key)
{
	ln_ctx wc = NULL, tc = NULL;
	struct json_object *wd, *td;

	td = NULL;
	wd = normalize(&wc, 0, rb, msg);
	absent(what, 0, wd, key);
	if (turbo_built()) {
		td = normalize(&tc, 1, rb, msg);
		absent(what, 1, td, key);
	}
	release(wc, wd);
	release(tc, td);
}

static void
expect_unparsed(const char *what, const char *rb, const char *msg)
{
	ln_ctx wc = NULL, tc = NULL;
	struct json_object *wd, *td;

	td = NULL;
	wd = normalize(&wc, 0, rb, msg);
	if (turbo_built())
		td = normalize(&tc, 1, rb, msg);
	if (key_of(wd, "unparsed-data") == NULL) {
		printf("FAIL %s/walker: parsed (%s)\n", what, dump(wd));
		failures++;
	}
	/* Strict turbo reports no match as a NULL document. A document
	 * without unparsed-data would mean the line was accepted. */
	if (td != NULL && key_of(td, "unparsed-data") == NULL) {
		printf("FAIL %s/turbo: parsed (%s)\n", what, dump(td));
		failures++;
	}
	release(wc, wd);
	release(tc, td);
}

static void
expect_load_fail(const char *what, const char *rb)
{
	ln_ctx ctx = ln_initCtx();

	if (ctx == NULL) {
		printf("FAIL %s: no context\n", what);
		failures++;
		return;
	}
	if (ln_loadSamplesFromString(ctx, rb) == 0) {
		printf("FAIL %s: rulebase was accepted\n", what);
		failures++;
	}
	ln_exitCtx(ctx);
}

/* failOnDuplicate is 0 for a normal rule, so two sources that share a
 * destination overwrite. The reject branch is the merge itself: the
 * caller passes fail_dup and the existing key is left unchanged. */
static void
test_fail_dup(void)
{
	struct ln_rw_ent ent;
	struct ln_rw_tab tab;
	uint32_t slot[4];
	struct json_object *dst, *src, *host;
	int i;

	memset(&ent, 0, sizeof(ent));
	ent.src = "src_ip";
	ent.slen = 6;
	ent.dst = "host";
	ent.dlen = 4;
	for (i = 0; i < 4; i++)
		slot[i] = 1;
	memset(&tab, 0, sizeof(tab));
	tab.ents = &ent;
	tab.n = 1;
	tab.mask = 3;
	tab.slot = slot;

	src = json_tokener_parse("{\"src_ip\":\"9.9.9.9\"}");
	dst = json_object_new_object();
	json_object_object_add(dst, "host", json_object_new_string("gw"));
	if (src == NULL || dst == NULL || ln_rewrite_merge(dst, src, &tab, 1) == 0) {
		printf("FAIL fail-dup: merge accepted a colliding key (%s)\n", dump(dst));
		failures++;
	}
	host = key_of(dst, "host");
	if (!str_eq(host, "gw")) {
		printf("FAIL fail-dup: host became %s\n", dump(host));
		failures++;
	}
	if (dst != NULL)
		json_object_put(dst);
	if (src != NULL)
		json_object_put(src);
}

int
main(void)
{
	static const char *plain =
		"version=2\n"
		"rule=:%.:json%\n";
	static const char *mapped =
		"version=2\n"
		"rewrite=alert:src_ip=source.ip\n"
		"rewrite=alert:dest_port=destination.port\n"
		"rewrite=alert:proto=network.transport|lower\n"
		"rewrite=alert:alert.signature_id=rule.id\n"
		"annotate=alert:+event.action=\"ids_alert\"\n"
		"rule=alert:%.:json%\n";
	static const char *kept_field =
		"version=2\n"
		"rewrite=alert:src_ip=source.ip\n"
		"rule=alert:%host:word% %.:json%\n";
	static const char *same_dest =
		"version=2\n"
		"rewrite=alert:src_ip=source.ip\n"
		"rewrite=alert:client_ip=source.ip\n"
		"rule=alert:%.:json%\n";
	static const char *clobber =
		"version=2\n"
		"rewrite=alert:src_ip=host\n"
		"rule=alert:%host:word% %.:json%\n";
	/* the same rule tail with a fallback rule: a broken object must leave
	 * host as the word parser stored it, on both engines */
	static const char *clobber_fallback =
		"version=2\n"
		"rewrite=alert:src_ip=host\n"
		"rule=alert:%host:word% %.:json%\n"
		"rule=other:%host:word% %r:rest%\n";
	static const char *dashed_tag =
		"version=2\n"
		"rewrite=cisco-asa:src_ip=source.ip\n"
		"rule=cisco-asa:%.:json%\n";
	static const char *hash_tag =
		"version=2\n"
		"rewrite=#x:src_ip=source.ip\n"
		"rule=#x:%.:json%\n";
	static const char *comment_only =
		"version=2\n"
		"rewrite= # nothing here\n"
		"rule=alert:%.:json%\n";
	static const char *two_tags_same =
		"version=2\n"
		"rewrite=a:src_ip=source.ip\n"
		"rewrite=b:src_ip=source.ip\n"
		"rule=a,b:%.:json%\n";
	static const char *two_tags_conflict =
		"version=2\n"
		"rewrite=a:src_ip=source.ip\n"
		"rewrite=b:src_ip=client.ip\n"
		"rule=a,b:%.:json%\n";
	static const char *clobber_twice =
		"version=2\n"
		"rewrite=alert:src_ip=host\n"
		"rewrite=alert:client_ip=host\n"
		"rule=alert:%host:word% %.:json%\n";
	static const char *nv_clobber_twice =
		"version=2\n"
		"rewrite=kv:src=host\n"
		"rewrite=kv:client=host\n"
		"rule=kv:%host:word% %.:name-value-list%\n";
	static const char *nv_clobber =
		"version=2\n"
		"rewrite=kv:src=host\n"
		"rule=kv:%host:word% %.:name-value-list%\n"
		"rule=other:%host:word% %r:rest%\n";
	static const char *bad_mod =
		"version=2\n"
		"rewrite=alert:src_ip=source.ip|upper\n"
		"rule=alert:%.:json%\n";
	static const char *twice =
		"version=2\n"
		"rewrite=alert:src_ip=source.ip\n"
		"rewrite=alert:src_ip=destination.ip\n"
		"rule=alert:%.:json%\n";
	static const char *prefix =
		"version=2\n"
		"rewrite=alert:src_ip=source.ip\n"
		"rule=alert:%.:json%\n"
		"rule=longer:%.:json% tail\n";
	static const char *nomatch =
		"version=2\n"
		"rewrite=alert:src_ip=source.ip\n"
		"rule=alert:%word:word%\n";
	static const char *custom_type =
		"version=2\n"
		"type=@hdr:%host:word% \n"
		"rewrite=alert:src_ip=source.ip\n"
		"rule=alert:%.:@hdr%%.:json%\n";
	static const char *array_rb =
		"version=2\n"
		"rewrite=alert:src_ip=source.ip\n"
		"annotate=alert:+event.action=\"ids_alert\"\n"
		"rule=alert:%.:json%\n";
	const char *msg =
		"{\"src_ip\":\"9.9.9.9\",\"dest_port\":22,\"proto\":\"TCP\","
		"\"alert\":{\"signature_id\":1001},\"junk\":\"no\"}";
	char *long_msg;
	size_t i;

	/* No rewrite=: the key stays the JSON key, flat on both engines. */
	pair("plain-keeps", plain, "{\"src_ip\":\"1.1.1.1\"}", "src_ip",
		0, "1.1.1.1", 0);

	pair("renamed", mapped, msg, "source.ip", 0, "9.9.9.9", 0);
	/* an escaped key is the same key: renamed by both engines, not dropped
	 * by one of them */
	pair("escaped-key", mapped, "{\"src\\u005fip\":\"1.2.3.4\"}", "source.ip",
		0, "1.2.3.4", 0);
	pair("escaped-key-plain", plain, "{\"src\\u005fip\":\"1.2.3.4\"}", "src_ip",
		0, "1.2.3.4", 0);
	/* the table belongs to %.:json%; a custom type inlined before it keeps
	 * its own fields */
	pair("custom-type-kept", custom_type, "gw {\"src_ip\":\"9.9.9.9\"}", "host",
		0, "gw", 0);
	pair("custom-type-json", custom_type, "gw {\"src_ip\":\"9.9.9.9\"}", "source.ip",
		0, "9.9.9.9", 0);
	pair("lower", mapped, msg, "network.transport", 0, "tcp", 0);
	pair("port", mapped, msg, "destination.port", 1, NULL, 22);
	pair("nested-src", mapped, msg, "rule.id", 1, NULL, 1001);
	pair("annot", mapped, msg, "event.action", 0, "ids_alert", 0);
	absent_both("dropped", mapped, msg, "src_ip");
	absent_both("junk", mapped, msg, "junk");
	absent_both("raw-alert", mapped, msg, "alert");

	pair("named-field", kept_field,
		"gw {\"src_ip\":\"8.8.8.8\",\"junk\":1}", "host", 0, "gw", 0);
	pair("named-renamed", kept_field,
		"gw {\"src_ip\":\"8.8.8.8\",\"junk\":1}", "source.ip", 0, "8.8.8.8", 0);
	absent_both("named-junk", kept_field,
		"gw {\"src_ip\":\"8.8.8.8\",\"junk\":1}", "junk");

	/* Two sources, one destination: last key wins, both engines. */
	pair("last-wins", same_dest,
		"{\"src_ip\":\"1.1.1.1\",\"client_ip\":\"2.2.2.2\"}",
		"source.ip", 0, "2.2.2.2", 0);

	/* A parsed field and a rewritten key share a name. Top-level
	 * normalize passes failOnDuplicate=0, so the rewritten value
	 * replaces the parsed one. Both engines must show that value. */
	pair("clobber", clobber, "gw {\"src_ip\":\"9.9.9.9\"}",
		"host", 0, "9.9.9.9", 0);
	/* name-value-list: the mapped pair replaces host on a match, and leaves
	 * it alone when the pairs fail after it was stored */
	pair("nv-clobber", nv_clobber, "gw src=9.9.9.9", "host", 0, "9.9.9.9", 0);
	pair("nv-clobber-fallback", nv_clobber, "gw src=9.9.9.9 x", "host", 0, "gw", 0);
	pair("nv-clobber-twice", nv_clobber_twice, "gw src=1.1.1.1 client=2.2.2.2", "host",
		0, "2.2.2.2", 0);
	if (turbo_built()
	    && turbo_key_count(nv_clobber_twice, "gw src=1.1.1.1 client=2.2.2.2", "host") != 1) {
		printf("FAIL nv-clobber-twice/turbo: \"host\" not unique\n");
		failures++;
	}
	pair("clobber-fallback", clobber_fallback, "gw {\"src_ip\":\"9.9.9.9\", broken",
		"host", 0, "gw", 0);
	/* two keys onto one earlier field: last write wins, and the earlier
	 * field is gone, not kept as a second "host" */
	pair("clobber-twice", clobber_twice, "gw {\"src_ip\":\"1.1.1.1\",\"client_ip\":\"2.2.2.2\"}",
		"host", 0, "2.2.2.2", 0);
	if (turbo_built()) {
		int n = turbo_key_count(clobber_twice,
			"gw {\"src_ip\":\"1.1.1.1\",\"client_ip\":\"2.2.2.2\"}", "host");
		if (n != 1) {
			printf("FAIL clobber-twice/turbo: %d \"host\" members, want 1\n", n);
			failures++;
		}
	}
	/* the object parses, the rule then fails on the trailing word */
	pair("clobber-trailing", clobber_fallback, "gw {\"src_ip\":\"9.9.9.9\"} x",
		"host", 0, "gw", 0);

	/* Top-level array: nothing to rename. The array is dropped and
	 * the annotation remains. The element must not leak as "0" or ".". */
	pair("array-annot", array_rb, "[\"9.9.9.9\"]",
		"event.action", 0, "ids_alert", 0);
	absent_both("array-element", array_rb, "[\"9.9.9.9\"]", "0");
	absent_both("array-dot", array_rb, "[\"9.9.9.9\"]", ".");

	/* A 512-byte key does not fit the walker path buffer or the turbo
	 * key buffer. Both engines reject the line. */
	long_msg = malloc(512 + 16);
	if (long_msg == NULL)
		return 1;
	long_msg[0] = '{';
	long_msg[1] = '"';
	for (i = 0; i < 512; i++)
		long_msg[2 + i] = 'a';
	memcpy(long_msg + 2 + 512, "\":\"x\"}", 6);
	long_msg[2 + 512 + 6] = '\0';
	expect_unparsed("long-key", mapped, long_msg);
	free(long_msg);

	/* A mapped key before the over-long one: the walker rejects the line
	 * and must not leave the renamed key in the event it reports. */
	{
		static const char head[] = "{\"src_ip\":\"1.2.3.4\",\"";
		const size_t hl = sizeof(head) - 1;
		ln_ctx wc = NULL;
		struct json_object *wd;

		long_msg = malloc(hl + 600 + 8);
		if (long_msg == NULL)
			return 1;
		memcpy(long_msg, head, hl);
		memset(long_msg + hl, 'a', 600);
		memcpy(long_msg + hl + 600, "\":1}", 5);
		long_msg[hl + 600 + 4] = '\0';
		wd = normalize(&wc, 0, mapped, long_msg);
		if (key_of(wd, "unparsed-data") == NULL) {
			printf("FAIL long-key-partial/walker: parsed (%s)\n", dump(wd));
			failures++;
		}
		absent("long-key-partial", 0, wd, "source.ip");
		release(wc, wd);
		free(long_msg);
	}

	pair("dashed-tag", dashed_tag, "{\"src_ip\":\"1.1.1.1\"}", "source.ip",
		0, "1.1.1.1", 0);
	pair("two-tags-same", two_tags_same, "{\"src_ip\":\"1.1.1.1\"}", "source.ip",
		0, "1.1.1.1", 0);
	expect_load_fail("two-tags-conflict", two_tags_conflict);
	pair("hash-tag", hash_tag, "{\"src_ip\":\"1.1.1.1\"}", "source.ip",
		0, "1.1.1.1", 0);
	expect_load_fail("comment-only", comment_only);

	test_fail_dup();

	/* Length-bounded input with no trailing NUL. The unparsed-data
	 * path must not strlen() past the buffer. */
	{
		ln_ctx ctx = ln_initCtx();
		struct json_object *json = NULL;
		char buf[1] = { 'A' };

		if (ctx == NULL || ln_loadSamplesFromString(ctx, plain) != 0) {
			printf("FAIL unterminated: load\n");
			failures++;
		} else {
			ln_normalize(ctx, buf, 1, &json);
			if (json == NULL || key_of(json, "unparsed-data") == NULL) {
				printf("FAIL unterminated: %s\n", dump(json));
				failures++;
			}
		}
		release(ctx, json);
	}

	{
		static const char cef_plain[] = "rule=:%.:cef%\n";
		static const char cef_mapped[] =
			"version=2\n"
			"rewrite=cef:src=source.ip\n"
			"rewrite=cef:dst=destination.ip\n"
			"rewrite=cef:spt=source.port\n"
			"rewrite=cef:proto=network.transport|lower\n"
			"rule=cef:%.:cef%\n";
		static const char cef_last[] =
			"version=2\n"
			"rewrite=cef:src=source.ip\n"
			"rewrite=cef:client=source.ip\n"
			"rule=cef:%.:cef%\n";
		const char *cef_msg =
			"CEF:0|Vend|Prod|1.0|100|Test|5|src=203.0.113.7 dst=10.1.2.3 spt=51515 proto=TCP app=http";
		ln_ctx wc = NULL, tc = NULL;
		struct json_object *wd, *td, *we, *te, *wl, *tl;

		td = NULL;
		wd = normalize(&wc, 0, cef_plain, cef_msg);
		if (turbo_built())
			td = normalize(&tc, 1, cef_plain, cef_msg);
		we = key_of(wd, "Extensions");
		te = key_of(td, "Extensions");
		if (!str_eq(key_of(wd, "DeviceVendor"), "Vend")
		    || (turbo_built() && !str_eq(key_of(td, "DeviceVendor"), "Vend"))) {
			printf("FAIL cef-plain header (%s) (%s)\n", dump(wd), dump(td));
			failures++;
		}
		wl = walker_leaf("cef-plain", we, "src");
		tl = NULL;
		if (turbo_built())
			tl = turbo_leaf("cef-plain", te, "src");
		if (wl != NULL && !str_eq(wl, "203.0.113.7")) {
			printf("FAIL cef-plain/walker src %s\n", dump(wl));
			failures++;
		}
		if (tl != NULL && !str_eq(tl, "203.0.113.7")) {
			printf("FAIL cef-plain/turbo src %s\n", dump(tl));
			failures++;
		}
		same_leaf("cef-plain", wl, tl);
		release(wc, wd);
		release(tc, td);

		td = NULL;
		wd = normalize(&wc, 0, cef_mapped, cef_msg);
		if (turbo_built())
			td = normalize(&tc, 1, cef_mapped, cef_msg);
		if (!str_eq(key_of(wd, "DeviceVendor"), "Vend")
		    || (turbo_built() && !str_eq(key_of(td, "DeviceVendor"), "Vend"))) {
			printf("FAIL cef-hdr (%s) (%s)\n", dump(wd), dump(td));
			failures++;
		}
		we = key_of(wd, "Extensions");
		te = key_of(td, "Extensions");
		wl = walker_leaf("cef-src", we, "source.ip");
		tl = NULL;
		if (turbo_built())
			tl = turbo_leaf("cef-src", te, "source.ip");
		if (wl != NULL && !str_eq(wl, "203.0.113.7")) {
			printf("FAIL cef-src/walker %s\n", dump(wl));
			failures++;
		}
		if (tl != NULL && !str_eq(tl, "203.0.113.7")) {
			printf("FAIL cef-src/turbo %s\n", dump(tl));
			failures++;
		}
		same_leaf("cef-src", wl, tl);
		wl = walker_leaf("cef-dst", we, "destination.ip");
		tl = NULL;
		if (turbo_built())
			tl = turbo_leaf("cef-dst", te, "destination.ip");
		same_leaf("cef-dst", wl, tl);
		if (wl != NULL && !str_eq(wl, "10.1.2.3")) {
			printf("FAIL cef-dst/walker %s\n", dump(wl));
			failures++;
		}
		wl = walker_leaf("cef-port", we, "source.port");
		tl = NULL;
		if (turbo_built())
			tl = turbo_leaf("cef-port", te, "source.port");
		same_leaf("cef-port", wl, tl);
		if (wl != NULL && !str_eq(wl, "51515")) {
			printf("FAIL cef-port %s\n", dump(wl));
			failures++;
		}
		wl = walker_leaf("cef-proto", we, "network.transport");
		tl = NULL;
		if (turbo_built())
			tl = turbo_leaf("cef-proto", te, "network.transport");
		same_leaf("cef-proto", wl, tl);
		if (wl != NULL && !str_eq(wl, "tcp")) {
			printf("FAIL cef-proto/walker %s\n", dump(wl));
			failures++;
		}
		if (tl != NULL && !str_eq(tl, "tcp")) {
			printf("FAIL cef-proto/turbo %s\n", dump(tl));
			failures++;
		}
		absent("cef-drop", 0, we, "src");
		if (turbo_built())
			absent("cef-drop", 1, te, "src");
		absent("cef-drop", 0, we, "app");
		if (turbo_built())
			absent("cef-drop", 1, te, "app");
		release(wc, wd);
		release(tc, td);

		td = NULL;
		wd = normalize(&wc, 0, cef_last,
			"CEF:0|Vend|Prod|1.0|100|Test|5|src=1.1.1.1 client=2.2.2.2");
		if (turbo_built())
			td = normalize(&tc, 1, cef_last,
				"CEF:0|Vend|Prod|1.0|100|Test|5|src=1.1.1.1 client=2.2.2.2");
		wl = walker_leaf("cef-last", key_of(wd, "Extensions"), "source.ip");
		tl = NULL;
		if (turbo_built())
			tl = turbo_leaf("cef-last", key_of(td, "Extensions"), "source.ip");
		same_leaf("cef-last", wl, tl);
		if (wl != NULL && !str_eq(wl, "2.2.2.2")) {
			printf("FAIL cef-last/walker %s\n", dump(wl));
			failures++;
		}
		if (tl != NULL && !str_eq(tl, "2.2.2.2")) {
			printf("FAIL cef-last/turbo %s\n", dump(tl));
			failures++;
		}
		release(wc, wd);
		release(tc, td);
	}

	{
		static const char nv_plain[] = "rule=:%.:name-value-list%\n";
		static const char nv_mapped[] =
			"version=2\n"
			"rewrite=kv:src=source.ip\n"
			"rewrite=kv:src-ip=source.ip\n"
			"rewrite=kv:proto=network.transport|lower\n"
			"rule=kv:%.:name-value-list%\n";
		static const char nv_last[] =
			"version=2\n"
			"rewrite=kv:src=source.ip\n"
			"rewrite=kv:client=source.ip\n"
			"rule=kv:%.:name-value-list%\n";
		const char *nv_msg = "src=203.0.113.7 proto=TCP app=http";
		ln_ctx wc = NULL, tc = NULL;
		struct json_object *wd, *td, *wl, *tl;

		td = NULL;
		wd = normalize(&wc, 0, nv_plain, nv_msg);
		if (turbo_built())
			td = normalize(&tc, 1, nv_plain, nv_msg);
		wl = walker_leaf("nv-plain", wd, "src");
		tl = NULL;
		if (turbo_built())
			tl = turbo_leaf("nv-plain", td, "src");
		same_leaf("nv-plain", wl, tl);
		if (wl != NULL && !str_eq(wl, "203.0.113.7")) {
			printf("FAIL nv-plain %s\n", dump(wl));
			failures++;
		}
		release(wc, wd);
		release(tc, td);

		td = NULL;
		wd = normalize(&wc, 0, nv_mapped, nv_msg);
		if (turbo_built())
			td = normalize(&tc, 1, nv_mapped, nv_msg);
		wl = walker_leaf("nv-src", wd, "source.ip");
		tl = NULL;
		if (turbo_built())
			tl = turbo_leaf("nv-src", td, "source.ip");
		same_leaf("nv-src", wl, tl);
		if (wl != NULL && !str_eq(wl, "203.0.113.7")) {
			printf("FAIL nv-src/walker %s\n", dump(wl));
			failures++;
		}
		wl = walker_leaf("nv-proto", wd, "network.transport");
		tl = NULL;
		if (turbo_built())
			tl = turbo_leaf("nv-proto", td, "network.transport");
		same_leaf("nv-proto", wl, tl);
		if (wl != NULL && !str_eq(wl, "tcp")) {
			printf("FAIL nv-proto/walker %s\n", dump(wl));
			failures++;
		}
		if (tl != NULL && !str_eq(tl, "tcp")) {
			printf("FAIL nv-proto/turbo %s\n", dump(tl));
			failures++;
		}
		absent("nv-drop", 0, wd, "src");
		if (turbo_built())
			absent("nv-drop", 1, td, "src");
		absent("nv-drop", 0, wd, "app");
		if (turbo_built())
			absent("nv-drop", 1, td, "app");
		release(wc, wd);
		release(tc, td);

		td = NULL;
		wd = normalize(&wc, 0, nv_mapped, "src-ip=198.51.100.9");
		if (turbo_built())
			td = normalize(&tc, 1, nv_mapped, "src-ip=198.51.100.9");
		wl = walker_leaf("nv-hyphen", wd, "source.ip");
		tl = NULL;
		if (turbo_built())
			tl = turbo_leaf("nv-hyphen", td, "source.ip");
		same_leaf("nv-hyphen", wl, tl);
		if (wl != NULL && !str_eq(wl, "198.51.100.9")) {
			printf("FAIL nv-hyphen %s\n", dump(wl));
			failures++;
		}
		release(wc, wd);
		release(tc, td);

		td = NULL;
		wd = normalize(&wc, 0, nv_last, "src=1.1.1.1 client=2.2.2.2");
		if (turbo_built())
			td = normalize(&tc, 1, nv_last, "src=1.1.1.1 client=2.2.2.2");
		wl = walker_leaf("nv-last", wd, "source.ip");
		tl = NULL;
		if (turbo_built())
			tl = turbo_leaf("nv-last", td, "source.ip");
		same_leaf("nv-last", wl, tl);
		if (wl != NULL && !str_eq(wl, "2.2.2.2")) {
			printf("FAIL nv-last/walker %s\n", dump(wl));
			failures++;
		}
		if (tl != NULL && !str_eq(tl, "2.2.2.2")) {
			printf("FAIL nv-last/turbo %s\n", dump(tl));
			failures++;
		}
		release(wc, wd);
		release(tc, td);
	}

	expect_load_fail("bad-modifier", bad_mod);
	expect_load_fail("conflict", twice);
	expect_load_fail("shared-prefix", prefix);
	expect_load_fail("not-json", nomatch);

	if (failures != 0) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("rewrite_test: ok\n");
	return 0;
}
