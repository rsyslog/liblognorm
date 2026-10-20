/**
 * @file rewrite.h
 * @brief Per-rule rename of inlined JSON, CEF extension and name-value keys.
 *
 * A rewrite= line is not an annotation. annotate= adds a constant field.
 * rewrite= decides, while those keys are stored, which of them are kept
 * and under what name. A rule with no rewrite= line is left untouched.
 */
#ifndef LIBLOGNORM_REWRITE_H_INCLUDED
#define LIBLOGNORM_REWRITE_H_INCLUDED

#include "liblognorm.h"
#include <stddef.h>
#include <stdint.h>

struct json_object;
struct ln_parser_s;

typedef struct ln_rewriteSet_s ln_rewriteSet;

struct ln_rw_ent {
	const char *src;
	uint16_t slen;
	const char *dst;
	uint16_t dlen;
	uint8_t lower;		/**< ASCII-lowercase a string value */
};

struct ln_rw_tab {
	struct ln_rw_ent *ents;
	uint32_t n;
	uint32_t mask;		/**< slot count - 1; slot count is a power of two */
	uint32_t *slot;		/**< 0 = empty, else ents index + 1 */
	char *blob;
};

ln_rewriteSet *ln_newRewriteSet(ln_ctx ctx);
void ln_deleteRewriteSet(ln_rewriteSet *rs);

/**
 * Parse one "rewrite=<tag>:<src>=<dst>[|lower]" line.
 * offs is the first byte after "rewrite=".
 * @returns 0 on success, -1 on a malformed line.
 */
int ln_rewrite_add_line(ln_ctx ctx, const char *buf, size_t len, size_t offs);

/**
 * Attach each rewrite tag to the unambiguous %.:json% rule that carries it.
 * Call once the whole rulebase is loaded, before TurboVM compilation.
 * @returns 0 on success, -1 if a tag cannot be placed without ambiguity.
 */
int ln_rewrite_bind(ln_ctx ctx);

const struct ln_rw_tab *ln_rewrite_tabs(ln_ctx ctx, uint32_t *n);
const struct ln_rw_tab *ln_rewrite_tab(ln_ctx ctx, unsigned id);
const struct ln_rw_ent *ln_rw_lookup(const struct ln_rw_tab *t,
				    const char *s, size_t n);

/** rewrite id of the terminal reached linearly from this parser, or 0. */
unsigned ln_rewrite_id_for_parser(const struct ln_parser_s *prs);

/* Indexes in pdag.c parser_lookup_table; turbo.c PRSID_* match. */
#define LN_PRSID_JSON 21
#define LN_PRSID_CEF 24
#define LN_PRSID_NAMEVALUE 26

/**
 * Rewrite table bound to the rule this parser belongs to, or NULL. Returns
 * at once when the rulebase has no rewrite= line, so the rule path is only
 * walked when a table exists.
 */
const struct ln_rw_tab *ln_rewrite_tab_for_parser(ln_ctx ctx, const struct ln_parser_s *prs);

/**
 * Copy leaves of src into dst, renamed through tab. Unmapped leaves are
 * dropped. fail_dup refuses a destination key that is already present.
 * @returns 0 on success, -1 on duplicate or allocation failure.
 */
int ln_rewrite_merge(struct json_object *dst, struct json_object *src,
		    const struct ln_rw_tab *tab, int fail_dup);

#endif
