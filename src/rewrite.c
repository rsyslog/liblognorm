/**
 * @file rewrite.c
 * @brief Rename keys of an inlined JSON object, CEF extensions or name-value pairs.
 *
 * The table for a rule is built once, when the rulebase is loaded. At match
 * time a key is one hash probe: a hit stores the ECS name, a miss stores
 * nothing. Rules that do not carry rewrite= never consult a table.
 */
#include "config.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libestr.h>

#include "lognorm.h"
#include "internal.h"
#include "pdag.h"
#include "rewrite.h"


struct ln_rw_op {
	struct ln_rw_op *next;
	es_str_t *src;
	es_str_t *dst;
	int lower;
};

struct ln_rw_rule {
	struct ln_rw_rule *next;
	es_str_t *tag;
	struct ln_rw_op *head;
	struct ln_rw_op *tail;
	int bound;
};

struct ln_rewriteSet_s {
	ln_ctx ctx;
	struct ln_rw_rule *rules;
	struct ln_rw_tab *tabs;
	uint32_t n_tabs;
	uint32_t cap_tabs;
};

ln_rewriteSet *
ln_newRewriteSet(ln_ctx ctx)
{
	ln_rewriteSet *rs;

	rs = calloc(1, sizeof(*rs));
	if (rs != NULL)
		rs->ctx = ctx;
	return rs;
}

static void
free_op(struct ln_rw_op *op)
{
	if (op->src != NULL)
		es_deleteStr(op->src);
	if (op->dst != NULL)
		es_deleteStr(op->dst);
	free(op);
}

static void
free_rule(struct ln_rw_rule *rule)
{
	struct ln_rw_op *op, *next;

	if (rule->tag != NULL)
		es_deleteStr(rule->tag);
	for (op = rule->head; op != NULL; op = next) {
		next = op->next;
		free_op(op);
	}
	free(rule);
}

static void
free_tab(struct ln_rw_tab *t)
{
	free(t->ents);
	free(t->slot);
	free(t->blob);
	memset(t, 0, sizeof(*t));
}

void
ln_deleteRewriteSet(ln_rewriteSet *rs)
{
	struct ln_rw_rule *rule, *next;
	uint32_t i;

	if (rs == NULL)
		return;
	for (rule = rs->rules; rule != NULL; rule = next) {
		next = rule->next;
		free_rule(rule);
	}
	for (i = 0; i < rs->n_tabs; i++)
		free_tab(&rs->tabs[i]);
	free(rs->tabs);
	free(rs);
}

static int
es_eq_cstr(es_str_t *e, const char *s, size_t n)
{
	if ((size_t)es_strlen(e) != n)
		return 0;
	return memcmp(es_getBufAddr(e), s, n) == 0;
}

static struct ln_rw_rule *
find_rule(ln_rewriteSet *rs, const char *tag, size_t n)
{
	struct ln_rw_rule *rule;

	for (rule = rs->rules; rule != NULL; rule = rule->next) {
		if (es_eq_cstr(rule->tag, tag, n))
			return rule;
	}
	return NULL;
}

static int
name_char(unsigned char c)
{
	return isalnum(c) || c == '_' || c == '.' || c == '@' || c == '-';
}

static uint32_t
fnv1a(const char *s, size_t n)
{
	uint32_t h = 2166136261u;
	size_t i;

	for (i = 0; i < n; i++) {
		h ^= (unsigned char)s[i];
		h *= 16777619u;
	}
	return h;
}

const struct ln_rw_ent *
ln_rw_lookup(const struct ln_rw_tab *t, const char *s, size_t n)
{
	uint32_t i, k;

	if (t == NULL || t->n == 0 || s == NULL)
		return NULL;
	i = fnv1a(s, n) & t->mask;
	for (k = 0; k <= t->mask; k++) {
		uint32_t slot = t->slot[i];
		const struct ln_rw_ent *e;

		if (slot == 0)
			return NULL;
		e = &t->ents[slot - 1];
		if (e->slen == n && memcmp(e->src, s, n) == 0)
			return e;
		i = (i + 1) & t->mask;
	}
	return NULL;
}

