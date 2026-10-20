/*
 * Deterministic smoke for rewrite=, plus an optional libFuzzer entry.
 *
 *   clang -fsanitize=fuzzer,address -DREWRITE_LIBFUZZER -I src \
 *         -o rewrite_fuzz tests/rewrite_fuzz.c src/.libs/liblognorm.a ...
 *   ./rewrite_fuzz -max_total_time=30
 *
 * make check runs main(), which feeds generated objects and raw bytes
 * through both engines and checks the output keys.
 *
 * This file is part of the liblognorm project, released under ASL 2.0.
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "liblognorm.h"
#include "lognorm.h"

static const char RB[] =
	"version=2\n"
	"rewrite=e:src_ip=source.ip\n"
	"rewrite=e:proto=network.transport|lower\n"
	"rewrite=e:alert.signature_id=rule.id\n"
	"annotate=e:+event.action=\"ids_alert\"\n"
	"rule=e:%.:json%\n";

static const char *const ALLOWED[] = {
	"source.ip",
	"network.transport",
	"rule.id",
	"event.action",
	"event.tags",
	"tags",
	"unparsed-data",
	"unparsed-data-binary",
	NULL
};

static int
allowed_path(const char *path)
{
	int i;

	for (i = 0; ALLOWED[i] != NULL; i++) {
		if (strcmp(path, ALLOWED[i]) == 0)
			return 1;
	}
	return 0;
}

/* Walk every leaf. Return -1 if a key is outside the rewrite destinations. */
static int
check_obj(struct json_object *o, char *path, size_t plen)
{
	struct json_object_iterator it, end;

	if (o == NULL || !json_object_is_type(o, json_type_object))
		return 0;
	it = json_object_iter_begin(o);
	end = json_object_iter_end(o);
	while (!json_object_iter_equal(&it, &end)) {
		const char *key = json_object_iter_peek_name(&it);
		struct json_object *val = json_object_iter_peek_value(&it);
		size_t klen = strlen(key);
		size_t saved = plen;

		if (plen + (plen ? 1 : 0) + klen >= 512)
			return -1;
		if (plen)
			path[plen++] = '.';
		memcpy(path + plen, key, klen);
		plen += klen;
		path[plen] = '\0';
		if (json_object_is_type(val, json_type_object)) {
			if (check_obj(val, path, plen) != 0)
				return -1;
		} else if (!allowed_path(path)) {
			/* A flat destination ("source.ip") is one key. */
			if (!allowed_path(key))
				return -1;
		}
		plen = saved;
		path[plen] = '\0';
		json_object_iter_next(&it);
	}
	return 0;
}

static int
exercise(ln_ctx walk, ln_ctx turbo, const char *msg, size_t len)
{
	struct json_object *doc = NULL;
	char path[512];
	char *rendered = NULL;
	size_t n = 0;
	int rc = 0;

	/* A rejected line still returns a document (unparsed-data). Free it
	 * on every path. Leaving it for the next input is a leak, and the
	 * fuzzer dies on it. */
	if (ln_normalize(walk, msg, len, &doc) == 0 && doc != NULL) {
		path[0] = '\0';
		if (check_obj(doc, path, 0) != 0)
			rc = -1;
	}
	if (doc != NULL)
		json_object_put(doc);
	if (turbo != NULL
	    && ln_normalize_to_str(turbo, msg, len, &rendered, &n) == 0
	    && rendered != NULL) {
		doc = json_tokener_parse(rendered);
		free(rendered);
		if (doc != NULL) {
			path[0] = '\0';
			if (check_obj(doc, path, 0) != 0)
				rc = -1;
			json_object_put(doc);
		}
	} else {
		free(rendered);
	}
	return rc;
}

#ifdef REWRITE_LIBFUZZER
int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static ln_ctx walk, turbo;
	static int ready;

	if (!ready) {
		walk = ln_initCtx();
		turbo = ln_initCtx();
		if (walk == NULL || turbo == NULL)
			return 0;
		ln_setCtxOpts(turbo, LN_CTXOPT_TURBO | LN_CTXOPT_TURBO_STRICT);
		if (ln_loadSamplesFromString(walk, RB) != 0
		    || ln_loadSamplesFromString(turbo, RB) != 0)
			abort();
		ready = 1;
	}
	if (size > 4096)
		size = 4096;
	exercise(walk, turbo, (const char *)data, size);
	return 0;
}
#else
static uint32_t
xorshift(uint32_t *s)
{
	uint32_t x = *s;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*s = x;
	return x;
}

static void
gen_json(char *out, size_t cap, uint32_t *st)
{
	static const char *keys[] = {
		"src_ip", "proto", "junk", "dest_port", "alert", "ok"
	};
	size_t used = 0;
	int n, i;

	n = (int)(xorshift(st) % 8);
	if (used + 1 >= cap)
		return;
	out[used++] = '{';
	for (i = 0; i < n && used + 32 < cap; i++) {
		const char *k = keys[xorshift(st) % 6];

		if (i)
			out[used++] = ',';
		used += (size_t)snprintf(out + used, cap - used,
			"\"%s\":", k);
		if (strcmp(k, "alert") == 0)
			used += (size_t)snprintf(out + used, cap - used,
				"{\"signature_id\":%u}", xorshift(st) % 1000);
		else if (strcmp(k, "proto") == 0)
			used += (size_t)snprintf(out + used, cap - used,
				"\"%s\"", (xorshift(st) & 1) ? "TCP" : "udp");
		else
			used += (size_t)snprintf(out + used, cap - used,
				"\"v%u\"", xorshift(st) % 50);
	}
	if (used + 1 < cap)
		out[used++] = '}';
	out[used] = '\0';
}

int
main(void)
{
	ln_ctx walk, turbo;
	uint32_t st = 0xC0FFEEu;
	int i, bad = 0;
	char buf[256];
	static const char *nasty[] = {
		"", "{", "}", "[]", "null", "{\"src_ip\":",
		"{\"src_ip\":\"a\",\"src_ip\":\"b\"}",
		"{\"a\":{\"b\":{\"c\":{\"d\":1}}}}",
		NULL
	};

	walk = ln_initCtx();
	turbo = ln_initCtx();
	if (walk == NULL || turbo == NULL)
		return 1;
	ln_setCtxOpts(turbo, LN_CTXOPT_TURBO | LN_CTXOPT_TURBO_STRICT);
	if (ln_loadSamplesFromString(walk, RB) != 0
	    || ln_loadSamplesFromString(turbo, RB) != 0) {
		fprintf(stderr, "rewrite_fuzz: rulebase rejected\n");
		return 1;
	}
	for (i = 0; nasty[i] != NULL; i++) {
		if (exercise(walk, turbo, nasty[i], strlen(nasty[i])) != 0)
			bad++;
	}
	for (i = 0; i < 500; i++) {
		uint32_t n = xorshift(&st) % 64;
		uint32_t j;

		for (j = 0; j < n; j++)
			buf[j] = (char)(xorshift(&st) & 0xff);
		if (exercise(walk, turbo, buf, n) != 0)
			bad++;
	}
	for (i = 0; i < 500; i++) {
		gen_json(buf, sizeof(buf), &st);
		if (exercise(walk, turbo, buf, strlen(buf)) != 0)
			bad++;
	}
	ln_exitCtx(walk);
	ln_exitCtx(turbo);
	if (bad != 0) {
		fprintf(stderr, "rewrite_fuzz: %d invariant failure(s)\n", bad);
		return 1;
	}
	printf("rewrite_fuzz: ok\n");
	return 0;
}
#endif
