/**
 * @file turbo_test_find.c
 * @brief ln_fast_result_find() against the JSON object built from a result.
 *
 * A consumer may answer a field lookup from ln_fast_result_find() instead of
 * the JSON object it builds from the same result. That is only correct if
 * every LN_FIND_ABSENT, LN_FIND_STRING and LN_FIND_INT answer matches what
 * the object holds when it is read the way rsyslog reads $!: one key per
 * dotted component, compared without case. The test builds the object the
 * way rsyslog mmnormalize does (dotted names nest, the last binding of a key
 * wins, a raw JSON value is parsed, tags go to a "tags" array) and checks
 * both fixed cases and random results made of colliding names.
 *
 * @copyright 2026 Advens. Released under ASL 2.0.
 */

#include "config.h"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

#ifdef ENABLE_TURBO

#include "turbo_result_fast.h"
#include "lognorm-turbo.h"

#include <json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <locale.h>

static int failures = 0;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL line %d: ", __LINE__); \
		fprintf(stderr, __VA_ARGS__); \
		fputc('\n', stderr); \
		failures++; \
	} \
} while (0)

/* The JSON object rsyslog mmnormalize builds from a result. */
static struct json_object *
build_json(const ln_fast_result_t *r)
{
	struct json_object *root = json_object_new_object();
	const int n = ln_fast_result_field_count(r);

	for (int i = 0; i < n; i++) {
		const char *fname, *sval = NULL;
		size_t flen, slen = 0;
		ln_ftype_t type;
		unsigned flags;
		int64_t ival = 0;
		double dval = 0;
		struct json_object *jval = NULL;
		int is_null = 0;
		char name[512];

		if (ln_fast_result_get_field_typed(r, i, &fname, &flen, &type, &flags,
				&sval, &slen, &ival, &dval) != 0 || flen >= sizeof(name))
			continue;
		switch (type) {
		case LN_FTYPE_STRING:
		case LN_FTYPE_STRING_INLINE:
			if (flags & LN_FFIELD_RAW_JSON) {
				struct json_tokener *tok = json_tokener_new();
				jval = json_tokener_parse_ex(tok, sval, (int)slen);
				json_tokener_free(tok);
			}
			if (jval == NULL) jval = json_object_new_string_len(sval, (int)slen);
			break;
		case LN_FTYPE_INT: jval = json_object_new_int64(ival); break;
		case LN_FTYPE_DOUBLE: jval = json_object_new_double(dval); break;
		case LN_FTYPE_BOOL: jval = json_object_new_boolean((json_bool)ival); break;
		case LN_FTYPE_NULL: is_null = 1; break;
		default: continue;
		}
		if (jval == NULL && !is_null) continue;
		memcpy(name, fname, flen);
		name[flen] = '\0';
		char *save = NULL;
		char *tok = memchr(fname, '.', flen) ? strtok_r(name, ".", &save) : NULL;
		if (tok != NULL) {
			struct json_object *parent = root;
			char *next = strtok_r(NULL, ".", &save);
			while (next != NULL) {
				struct json_object *child = NULL;
				if (!json_object_object_get_ex(parent, tok, &child)
					|| !json_object_is_type(child, json_type_object)) {
					child = json_object_new_object();
					json_object_object_add(parent, tok, child);
				}
				parent = child;
				tok = next;
				next = strtok_r(NULL, ".", &save);
			}
			json_object_object_add(parent, tok, jval);
		} else {
			json_object_object_add(root, name, jval);
		}
	}
	const int ntags = ln_fast_result_tag_count(r);
	if (ntags > 0) {
		struct json_object *tags = json_object_new_array();
		for (int i = 0; i < ntags; i++)
			json_object_array_add(tags, json_object_new_string(ln_fast_result_get_tag(r, i)));
		json_object_object_add(root, "tags", tags);
	}
	return root;
}

/* What the object holds at a dotted name, one key per component.
 * 0 absent, 1 present (value in *out, NULL for a JSON null). */
static int
json_at(struct json_object *root, const char *name, struct json_object **out)
{
	char buf[512];
	struct json_object *cur = root;
	snprintf(buf, sizeof(buf), "%s", name);
	char *save = NULL;
	for (char *tok = strtok_r(buf, ".", &save); tok != NULL; tok = strtok_r(NULL, ".", &save)) {
		if (!json_object_is_type(cur, json_type_object)) return 0;
		if (!json_object_object_get_ex(cur, tok, &cur)) return 0;
	}
	*out = cur;
	return 1;
}