const struct ln_rw_tab *
ln_rewrite_tabs(ln_ctx ctx, uint32_t *n)
{
	if (n != NULL)
		*n = 0;
	if (ctx == NULL || ctx->rewrites == NULL)
		return NULL;
	if (n != NULL)
		*n = ctx->rewrites->n_tabs;
	if (ctx->rewrites->n_tabs == 0)
		return NULL;
	return ctx->rewrites->tabs;
}

const struct ln_rw_tab *
ln_rewrite_tab(ln_ctx ctx, unsigned id)
{
	if (ctx == NULL || ctx->rewrites == NULL || id == 0)
		return NULL;
	if (id > ctx->rewrites->n_tabs)
		return NULL;
	return &ctx->rewrites->tabs[id - 1];
}

const struct ln_rw_tab *
ln_rewrite_tab_for_parser(ln_ctx ctx, const struct ln_parser_s *prs)
{
	if (ctx == NULL || ctx->rewrites == NULL || ctx->rewrites->n_tabs == 0
	    || prs == NULL)
		return NULL;
	return ln_rewrite_tab(ctx, ln_rewrite_id_for_parser(prs));
}

unsigned
ln_rewrite_id_for_parser(const struct ln_parser_s *prs)
{
	struct ln_pdag *n;
	int guard = 0;

	if (prs == NULL)
		return 0;
	n = prs->node;
	while (n != NULL && !n->flags.isTerminal && n->nparsers == 1
	       && guard < 8192) {
		n = n->parsers[0].node;
		guard++;
	}
	if (n == NULL || !n->flags.isTerminal || n->nparsers != 0)
		return 0;
	return n->rewrite_id;
}

/* One rewrite= line, as byte ranges into the rulebase buffer. */
struct rw_line {
	size_t tag_b, tag_n;
	size_t src_b, src_n;
	size_t dst_b, dst_n;
	int lower;
};

static size_t
skip_space(const char *buf, size_t len, size_t i)
{
	while (i < len && isspace((unsigned char)buf[i]))
		i++;
	return i;
}

static size_t
skip_name(const char *buf, size_t len, size_t i)
{
	while (i < len && name_char((unsigned char)buf[i]))
		i++;
	return i;
}

/*
 * Split one rewrite= line. i is the first byte after "rewrite=".
 *
 *   [space] tag ':' [space] src [space] '=' [space] dst [space] ['|' lower]
 *   [space] ['#' comment]
 *
 * tag is any run of bytes rule= accepts in a tag (anything but ',' and
 * ':') except whitespace, so every single-word rule tag can carry
 * rewrite= lines. src and dst are letters, digits,
 * '_', '.', '@' or '-'. Both are required and at most 511 bytes, the
 * longest path the walker builds. |lower is
 * the only modifier. Anything else after the destination is rejected.
 *
 * Returns 1 and fills *line for a mapping, -1 after reporting a malformed
 * line. A line without a mapping is malformed: a rule tag may start with
 * '#', so treating "rewrite=#..." as a comment would drop that tag's map
 * without a word.
 */
static int
rw_parse_line(ln_ctx ctx, const char *buf, size_t len, size_t i,
	      struct rw_line *line)
{
	memset(line, 0, sizeof(*line));
	i = skip_space(buf, len, i);

	line->tag_b = i;
	while (i < len && buf[i] != ':' && buf[i] != ','
	       && !isspace((unsigned char)buf[i]))
		i++;
	line->tag_n = i - line->tag_b;
	if (line->tag_n == 0 || i >= len || buf[i] != ':') {
		ln_errprintf(ctx, 0, "rewrite: expected <tag>:<src>=<dst>");
		return -1;
	}

