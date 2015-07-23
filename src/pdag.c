/**
 * @file pdag.c
 * @brief Implementation of the parse dag object.
 * @class ln_pdag pdag.h
 *//*
 * Copyright 2015 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <libestr.h>

#include "json_compatibility.h"
#include "liblognorm.h"
#include "lognorm.h"
#include "samp.h"
#include "pdag.h"
#include "annot.h"
#include "internal.h"
#include "parser.h"


/* parser lookup table
 * This is a memory- and cache-optimized way of calling parsers.
 * VERY IMPORTANT: the initialization must be done EXACTLY in the
 * order of parser IDs (also see comment in pdag.h).
 */
#define PARSER_ENTRY_NO_DATA(identifier, parser) \
{ identifier, NULL, ln_parse##parser, NULL }
#define PARSER_ENTRY(identifier, parser) \
{ identifier, ln_construct##parser, ln_parse##parser, ln_destruct##parser }
static struct ln_parser_info parser_lookup_table[] = {
	PARSER_ENTRY("literal", Literal),
	PARSER_ENTRY_NO_DATA("date-rfc3164", RFC3164Date),
	PARSER_ENTRY_NO_DATA("date-rfc5424", RFC5424Date),
	PARSER_ENTRY_NO_DATA("number", Number),
	PARSER_ENTRY_NO_DATA("float", Float),
	PARSER_ENTRY("hexnumber", HexNumber),
	PARSER_ENTRY_NO_DATA("kernel-timestamp", KernelTimestamp),
	PARSER_ENTRY_NO_DATA("whitespace", Whitespace),
	PARSER_ENTRY_NO_DATA("ipv4", IPv4),
	PARSER_ENTRY_NO_DATA("ipv6", IPv6),
	PARSER_ENTRY_NO_DATA("word", Word),
	PARSER_ENTRY_NO_DATA("alpha", Alpha),
	PARSER_ENTRY_NO_DATA("rest", Rest),
	PARSER_ENTRY_NO_DATA("op-quoted-string", OpQuotedString),
	PARSER_ENTRY_NO_DATA("quoted-string", QuotedString),
	PARSER_ENTRY_NO_DATA("date-iso", ISODate),
	PARSER_ENTRY_NO_DATA("time-24hr", Time24hr),
	PARSER_ENTRY_NO_DATA("time-12hr", Time12hr),
	PARSER_ENTRY_NO_DATA("duration", Duration),
	PARSER_ENTRY_NO_DATA("cisco-interface-spec", CiscoInterfaceSpec),
	PARSER_ENTRY_NO_DATA("name-value-list", NameValue),
	PARSER_ENTRY_NO_DATA("json", JSON),
	PARSER_ENTRY_NO_DATA("cee-syslog", CEESyslog),
	PARSER_ENTRY_NO_DATA("mac48", MAC48),
	PARSER_ENTRY_NO_DATA("cef", CEF),
	PARSER_ENTRY_NO_DATA("checkpoint-lea", CheckpointLEA),
	PARSER_ENTRY_NO_DATA("v2-iptables", v2IPTables),
	PARSER_ENTRY("string-to", StringTo),
	PARSER_ENTRY("char-to", CharTo),
	PARSER_ENTRY("char-sep", CharSeparated)
};
#define NPARSERS (sizeof(parser_lookup_table)/sizeof(struct ln_parser_info))

static inline const char *
parserName(const prsid_t id)
{
	const char *name;
	if(id == PRS_CUSTOM_TYPE)
		name = "USER-DEFINED";
	else
		name = parser_lookup_table[id].name;
	return name;
}

prsid_t 
ln_parserName2ID(const char *const __restrict__ name)
{
	unsigned i;
	for(  i = 0
	    ; i < sizeof(parser_lookup_table) / sizeof(struct ln_parser_info)
	    ; ++i) {
	    	if(!strcmp(parser_lookup_table[i].name, name))
			return i;
	    }
	return PRS_INVALID;
}

/* find type pdag in table. If "bAdd" is seit, add it if not
 * already present.
 * Returns NULL on error, ptr to type pdag entry otherwise
 */
struct ln_type_pdag *
ln_pdagFindType(ln_ctx ctx, const char *const __restrict__ name, const int bAdd)
{
	struct ln_type_pdag *td = NULL;
	int i;

	for(i = 0 ; i < ctx->nTypes ; ++i) {
		if(!strcmp(ctx->type_pdags[i].name, name))
			td = ctx->type_pdags + i;
			goto done;
	}

	if(!bAdd) {
		ln_dbgprintf(ctx, "custom type '%s' not found", name);
		goto done;
	}

	/* type does not yet exist -- create entry */
	struct ln_type_pdag *newarr;
	newarr = realloc(ctx->type_pdags, sizeof(struct ln_type_pdag) * (ctx->nTypes+1));
	if(newarr == NULL) {
		ln_dbgprintf(ctx, "ln_pdagFindTypeAG: alloc newarr failed");
		goto done;
	}
	ctx->type_pdags = newarr;
	td = ctx->type_pdags + ctx->nTypes;
	++ctx->nTypes;
	td->name = strdup(name);
	td->pdag = ln_newPDAG(ctx);
done:
	return td;
}

/* we clear some multiple times, but as long as we have no loops
 * (dag!) we have no real issue.
 */
static void
ln_pdagComponentClearVisited(struct ln_pdag *const dag)
{
	dag->flags.visited = 0;
	for(int i = 0 ; i < dag->nparsers ; ++i) {
		ln_parser_t *prs = dag->parsers+i;
		ln_pdagComponentClearVisited(prs->node);
	}
}
static void
ln_pdagClearVisited(ln_ctx ctx)
{
	for(int i = 0 ; i < ctx->nTypes ; ++i)
		ln_pdagComponentClearVisited(ctx->type_pdags[i].pdag);
	ln_pdagComponentClearVisited(ctx->pdag);
}

/**
 * Process a parser defintion. Note that a single defintion can potentially
 * contain many parser instances.
 * @return parser node ptr or NULL (on error)
 */
ln_parser_t*
ln_newParser(ln_ctx ctx,
	json_object *prscnf)
{
	ln_parser_t *node = NULL;
	json_object *json;
	const char *val;
	prsid_t prsid;
	struct ln_type_pdag *custType;
	const char *name = NULL;
	const char *extraData = NULL;

	ln_dbgprintf(ctx, "in ln_newParser: %s", json_object_to_json_string(prscnf));
	json = json_object_object_get(prscnf, "type");
	if(json == NULL) {
		ln_errprintf(ctx, 0, "parser type missing in config: %s",
			json_object_to_json_string(prscnf));
		goto done;
	}
	val = json_object_get_string(json);
	if(*val == '@') {
		prsid = PRS_CUSTOM_TYPE;
		custType = ln_pdagFindType(ctx, val, 0);
		if(custType == NULL) {
			ln_errprintf(ctx, 0, "unknown user-defined type '%s'", val);
			goto done;
		}
	} else {
		prsid = ln_parserName2ID(val);
		if(prsid == PRS_INVALID) {
			ln_errprintf(ctx, 0, "invalid field type '%s'", val);
			goto done;
		}
	}

	json = json_object_object_get(prscnf, "name");
	if(json == NULL) {
		name = strdup("-");
	} else {
		name = strdup(json_object_get_string(json));
	}

	json = json_object_object_get(prscnf, "extradata");
	if(json == NULL) {
		extraData = NULL;
	} else {
		extraData = strdup(json_object_get_string(json));
	}

	/* we need to remove already processed items from the config, so
	 * that we can pass the remaining parameters to the parser.
	 */
	json_object_object_del(prscnf, "type");
	json_object_object_del(prscnf, "name");
	json_object_object_del(prscnf, "extradata");

	/* got all data items */
	if((node = calloc(1, sizeof(ln_parser_t))) == NULL) {
		ln_dbgprintf(ctx, "lnNewParser: alloc node failed");
		goto done;
	}

	node->node = NULL;
	node->prio = 0;
	node->name = name;
	node->prsid = prsid;
	if(prsid == PRS_CUSTOM_TYPE) {
		node->custType = custType;
	} else {
		if(parser_lookup_table[prsid].construct != NULL) {
			parser_lookup_table[prsid].construct(ctx, extraData, prscnf, &node->parser_data);
		}
	}
done:
	ln_dbgprintf(ctx, "out ln_newParser [node %p]: %s", node, json_object_to_json_string(prscnf));
	free((void*)extraData);
	return node;
}

#if 0
/**
 *  Construct a new literal parser.
 */
ln_parser_t *
ln_newLiteralParser(ln_ctx ctx, char lit)
{
	char buf[] = "x";
	buf[0] = lit;
	struct json_object *val;
	struct json_object *prscnf = json_object_new_object();
	val = json_object_new_string("-");
	json_object_object_add(prscnf, "name", val);

	val = json_object_new_string("literal");
	json_object_object_add(prscnf, "type", val);

	val = json_object_new_string(buf);
	json_object_object_add(prscnf, "extradata", val);

	ln_parser_t *parser = ln_newParser(ctx, prscnf);
	return parser;
}
#endif

struct ln_pdag*
ln_newPDAG(ln_ctx ctx)
{
	struct ln_pdag *dag;

	if((dag = calloc(1, sizeof(struct ln_pdag))) == NULL)
		goto done;
	
	dag->refcnt = 1;
	dag->ctx = ctx;
	ctx->nNodes++;
done:	return dag;
}

/* note: we must NOT free the parser itself, because
 * it is stored inside a parser table (so no single
 * alloc for the parser!).
 */
static void
pdagDeletePrs(ln_ctx ctx, ln_parser_t *const __restrict__ prs)
{
	// TODO: be careful here: once we move to real DAG from tree, we
	// cannot simply delete the next node! (refcount? something else?)
	if(prs->node != NULL)
		ln_pdagDelete(prs->node);
	free((void*)prs->name);
	if(prs->parser_data != NULL)
		parser_lookup_table[prs->prsid].destruct(ctx, prs->parser_data);
}

void
ln_pdagDelete(struct ln_pdag *const __restrict__ pdag)
{
	if(pdag == NULL)
		goto done;

	--pdag->refcnt;
	if(pdag->refcnt > 0)
		goto done;

	if(pdag->tags != NULL)
		json_object_put(pdag->tags);

	for(int i = 0 ; i < pdag->nparsers ; ++i) {
		pdagDeletePrs(pdag->ctx, pdag->parsers+i);
	}
	free(pdag->parsers);
	free(pdag);
done:	return;
}


/**
 * pdag optimizer step: literal path compaction
 *
 * We compress as much as possible and evalute the path down to
 * the first non-compressable element.
 */
static inline int
optLitPathCompact(ln_ctx ctx, ln_parser_t *prs)
{
	int r = 0;

	while(prs != NULL) {
		if(!(   prs->prsid == PRS_LITERAL
		     && prs->node->nparsers == 1
		     && prs->node->parsers[0].prsid == PRS_LITERAL)
		  )
			goto done;
		// TODO: think about names if literal is actually to be parsed!
		// check name == "-"?
		// also check if isTerminal!

		/* ok, we have two literals in a row, let's compact the nodes */
		ln_parser_t *child_prs = prs->node->parsers;
		ln_dbgprintf(ctx, "opt path compact: add %p to %p", child_prs, prs);
		CHKR(ln_combineData_Literal(prs->parser_data, child_prs->parser_data));
		ln_pdag *const node_del = prs->node;
		prs->node = child_prs->node;

		child_prs->node = NULL; /* remove, else this would be destructed! */
		ln_pdagDelete(node_del);
	}
done:
	return r;
}

static int
ln_pdagComponentOptimize(ln_ctx ctx, struct ln_pdag *const dag)
{
	int r = 0;

	for(int i = 0 ; i < dag->nparsers ; ++i) {
		ln_parser_t *prs = dag->parsers+i;
		ln_dbgprintf(dag->ctx, "optimizing %p: field %d type '%s', name '%s': '%s':",
			prs->node, i, parserName(prs->prsid), prs->name, prs->parser_data);
		
		optLitPathCompact(ctx, prs);

		ln_pdagComponentOptimize(ctx, prs->node);
	}
	return r;
}
/**
 * Optimize the pdag.
 * This includes all components.
 */
int
ln_pdagOptimize(ln_ctx ctx)
{
	int r = 0;

	for(int i = 0 ; i < ctx->nTypes ; ++i) {
		ln_dbgprintf(ctx, "optimizing component %s\n", ctx->type_pdags[i].name);
		ln_pdagComponentOptimize(ctx, ctx->type_pdags[i].pdag);
	}

	ln_dbgprintf(ctx, "optimizing main pdag component\n");
	ln_pdagComponentOptimize(ctx, ctx->pdag);
ln_dbgprintf(ctx, "---AFTER OPTIMIZATION------------------");
ln_displayPDAG(ctx);
ln_dbgprintf(ctx, "=======================================");
	return r;
}


/* data structure for pdag statistics */
struct pdag_stats {
	int nodes;
	int term_nodes;
	int parsers;
	int max_nparsers;
	int nparsers_cnt[100];
	int nparsers_100plus;
	int *prs_cnt;
};

/**
 * Recursive step of statistics gatherer.
 */
static int
ln_pdagStatsRec(ln_ctx ctx, struct ln_pdag *const dag, struct pdag_stats *const stats)
{
	if(dag->flags.visited)
		return 0;
	dag->flags.visited = 1;
	stats->nodes++;
	if(dag->flags.isTerminal)
		stats->term_nodes++;
	if(dag->nparsers > stats->max_nparsers)
		stats->max_nparsers = dag->nparsers;
	if(dag->nparsers >= 100)
		stats->nparsers_100plus++;
	else
		stats->nparsers_cnt[dag->nparsers]++;
	stats->parsers += dag->nparsers;
	int max_path = 0;
	for(int i = 0 ; i < dag->nparsers ; ++i) {
		ln_parser_t *prs = dag->parsers+i;
		stats->prs_cnt[prs->prsid]++;
		const int path_len = ln_pdagStatsRec(ctx, prs->node, stats);
		if(path_len > max_path)
			max_path = path_len;
	}
	return max_path + 1;
}

/**
 * Gather pdag statistics for a *specific* pdag.
 *
 * Data is sent to given file ptr.
 */
void
ln_pdagStats(ln_ctx ctx, struct ln_pdag *const dag, FILE *const fp)
{
	struct pdag_stats *const stats = calloc(1, sizeof(struct pdag_stats));
	stats->prs_cnt = calloc(NPARSERS, sizeof(int));
	ln_pdagClearVisited(ctx);
	const int longest_path = ln_pdagStatsRec(ctx, dag, stats);

	fprintf(fp, "nodes.............: %4d\n", stats->nodes);
	fprintf(fp, "terminal nodes....: %4d\n", stats->term_nodes);
	fprintf(fp, "parsers entries...: %4d\n", stats->parsers);
	fprintf(fp, "longest path......: %4d\n", longest_path);

	fprintf(fp, "Parser Type Counts:\n");
	for(prsid_t i = 0 ; i < NPARSERS ; ++i) {
		if(stats->prs_cnt[i] != 0)
			fprintf(fp, "\t%20s: %d\n", parserName(i), stats->prs_cnt[i]);
	}

	int pp = 0;
	fprintf(fp, "Parsers per Node:\n");
	fprintf(fp, "\tmax:\t%4d\n", stats->max_nparsers);
	for(int i = 0 ; i < 100 ; ++i) {
		pp += stats->nparsers_cnt[i];
		if(stats->nparsers_cnt[i] != 0)
			fprintf(fp, "\t%d:\t%4d\n", i, stats->nparsers_cnt[i]);
	}

	free(stats->prs_cnt);
	free(stats);
}

/**
 * Gather and output pdag statistics for the full pdag (ctx)
 * including all disconnected components (type defs).
 *
 * Data is sent to given file ptr.
 */
void
ln_fullPdagStats(ln_ctx ctx, FILE *const fp)
{
	fprintf(fp, "User-Defined Types\n"
	            "==================\n");
	fprintf(fp, "number types: %d\n", ctx->nTypes);
	for(int i = 0 ; i < ctx->nTypes ; ++i)
		fprintf(fp, "type: %s\n", ctx->type_pdags[i].name);

	for(int i = 0 ; i < ctx->nTypes ; ++i) {
		fprintf(fp, "\n"
			    "type PDAG: %s\n"
		            "----------\n", ctx->type_pdags[i].name);
		ln_pdagStats(ctx, ctx->type_pdags[i].pdag, fp);
	}

	fprintf(fp, "\n"
		    "Main PDAG\n"
	            "=========\n");
	ln_pdagStats(ctx, ctx->pdag, fp);
}

/**
 * Check if the provided dag is a leaf. This means that it
 * does not contain any subdags.
 * @return 1 if it is a leaf, 0 otherwise
 */
static inline int
isLeaf(struct ln_pdag *dag)
{
	return dag->nparsers == 0 ? 1 : 0;
}


// TODO: how to *exactly* handle detection of same parser type with
//       different parameters. This is an important use case, especially
//       when we get more generic parsers.
/**
 * Add a parser instance to the pdag at the current position.
 *
 * @param[in] ctx
 * @param[in] prscnf json parser config *object* (no array!)
 * @param[in] pdag current pdag position (to which parser is to be added)
 * @param[in/out] nextnode contains point to the next node, either 
 *            an existing one or one newly created.
 *
 * The nextnode parameter permits to use this function to create
 * multiple parsers alternative parsers with a single run. To do so,
 * set nextnode=NULL on first call. On successive calls, keep the
 * value. If a value is present, we will not accept non-identical
 * parsers which point to different nodes - this will result in an
 * error.
 *
 * IMPORTANT: the caller is responsible to update its pdag pointer
 *            to the nextnode value when he is done adding parsers.
 *
 * If a parser of the same type with identical data already exists,
 * it is "resued", which means the function is effectively used to
 * walk the path. This is used during parser construction to
 * navigate to new parts of the pdag.
 */
static int
ln_pdagAddParserInstance(ln_ctx ctx,
	json_object *const __restrict__ prscnf,
	struct ln_pdag *const __restrict__ pdag,
	struct ln_pdag **nextnode)
{
	int r;
	ln_parser_t *const parser = ln_newParser(ctx, prscnf);
	CHKN(parser);
	ln_dbgprintf(ctx, "pdag: %p, parser %p", pdag, parser);
	/* check if we already have this parser, if so, merge
	 */
	int i;
	for(i = 0 ; i < pdag->nparsers ; ++i) {
		if(   pdag->parsers[i].prsid == parser->prsid
		   && !strcmp(pdag->parsers[i].name, parser->name)) {
			// FIXME: work-around for literal parser with different
			//        literals (see header TODO)
			// FIXME: if nextnode is set, check we can actually combine, 
			//        else err out
			if(parser->prsid == PRS_LITERAL &&
			   ((char*)pdag->parsers[i].parser_data)[0] != ((char*)parser->parser_data)[0])
			   	continue;
			*nextnode = pdag->parsers[i].node;
			r = 0;
			ln_dbgprintf(ctx, "merging with pdag %p", pdag);
			pdagDeletePrs(ctx, parser); /* no need for data items */
			goto done;
		}
	}
	/* if we reach this point, we have a new parser type */
	if(*nextnode == NULL) {
		CHKN(*nextnode = ln_newPDAG(ctx)); /* we need a new node */
	} else {
		(*nextnode)->refcnt++;
	}
	parser->node = *nextnode;
	ln_parser_t *const newtab
		= realloc(pdag->parsers, (pdag->nparsers+1) * sizeof(ln_parser_t));
	CHKN(newtab);
	pdag->parsers = newtab;
	memcpy(pdag->parsers+pdag->nparsers, parser, sizeof(ln_parser_t));
	pdag->nparsers++;

	r = 0;

done:
	free(parser);
	return r;
}


/**
 * add parsers to current pdag. This is used
 * to add parsers stored in an array. The mode specifies
 * how parsers shall be added.
 */
#define PRS_ADD_MODE_SEQ 0
#define PRS_ADD_MODE_ALTERNATIVE 1
static int
ln_pdagAddParsers(ln_ctx ctx,
	json_object *const prscnf,
	const int mode,
	struct ln_pdag **pdag)
{
	int r = LN_BADCONFIG;
	struct ln_pdag *nextnode = NULL;
	
	const int lenarr = json_object_array_length(prscnf);
	for(int i = 0 ; i < lenarr ; ++i) {
		struct json_object *const curr_prscnf =
			json_object_array_get_idx(prscnf, i);
		ln_dbgprintf(ctx, "parser %d: %s", i, json_object_to_json_string(curr_prscnf));
		CHKR(ln_pdagAddParserInstance(ctx, curr_prscnf, *pdag, &nextnode));
		if(mode == PRS_ADD_MODE_SEQ) {
			*pdag = nextnode;
			nextnode = NULL;
		}
	}

	if(mode != PRS_ADD_MODE_SEQ)
		*pdag = nextnode;
	r = 0;
done:
	return r;
}

/* add a json parser config object. Note that this object may contain
 * multiple parser instances.
 */
int
ln_pdagAddParser(ln_ctx ctx, struct ln_pdag **pdag, json_object *const prscnf)
{
	int r = LN_BADCONFIG;
	struct ln_pdag *nextnode = NULL;
	
	if(json_object_get_type(prscnf) == json_type_object) {
		/* check for special types we need to handle here */
		struct json_object *json = json_object_object_get(prscnf, "type");
		const char *const ftype = json_object_get_string(json);
		if(!strcmp(ftype, "alternative")) {
			json = json_object_object_get(prscnf, "parser");
			if(json_object_get_type(json) != json_type_array) {
				ln_errprintf(ctx, 0, "alternative type needs array of parsers. "
					"Object: '%s', type is %s",
					json_object_to_json_string(prscnf),
					json_type_to_name(json_object_get_type(json)));
				goto done;
			}
			CHKR(ln_pdagAddParsers(ctx, json, PRS_ADD_MODE_ALTERNATIVE, pdag));
		} else {
			CHKR(ln_pdagAddParserInstance(ctx, prscnf, *pdag, &nextnode));
			*pdag = nextnode;
		}
	} else if(json_object_get_type(prscnf) == json_type_array) {
		CHKR(ln_pdagAddParsers(ctx, prscnf, PRS_ADD_MODE_SEQ, pdag));
	} else {
		ln_errprintf(ctx, 0, "bug: prscnf object of wrong type. Object: '%s'",
			json_object_to_json_string(prscnf));
		goto done;
	}

done:
	json_object_put(prscnf);
	return r;
}


void
ln_displayPDAGComponent(struct ln_pdag *dag, int level)
{
	char indent[2048];

	if(level > 1023)
		level = 1023;
	memset(indent, ' ', level * 2);
	indent[level * 2] = '\0';

	ln_dbgprintf(dag->ctx, "%ssubDAG%s %p (children: %d parsers)",
		     indent, dag->flags.isTerminal ? " [TERM]" : "", dag, dag->nparsers);

	for(int i = 0 ; i < dag->nparsers ; ++i) {
		ln_dbgprintf(dag->ctx, "%sfield type '%s', name '%s': '%p':", indent,
			parserName(dag->parsers[i].prsid),
			dag->parsers[i].name,
			dag->parsers[i].parser_data);
		ln_displayPDAGComponent(dag->parsers[i].node, level + 1);
	}
}



/* developer debug aid, to be used for example as follows:
   ln_dbgprintf(dag->ctx, "---------------------------------------");
   ln_displayPDAG(dag);
   ln_dbgprintf(dag->ctx, "=======================================");
 */
void
ln_displayPDAG(ln_ctx ctx)
{
	for(int i = 0 ; i < ctx->nTypes ; ++i) {
		ln_dbgprintf(ctx, "COMPONENT: %s", ctx->type_pdags[i].name);
		ln_displayPDAGComponent(ctx->type_pdags[i].pdag, 0);
	}

	ln_dbgprintf(ctx, "MAIN COMPONENT:");
	ln_displayPDAGComponent(ctx->pdag, 0);
}


/* the following is a quick hack, which should be moved to the
 * string class.
 */
static inline void dotAddPtr(es_str_t **str, void *p)
{
	char buf[64];
	int i;
	i = snprintf(buf, sizeof(buf), "l%p", p);
	es_addBuf(str, buf, i);
}
struct data_Literal { const char *lit; }; // TODO remove when this hack is no longe needed
/**
 * recursive handler for DOT graph generator.
 */
static void
ln_genDotPDAGGraphRec(struct ln_pdag *dag, es_str_t **str)
{
	ln_dbgprintf(dag->ctx, "in dot: %p, visited %d", dag, (int) dag->flags.visited);
	if(dag->flags.visited)
		return; /* already processed this subpart */
	dag->flags.visited = 1;
	dotAddPtr(str, dag);
	es_addBufConstcstr(str, " [ label=\"n\"");

	if(isLeaf(dag)) {
		es_addBufConstcstr(str, " style=\"bold\"");
	}
	es_addBufConstcstr(str, "]\n");

	/* display field subdags */

	for(int i = 0 ; i < dag->nparsers ; ++i) {
		ln_parser_t *const prs = dag->parsers+i;
		dotAddPtr(str, dag);
		es_addBufConstcstr(str, " -> ");
		dotAddPtr(str, prs->node);
		es_addBufConstcstr(str, " [label=\"");
		es_addBuf(str, parserName(prs->prsid), strlen(parserName(prs->prsid)));
		es_addBufConstcstr(str, ":");
		//es_addStr(str, node->name);
		if(prs->prsid == PRS_LITERAL) {
			for(const char *p = ((struct data_Literal*)prs->parser_data)->lit ; *p ; ++p) {
				// TODO: handle! if(*p == '\\')
					//es_addChar(str, '\\');
				if(*p != '\\' && *p != '"')
					es_addChar(str, *p);
			}
		}
		es_addBufConstcstr(str, "\"");
		es_addBufConstcstr(str, " style=\"dotted\"]\n");
		ln_genDotPDAGGraphRec(prs->node, str);
	}
}


void
ln_genDotPDAGGraph(struct ln_pdag *dag, es_str_t **str)
{
	ln_pdagClearVisited(dag->ctx);
	es_addBufConstcstr(str, "digraph pdag {\n");
	ln_genDotPDAGGraphRec(dag, str);
	es_addBufConstcstr(str, "}\n");
}


/**
 * add unparsed string to event.
 */
static inline int
addUnparsedField(const char *str, const size_t strLen, const size_t offs, struct json_object *json)
{
	int r = 1;
	struct json_object *value;
	char *s = NULL;
	CHKN(s = strndup(str, strLen));
	value = json_object_new_string(s);
	if (value == NULL) {
		goto done;
	}
	json_object_object_add(json, ORIGINAL_MSG_KEY, value);
	
	value = json_object_new_string(s + offs);
	if (value == NULL) {
		goto done;
	}
	json_object_object_add(json, UNPARSED_DATA_KEY, value);

	r = 0;
done:
	free(s);
	return r;
}


// TODO: remove once all parsers properly generate JSON
static int
fixJSON(struct ln_pdag *dag,
	struct json_object **value,
	struct json_object *json,
	const ln_parser_t *const prs)

{
	int r = LN_WRONGPARSER;

	ln_dbgprintf(dag->ctx, "in  field name '%s', json: '%s', value: '%s'", prs->name, json_object_to_json_string(json), json_object_to_json_string(*value));
	if(!strcmp(prs->name, "-")) {
		if (*value != NULL) {
			/* Free the unneeded value */
			json_object_put(*value);
		}
	} else if(!strcmp(prs->name, ".")) {
		if(json_object_get_type(*value) == json_type_object) {
			json_object_object_foreach(*value, key, val) {
				ln_dbgprintf(dag->ctx, "key: %s, json: %s", key, json_object_to_json_string(val));
				json_object_get(val);
				json_object_object_add(json, key, val);
			}
			json_object_put(*value);
		} else {
			ln_dbgprintf(dag->ctx, "field name is '.', but json type is %s",
				json_type_to_name(json_object_get_type(*value)));
			json_object_object_add(json, prs->name, *value);
		}
	} else {
		json_object_object_add(json, prs->name, *value);
	}
	ln_dbgprintf(dag->ctx, "out field name '%s', json: %s", prs->name, json_object_to_json_string(json));
	r = 0;
	return r;
}

// TODO: streamline prototype when done with changes
static int
ln_normalizeRec(struct ln_pdag *dag,
	const char *const str,
	const size_t strLen,
	const size_t offs,
	const int bPartialMatch,
	size_t *const __restrict__ pParsedTo,
	struct json_object *json,
	struct ln_pdag **endNode);

static int
tryParser(struct ln_pdag *dag,
	const char *const str,
	const size_t strLen,
	size_t *offs,
	size_t *const __restrict__ pParsed,
	struct json_object **value,
	const ln_parser_t *const prs)
{
	int r;
	struct ln_pdag *endNode = NULL;
	if(prs->prsid == PRS_CUSTOM_TYPE) {
		if(*value == NULL)
			*value = json_object_new_object();
		ln_dbgprintf(dag->ctx, "calling custom parser '%s'", prs->custType->name);
		r = ln_normalizeRec(prs->custType->pdag, str, strLen, *offs, 1,
				    pParsed, *value, &endNode);
		*pParsed -= *offs;
		ln_dbgprintf(dag->ctx, "custom parser '%s' returns %d, pParsed %zu, json: %s", prs->custType->name, r, *pParsed, json_object_to_json_string(*value));
	} else {
		r = parser_lookup_table[prs->prsid].parser(dag->ctx, str, strLen,
			offs, prs->parser_data, pParsed, strcmp(prs->name, "-") ? value : NULL);
		ln_dbgprintf(dag->ctx, "parser lookup returns %d, pParsed %zu", r, *pParsed);
	}
	return r;
}
/**
 * Recursive step of the normalizer. It walks the parse dag and calls itself
 * recursively when this is appropriate. It also implements backtracking in
 * those (hopefully rare) cases where it is required.
 *
 * @param[in] dag current tree to process
 * @param[in] string string to be matched against (the to-be-normalized data)
 * @param[in] strLen length of the to-be-matched string
 * @param[in] offs start position in input data
 * @param[out] pPrasedTo ptr to position up to which the the parsing succed in max
 * @param[in/out] json ... that is being created during normalization
 * @param[out] endNode if a match was found, this is the matching node (undefined otherwise)
 *
 * @return number of characters left unparsed by following the subdag, negative if
 *         the to-be-parsed message is shorter than the rule sample by this number of
 *         characters.
 * TODO: can we use parameter block to prevent pushing params to the stack?
 */
static int
ln_normalizeRec(struct ln_pdag *dag,
	const char *const str,
	const size_t strLen,
	const size_t offs,
	const int bPartialMatch,
	size_t *const __restrict__ pParsedTo,
	struct json_object *json,
	struct ln_pdag **endNode)
{
	int r = LN_WRONGPARSER;
	int localR;
	size_t i;
	size_t iprs;
	size_t parsedTo = *pParsedTo;
	size_t parsed = 0;
	struct json_object *value;
	
ln_dbgprintf(dag->ctx, "%zu: enter parser, dag node %p, json %p", offs, dag, json);
// TODO: parser priorities are desperately needed --> rest

	/* now try the parsers */
	for(iprs = 0 ; iprs < dag->nparsers && r != 0 ; ++iprs) {
		const ln_parser_t *const prs = dag->parsers + iprs;
		if(dag->ctx->debug) {
			ln_dbgprintf(dag->ctx, "%zu/%d:trying '%s' parser for field '%s'",
					offs, bPartialMatch, parserName(prs->prsid), prs->name);
		}
		i = offs;
		value = NULL;
		localR = tryParser(dag, str, strLen, &i, &parsed, &value, prs);
		if(localR == 0) {
			parsedTo = i + parsed;
			/* potential hit, need to verify */
			ln_dbgprintf(dag->ctx, "%zu: potential hit, trying subtree %p", offs, prs->node);
			r = ln_normalizeRec(prs->node, str, strLen, parsedTo, bPartialMatch, &parsedTo, json, endNode);
			ln_dbgprintf(dag->ctx, "%zu: subtree returns %d, parsedTo %zu", offs, r, parsedTo);
			if(r == 0) {
				ln_dbgprintf(dag->ctx, "%zu: parser matches at %zu", offs, i);
				CHKR(fixJSON(dag, &value, json, prs));
			} else {
				ln_dbgprintf(dag->ctx, "%zu nonmatch, backtracking required, parsed to=%zu",
						offs, parsedTo);
				if (value != NULL) { /* Free the value if it was created */
					json_object_put(value);
				}
			}
		}
		/* did we have a longer parser --> then update */
		if(parsedTo > *pParsedTo)
			*pParsedTo = parsedTo;
		ln_dbgprintf(dag->ctx, "parsedTo %zu, *pParsedTo %zu", parsedTo, *pParsedTo);
	}

ln_dbgprintf(dag->ctx, "offs %zu, strLen %zu, isTerm %d", offs, strLen, dag->flags.isTerminal);
	if(dag->flags.isTerminal && (offs == strLen || bPartialMatch)) {
		*endNode = dag;
		r = 0;
		goto done;
	}

done:
	ln_dbgprintf(dag->ctx, "%zu returns %d, pParsedTo %zu, parsedTo %zu", offs, r, *pParsedTo, parsedTo);
	return r;
}


int
ln_normalize(ln_ctx ctx, const char *str, const size_t strLen, struct json_object **json_p)
{
	int r;
	struct ln_pdag *endNode = NULL;
	size_t parsedTo = 0;

	if(*json_p == NULL) {
		CHKN(*json_p = json_object_new_object());
	}

	r = ln_normalizeRec(ctx->pdag, str, strLen, 0, 0, &parsedTo, *json_p, &endNode);

	if(ctx->debug) {
		if(r == 0) {
			ln_dbgprintf(ctx, "final result for normalizer: parsedTo %zu, endNode %p, "
				     "isTerminal %d, tagbucket %p",
				     parsedTo, endNode, endNode->flags.isTerminal, endNode->tags);
		} else {
			ln_dbgprintf(ctx, "final result for normalizer: parsedTo %zu, endNode %p",
				     parsedTo, endNode);
		}
	}
	if(r == 0 && endNode->flags.isTerminal) {
		/* success, finalize event */
		if(endNode->tags != NULL) {
			/* add tags to an event */
			json_object_get(endNode->tags);
			json_object_object_add(*json_p, "event.tags", endNode->tags);
			CHKR(ln_annotate(ctx, *json_p, endNode->tags));
		}
		r = 0;
	} else {
		addUnparsedField(str, strLen, parsedTo, *json_p);
	}

done:	return r;
}