/* Check one lookup against the object; returns the find() answer. */
static int
check(const ln_fast_result_t *r, struct json_object *root, const char *name)
{
	const char *v = NULL;
	size_t vl = 0;
	int64_t iv = 0;
	struct json_object *j = NULL;
	const int rc = ln_fast_result_find(r, name, strlen(name), &v, &vl, &iv);
	const int present = json_at(root, name, &j);

	switch (rc) {
	case LN_FIND_ABSENT:
		CHECK(!present, "'%s': find says absent, the object holds %s", name,
			  present ? json_object_to_json_string(j) : "-");
		break;
	case LN_FIND_STRING:
		CHECK(present && json_object_is_type(j, json_type_string)
			  && (size_t)json_object_get_string_len(j) == vl
			  && memcmp(json_object_get_string(j), v, vl) == 0,
			  "'%s': find says string '%.*s', the object holds %s", name, (int)vl, v,
			  present ? json_object_to_json_string(j) : "nothing");
		break;
	case LN_FIND_INT:
		CHECK(present && json_object_is_type(j, json_type_int) && json_object_get_int64(j) == iv,
			  "'%s': find says int %" PRId64 ", the object holds %s", name, iv,
			  present ? json_object_to_json_string(j) : "nothing");
		break;
	case LN_FIND_OTHER:
		break;
	default:
		CHECK(0, "'%s': find returned %d", name, rc);
	}
	return rc;
}

static void
expect(const ln_fast_result_t *r, struct json_object *root, const char *name, int want)
{
	const int got = check(r, root, name);
	CHECK(got == want, "'%s': find returned %d, expected %d", name, got, want);
}

static void
fixed_cases(void)
{
	static ln_fast_result_t r;
	static const char longval[] = "a value longer than the inline slot of a fast result field";

	ln_fast_result_init(&r, NULL);
	ln_fast_add_string_static(&r, "source.ip", 9, "192.0.2.1", 9);
	ln_fast_add_string_static(&r, "source.port", 11, "x", 1);
	ln_fast_add_int_static(&r, "source.port", 11, 443);          /* last binding wins */
	ln_fast_add_string_static(&r, "url.original", 12, longval, sizeof(longval) - 1);
	ln_fast_add_double_static(&r, "event.duration", 14, 1.5);
	ln_fast_add_bool_static(&r, "event.ok", 8, 1);
	ln_fast_add_rawjson_static(&r, "kc", 2, "{\"a\":\"b\"}", 9);
	ln_fast_add_string_static(&r, "host", 4, "h", 1);
	ln_fast_add_string_static(&r, "host.name", 9, "n", 1);       /* replaces host by an object */
	ln_fast_add_string_static(&r, "User.Name", 9, "u", 1);
	ln_fast_add_string_static(&r, "a..b", 4, "odd", 3);
	ln_fast_add_tag(&r, "web");
	struct json_object *root = build_json(&r);

	expect(&r, root, "source.ip", LN_FIND_STRING);
	expect(&r, root, "source.port", LN_FIND_INT);
	expect(&r, root, "url.original", LN_FIND_STRING);
	expect(&r, root, "source", LN_FIND_OTHER);                  /* an object */
	expect(&r, root, "source.ip.x", LN_FIND_OTHER);             /* below a string */
	expect(&r, root, "destination.ip", LN_FIND_ABSENT);
	expect(&r, root, "url.path", LN_FIND_ABSENT);
	expect(&r, root, "event.duration", LN_FIND_OTHER);
	expect(&r, root, "event.ok", LN_FIND_OTHER);
	expect(&r, root, "kc", LN_FIND_OTHER);
	expect(&r, root, "kc.a", LN_FIND_OTHER);
	expect(&r, root, "host", LN_FIND_OTHER);
	expect(&r, root, "host.name", LN_FIND_OTHER);               /* host is a field too */
	expect(&r, root, "user.name", LN_FIND_OTHER);               /* differs only in case */
	expect(&r, root, "Source.IP", LN_FIND_OTHER);
	expect(&r, root, "a.b", LN_FIND_OTHER);                     /* empty component */
	expect(&r, root, "source..ip", LN_FIND_OTHER);
	expect(&r, root, "tags", LN_FIND_OTHER);
	expect(&r, root, "tags.x", LN_FIND_OTHER);
	expect(&r, root, "sourceip", LN_FIND_ABSENT);
	expect(&r, root, "source.ipx", LN_FIND_ABSENT);
	expect(&r, root, "x", LN_FIND_ABSENT);
	/* a locale may fold these into the same key: never absent */
	expect(&r, root, "\xDDrl", LN_FIND_ABSENT);                  /* no field starts with i/I */
	ln_fast_add_string_static(&r, "id", 2, "1", 1);
	json_object_put(root);
	root = build_json(&r);
	/* "\xDD" is a capital dotted I in ISO-8859-9: a Turkish locale folds
	 * it to 'i' */
	expect(&r, root, "\xDD" "d", LN_FIND_OTHER);
	expect(&r, root, "Id", LN_FIND_OTHER);
	expect(&r, root, "id", LN_FIND_STRING);
	CHECK(ln_fast_result_find(&r, "id", 0, NULL, NULL, NULL) == LN_FIND_ABSENT, "empty name");
	CHECK(ln_fast_result_find(NULL, "id", 2, NULL, NULL, NULL) == LN_FIND_ABSENT, "NULL result");
	/* the name need not be terminated */
	CHECK(ln_fast_result_find(&r, "source.ipXYZ", 9, NULL, NULL, NULL) == LN_FIND_STRING, "length bound");
	json_object_put(root);
}