	line->src_b = skip_space(buf, len, i + 1);
	i = skip_name(buf, len, line->src_b);
	line->src_n = i - line->src_b;
	i = skip_space(buf, len, i);
	if (line->src_n == 0 || line->src_n > 511 || i >= len || buf[i] != '=') {
		ln_errprintf(ctx, 0, "rewrite: expected <src>=<dst> after tag");
		return -1;
	}

	line->dst_b = skip_space(buf, len, i + 1);
	i = skip_name(buf, len, line->dst_b);
	line->dst_n = i - line->dst_b;
	if (line->dst_n == 0 || line->dst_n > 511) {
		ln_errprintf(ctx, 0, "rewrite: empty destination name");
		return -1;
	}

	i = skip_space(buf, len, i);
	if (i < len && buf[i] == '|') {
		const size_t m = i + 1;

		for (i = m; i < len && isalpha((unsigned char)buf[i]); i++)
			;
		if (i - m != 5 || strncasecmp(buf + m, "lower", 5) != 0) {
			ln_errprintf(ctx, 0, "rewrite: unknown modifier (only |lower)");
			return -1;
		}
		line->lower = 1;
		i = skip_space(buf, len, i);
	}
	if (i < len && buf[i] != '#') {
		ln_errprintf(ctx, 0, "rewrite: trailing junk");
		return -1;
	}
	return 1;
}

/* The list for a tag, created empty on first use. NULL when out of memory. */
static struct ln_rw_rule *
rw_get_rule(ln_rewriteSet *rs, const char *tag, size_t n)
{
	struct ln_rw_rule *rule;
	es_str_t *str;

	rule = find_rule(rs, tag, n);
	if (rule != NULL)
		return rule;
	rule = calloc(1, sizeof(*rule));
	str = es_newStr(32);
	if (rule == NULL || str == NULL || es_addBuf(&str, (char *)tag, n) != 0) {
		if (str != NULL)
			es_deleteStr(str);
		free(rule);
		return NULL;
	}
	rule->tag = str;
	rule->next = rs->rules;
	rs->rules = rule;
	return rule;
}

/*
 * Append src -> dst to the tag's list. The same source with the same
 * destination and modifier is ignored; the same source with a different
 * destination or modifier is rejected. Returns 0, or -1 when rejected or
 * out of memory.
 */
static int
rw_add_op(ln_ctx ctx, const char *buf, const struct rw_line *line)
{
	struct ln_rw_rule *rule;
	struct ln_rw_op *op, *old;
	es_str_t *src, *dst;

	rule = rw_get_rule(ctx->rewrites, buf + line->tag_b, line->tag_n);
	if (rule == NULL)
		return -1;
	for (old = rule->head; old != NULL; old = old->next) {
		if (!es_eq_cstr(old->src, buf + line->src_b, line->src_n))
			continue;
		if (es_eq_cstr(old->dst, buf + line->dst_b, line->dst_n)
		    && old->lower == line->lower)
			return 0;
		ln_errprintf(ctx, 0, "rewrite: '%.*s' mapped twice for one tag",
			    (int)line->src_n, buf + line->src_b);
		return -1;
	}

	src = es_newStr(32);
	dst = es_newStr(32);
	op = calloc(1, sizeof(*op));
	if (src == NULL || dst == NULL || op == NULL
	    || es_addBuf(&src, (char *)buf + line->src_b, line->src_n) != 0
	    || es_addBuf(&dst, (char *)buf + line->dst_b, line->dst_n) != 0) {
		if (src != NULL)
			es_deleteStr(src);
		if (dst != NULL)
			es_deleteStr(dst);
		free(op);
		return -1;
	}
	op->src = src;
	op->dst = dst;
	op->lower = line->lower;
	if (rule->tail == NULL)
		rule->head = op;
	else
		rule->tail->next = op;
	rule->tail = op;
	return 0;
}

/*
 * Parse one rewrite= line and add its mapping. offs is the first byte after
 * "rewrite=". Lines that share a tag form one list; ln_rewrite_bind attaches
 * the list to a rule after the whole rulebase has been read.
 *
 * Returns 0 on success. Returns -1 when the line is malformed or memory
 * cannot be allocated.
 */
int
ln_rewrite_add_line(ln_ctx ctx, const char *buf, size_t len, size_t offs)
{
	struct rw_line line;
	int r;

	if (ctx->rewrites == NULL)
		return -1;
	r = rw_parse_line(ctx, buf, len, offs, &line);
	if (r <= 0)
		return r;
	return rw_add_op(ctx, buf, &line);
}

static uint32_t
pow2_slots(uint32_t n)
{
	uint32_t s = 4;

	while (s < n * 2)
		s <<= 1;
	return s;
}

static int
tab_add(struct ln_rw_tab *t, uint32_t idx)
{
	const struct ln_rw_ent *e = &t->ents[idx];
	uint32_t i = fnv1a(e->src, e->slen) & t->mask;
	uint32_t k;

	for (k = 0; k <= t->mask; k++) {
		if (t->slot[i] == 0) {
			t->slot[i] = idx + 1;
			return 0;
		}
		i = (i + 1) & t->mask;
	}
	return -1;
}

static const struct ln_rw_ent *
src_present(const struct ln_rw_tab *t, const char *src, size_t sl)
{
	uint32_t i;

	for (i = 0; i < t->n; i++) {
		if (t->ents[i].slen == sl && memcmp(t->ents[i].src, src, sl) == 0)
			return &t->ents[i];
	}
	return NULL;
}

static int
append_ent(struct ln_rw_tab *t, struct ln_rw_op *op, char **blob, size_t *used,
	  size_t *cap)
{
	char *src_c, *dst_c, *buf;
	size_t sl, dl, need;
	struct ln_rw_ent *grown;
	const struct ln_rw_ent *prev;

	src_c = ln_es_str2cstr(&op->src);
	dst_c = ln_es_str2cstr(&op->dst);
	if (src_c == NULL || dst_c == NULL)
		return -1;
	sl = strlen(src_c);
	dl = strlen(dst_c);
	if (sl > UINT16_MAX || dl > UINT16_MAX)
		return -1;
	/* A rule with several tags merges their lists. The same mapping
	 * twice is ignored; one source with two destinations or modifiers is
	 * the conflict ln_rewrite_add_line rejects within one tag. */
	prev = src_present(t, src_c, sl);
	if (prev != NULL) {
		if (prev->dlen == dl && memcmp(prev->dst, dst_c, dl) == 0
		    && prev->lower == op->lower)
			return 0;
		return -2;
	}
	need = sl + 1 + dl + 1;
	if (*used + need > *cap) {
		size_t ncap = *cap ? *cap * 2 : 256;
		char *nblob;
		uint32_t j;

		while (ncap < *used + need)
			ncap *= 2;
		/* First growth starts from a null blob, so there is nothing
		 * to retarget. */
		if (*blob == NULL)
			nblob = malloc(ncap);
		else
			nblob = realloc(*blob, ncap);
		if (nblob == NULL)
			return -1;
		if (*blob != NULL) {
			for (j = 0; j < t->n; j++) {
				t->ents[j].src = nblob + (t->ents[j].src - *blob);
				t->ents[j].dst = nblob + (t->ents[j].dst - *blob);
			}
		}
		*blob = nblob;
		*cap = ncap;
	}
	/* No buffer means the entry cannot be stored. */
	buf = *blob;
	if (buf == NULL)
		return -1;
	if (t->ents == NULL)
		grown = malloc(sizeof(*t->ents));
	else
		grown = realloc(t->ents, (t->n + 1) * sizeof(*t->ents));
	if (grown == NULL)
		return -1;
	t->ents = grown;
	memcpy(buf + *used, src_c, sl + 1);
	t->ents[t->n].src = buf + *used;
	t->ents[t->n].slen = (uint16_t)sl;
	*used += sl + 1;
	memcpy(buf + *used, dst_c, dl + 1);
	t->ents[t->n].dst = buf + *used;
	t->ents[t->n].dlen = (uint16_t)dl;
	t->ents[t->n].lower = op->lower ? 1 : 0;
	*used += dl + 1;
	t->n++;
	return 0;
}