/* Random results from a small alphabet of colliding names. */
static void
random_cases(unsigned iterations)
{
	static const char *const parts[] = {"a", "A", "b", "ab", "source", "Source", "ip", "IP", "i", "\xC4\xB1"};
	static ln_fast_result_t r;
	static char names[LN_FAST_MAX_FIELDS][64];
	char probe[64];
	uint64_t seed = 0x9e3779b97f4a7c15ULL;
	unsigned checked = 0, decided = 0;

#define RND() (seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17, seed)
	for (unsigned it = 0; it < iterations; it++) {
		ln_fast_result_init(&r, NULL);
		const unsigned nf = 1 + RND() % 12;
		for (unsigned i = 0; i < nf; i++) {
			const unsigned depth = 1 + RND() % 3;
			names[i][0] = '\0';
			for (unsigned d = 0; d < depth; d++) {
				if (d) strcat(names[i], (RND() % 40) ? "." : "..");
				strcat(names[i], parts[RND() % (sizeof(parts) / sizeof(parts[0]))]);
			}
			const uint16_t nl = (uint16_t)strlen(names[i]);
			switch (RND() % 6) {
			case 0: ln_fast_add_int_static(&r, names[i], nl, (int64_t)(RND() % 1000)); break;
			case 1: ln_fast_add_double_static(&r, names[i], nl, 0.5); break;
			case 2: ln_fast_add_rawjson_static(&r, names[i], nl, "{\"ip\":\"x\"}", 10); break;
			default: ln_fast_add_string_static(&r, names[i], nl, "v", 1); break;
			}
		}
		if (RND() % 4 == 0) ln_fast_add_tag(&r, "t");
		struct json_object *root = build_json(&r);
		for (unsigned p = 0; p < 16; p++) {
			if (p < nf && RND() % 2) {
				snprintf(probe, sizeof(probe), "%s", names[p]);
			} else {
				const unsigned depth = 1 + RND() % 3;
				probe[0] = '\0';
				for (unsigned d = 0; d < depth; d++) {
					if (d) strcat(probe, ".");
					strcat(probe, parts[RND() % (sizeof(parts) / sizeof(parts[0]))]);
				}
			}
			if (RND() % 8 == 0) snprintf(probe, sizeof(probe), "tags");
			if (check(&r, root, probe) != LN_FIND_OTHER) decided++;
			checked++;
		}
		json_object_put(root);
	}
#undef RND
	printf("random: %u lookups, %u answered without the JSON\n", checked, decided);
}

int
main(int argc, char **argv)
{
	/* rsyslog compares $! keys without case, in the process locale */
	setlocale(LC_ALL, "");
	fjson_global_do_case_sensitive_comparison(0);
	fixed_cases();
	random_cases(argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 20000u);
	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}

#else
int main(void) { return 77; }
#endif