static int
store_tab(ln_rewriteSet *rs, struct ln_rw_tab *tab, unsigned *id_out)
{
	uint32_t slots, i;

	if (rs->n_tabs == 65535)
		return -1;
	slots = pow2_slots(tab->n);
	tab->slot = calloc(slots, sizeof(*tab->slot));
	if (tab->slot == NULL)
		return -1;
	tab->mask = slots - 1;
	for (i = 0; i < tab->n; i++) {
		if (tab_add(tab, i) != 0)
			return -1;
	}
	if (rs->n_tabs == rs->cap_tabs) {
		uint32_t ncap = rs->cap_tabs ? rs->cap_tabs * 2 : 4;
		struct ln_rw_tab *grown = realloc(rs->tabs, ncap * sizeof(*grown));

		if (grown == NULL)
			return -1;
		rs->tabs = grown;
		rs->cap_tabs = ncap;
	}
	rs->tabs[rs->n_tabs] = *tab;
	rs->n_tabs++;
	*id_out = rs->n_tabs; /* 1-based */
	memset(tab, 0, sizeof(*tab)); /* ownership moved */
	return 0;
}

static int
build_tab(ln_rewriteSet *rs, struct json_object *tags, unsigned *id_out)
{
	struct ln_rw_tab tab;
	char *blob = NULL;
	size_t used = 0, cap = 0;
	int i, ntags, rc;

	memset(&tab, 0, sizeof(tab));
	*id_out = 0;
	if (tags == NULL)
		return 0;
	ntags = json_object_array_length(tags);
	for (i = 0; i < ntags; i++) {
		struct json_object *tagObj;
		const char *tag;
		struct ln_rw_rule *rule;
		struct ln_rw_op *op;

		tagObj = json_object_array_get_idx(tags, i);
		if (tagObj == NULL)
			continue;
		tag = json_object_get_string(tagObj);
		if (tag == NULL || tag[0] == '\0')
			continue;
		rule = find_rule(rs, tag, strlen(tag));
		if (rule == NULL)
			continue;
		rule->bound = 1;
		for (op = rule->head; op != NULL; op = op->next) {
			rc = append_ent(&tab, op, &blob, &used, &cap);
			if (rc != 0) {
				if (rc == -2) {
					const char *src = ln_es_str2cstr(&op->src);

					ln_errprintf(rs->ctx, 0,
						"rewrite: '%s' mapped to two destinations by the tags of one rule",
						src != NULL ? src : "?");
				}
				free(tab.ents);
				free(blob);
				return -1;
			}
		}
	}
	if (tab.n == 0) {
		free(blob);
		return 0;
	}
	tab.blob = blob;
	rc = store_tab(rs, &tab, id_out);
	if (rc != 0) {
		free(tab.ents);
		free(tab.slot);
		free(tab.blob);
	}
	return rc;
}

static void
clear_visited(struct ln_pdag *n)
{
	prsid_t i;

	if (n == NULL || !n->flags.visited)
		return;
	n->flags.visited = 0;
	for (i = 0; i < n->nparsers; i++)
		clear_visited(n->parsers[i].node);
}

static void
clear_ids(struct ln_pdag *n)
{
	prsid_t i;

	if (n == NULL || n->flags.visited)
		return;
	n->flags.visited = 1;
	n->rewrite_id = 0;
	for (i = 0; i < n->nparsers; i++)
		clear_ids(n->parsers[i].node);
}

static int
terminal_has_rewrite(ln_rewriteSet *rs, struct ln_pdag *n)
{
	int i, ntags;

	if (n->tags == NULL)
		return 0;
	ntags = json_object_array_length(n->tags);
	for (i = 0; i < ntags; i++) {
		struct json_object *tagObj = json_object_array_get_idx(n->tags, i);
		const char *tag;

		if (tagObj == NULL)
			continue;
		tag = json_object_get_string(tagObj);
		if (tag != NULL && find_rule(rs, tag, strlen(tag)) != NULL)
			return 1;
	}
	return 0;
}

static struct ln_pdag *
linear_terminal(struct ln_pdag *n)
{
	int guard = 0;

	while (n != NULL && !n->flags.isTerminal && n->nparsers == 1
	       && guard < 8192) {
		n = n->parsers[0].node;
		guard++;
	}
	if (n == NULL || !n->flags.isTerminal)
		return NULL;
	return n;
}

static int
bind_node(ln_ctx ctx, struct ln_pdag *n, int depth)
{
	prsid_t i;

	if (n == NULL || n->flags.visited || depth > 8192)
		return 0;
	n->flags.visited = 1;
	for (i = 0; i < n->nparsers; i++) {
		ln_parser_t *prs = &n->parsers[i];
		struct ln_pdag *term;
		unsigned id = 0;

		if ((prs->prsid == LN_PRSID_JSON && prs->name != NULL
		     && prs->name[0] == '.' && prs->name[1] == '\0')
		    || prs->prsid == LN_PRSID_CEF
		    || prs->prsid == LN_PRSID_NAMEVALUE) {
			term = linear_terminal(prs->node);
			if (term != NULL && term->nparsers != 0
			    && terminal_has_rewrite(ctx->rewrites, term)) {
				ln_errprintf(ctx, 0,
					"rewrite: open-key rule is a prefix of a longer rule");
				return -1;
			}
			if (term != NULL && term->nparsers == 0
			    && terminal_has_rewrite(ctx->rewrites, term)) {
				if (term->rewrite_id == 0) {
					if (build_tab(ctx->rewrites, term->tags, &id) != 0)
						return -1;
					term->rewrite_id = id;
				}
			}
		}
		if (bind_node(ctx, prs->node, depth + 1) != 0)
			return -1;
	}
	return 0;
}

int
ln_rewrite_bind(ln_ctx ctx)
{
	struct ln_rw_rule *rule;
	uint32_t i;
	int rc;

	if (ctx == NULL || ctx->rewrites == NULL)
		return 0;
	if (ctx->rewrites->rules == NULL)
		return 0;

	for (rule = ctx->rewrites->rules; rule != NULL; rule = rule->next)
		rule->bound = 0;
	for (i = 0; i < ctx->rewrites->n_tabs; i++)
		free_tab(&ctx->rewrites->tabs[i]);
	ctx->rewrites->n_tabs = 0;

	clear_ids(ctx->pdag);
	clear_visited(ctx->pdag);
	rc = bind_node(ctx, ctx->pdag, 0);
	clear_visited(ctx->pdag);
	if (rc != 0)
		return -1;

	for (rule = ctx->rewrites->rules; rule != NULL; rule = rule->next) {
		char *tag;

		if (rule->bound)
			continue;
		tag = ln_es_str2cstr(&rule->tag);
		ln_errprintf(ctx, 0,
			"rewrite tag '%s' is not on one %%.:json%%, %%:cef%% or %%:name-value-list%% rule",
			tag != NULL ? tag : "?");
		return -1;
	}
	return 0;
}

static void
ascii_lower(char *s, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (s[i] >= 'A' && s[i] <= 'Z')
			s[i] = (char)(s[i] + 32);
	}
}

/* outer is the event object the staged leaves will join; with fail_dup a
 * key already there is a duplicate as well. */
static int
put_leaf(struct json_object *dst, struct json_object *outer, const char *key,
	struct json_object *val, int lower, int fail_dup)
{
	struct json_object *stored = val;

	if (fail_dup && (json_object_object_get_ex(dst, key, NULL)
			 || json_object_object_get_ex(outer, key, NULL)))
		return -1;
	if (lower && json_object_is_type(val, json_type_string)) {
		int n = json_object_get_string_len(val);
		const char *s = json_object_get_string(val);
		char *buf;

		if (s == NULL)
			return -1;
		buf = malloc((size_t)n + 1);
		if (buf == NULL)
			return -1;
		memcpy(buf, s, (size_t)n);
		buf[n] = '\0';
		ascii_lower(buf, n);
		stored = json_object_new_string_len(buf, n);
		free(buf);
		if (stored == NULL)
			return -1;
	} else {
		json_object_get(val);
	}
	json_object_object_add(dst, key, stored);
	return 0;
}

static int
merge_obj(struct json_object *dst, struct json_object *outer,
	 struct json_object *src, const struct ln_rw_tab *tab, int fail_dup,
	 char *path, size_t plen)
{
	struct json_object_iterator it, end;

	it = json_object_iter_begin(src);
	end = json_object_iter_end(src);
	if (json_object_iter_equal(&it, &end)) {
		const struct ln_rw_ent *e;

		if (plen == 0)
			return 0;
		e = ln_rw_lookup(tab, path, plen);
		if (e == NULL)
			return 0;
		return put_leaf(dst, outer, e->dst, src, e->lower, fail_dup);
	}
	while (!json_object_iter_equal(&it, &end)) {
		const char *key = json_object_iter_peek_name(&it);
		struct json_object *val = json_object_iter_peek_value(&it);
		size_t klen = strlen(key);
		size_t saved = plen;
		const struct ln_rw_ent *e;
		int rc = 0;

		/* Reject before the add. A huge key must not wrap size_t and
		 * walk off path[512]. One compare, walker merge only. */
		if (klen >= 512 || plen >= 512
		    || klen >= 512 - plen - (plen != 0))
			return -1;
		if (plen)
			path[plen++] = '.';
		memcpy(path + plen, key, klen);
		plen += klen;
		path[plen] = '\0';

		if (json_object_is_type(val, json_type_object))
			rc = merge_obj(dst, outer, val, tab, fail_dup, path, plen);
		else {
			e = ln_rw_lookup(tab, path, plen);
			if (e != NULL)
				rc = put_leaf(dst, outer, e->dst, val, e->lower, fail_dup);
		}
		if (rc != 0)
			return -1;
		plen = saved;
		path[plen] = '\0';
		json_object_iter_next(&it);
	}
	return 0;
}

int
ln_rewrite_merge(struct json_object *dst, struct json_object *src,
		const struct ln_rw_tab *tab, int fail_dup)
{
	char path[512];
	struct json_object *staged;
	struct json_object_iterator it, end;

	if (dst == NULL || src == NULL || tab == NULL)
		return -1;
	/* Stage the renamed leaves and move them into the event only when the
	 * whole object merged: a failure part-way must not leave some renamed
	 * keys in the event (unparsed-data, or an alternative rule). */
	staged = json_object_new_object();
	if (staged == NULL)
		return -1;
	path[0] = '\0';
	if (merge_obj(staged, dst, src, tab, fail_dup, path, 0) != 0) {
		json_object_put(staged);
		return -1;
	}
	it = json_object_iter_begin(staged);
	end = json_object_iter_end(staged);
	while (!json_object_iter_equal(&it, &end)) {
		json_object_object_add(dst, json_object_iter_peek_name(&it),
			json_object_get(json_object_iter_peek_value(&it)));
		json_object_iter_next(&it);
	}
	json_object_put(staged);
	return 0;
}
