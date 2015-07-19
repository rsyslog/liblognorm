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
static struct ln_parser_info parser_lookup_table[] = {
	{ "literal", ln_parseLiteral, NULL }, /* PRS_LITERAL */
	{ "date-rfc3164", ln_parseRFC3164Date, NULL },
	{ "date-rfc5424", ln_parseRFC5424Date, NULL },
	{ "number", ln_parseNumber, NULL },
	{ "float", ln_parseFloat, NULL },
	{ "hexnumber", ln_parseHexNumber, NULL },
	{ "kernel-timestamp", ln_parseKernelTimestamp, NULL },
	{ "whitespace", ln_parseWhitespace, NULL },
	{ "ipv4", ln_parseIPv4, NULL },
	{ "ipv6", ln_parseIPv6, NULL },
	{ "word", ln_parseWord, NULL },
	{ "alpha", ln_parseAlpha, NULL },
	{ "rest", ln_parseRest, NULL },
	{ "op-quoted-string", ln_parseOpQuotedString, NULL },
	{ "quoted-string", ln_parseQuotedString, NULL },
	{ "date-iso", ln_parseISODate, NULL },
	{ "time-24hr", ln_parseTime24hr, NULL },
	{ "time-12hr", ln_parseTime12hr, NULL },
	{ "duration", ln_parseDuration, NULL },
	{ "cisco-interface-spec", ln_parseCiscoInterfaceSpec, NULL },
	{ "name-value-list", ln_parseNameValue, NULL },
	{ "json", ln_parseJSON, NULL },
	{ "cee-syslog", ln_parseCEESyslog, NULL },
	{ "mac48", ln_parseMAC48, NULL },
	{ "cef", ln_parseCEF, NULL },
	{ "checkpoint-lea", ln_parseCheckpointLEA, NULL },
	{ "v2-iptables", ln_parsev2IPTables, NULL },
	{ "string-to", ln_parseStringTo, NULL },
	{ "char-to", ln_parseCharTo, NULL },
	{ "char-sep", ln_parseCharSeparated, NULL }
};

static inline const char *
parserName(const prsid_t id)
{
	return parser_lookup_table[id].name;
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


struct ln_pdag*
ln_newPDAG(ln_ctx ctx)
{
	struct ln_pdag *dag;

	if((dag = calloc(1, sizeof(struct ln_pdag))) == NULL)
		goto done;
	
	dag->ctx = ctx;
	ctx->nNodes++;
done:	return dag;
}

/* note: we must NOT free the parser itself, because
 * it is stored inside a parser table (so no single
 * alloc for the parser!).
 */
static void
pdagDeletePrs(ln_parser_t *const __restrict__ prs)
{
	// TODO: be careful here: once we move to real DAG from tree, we
	// cannot simply delete the next node! (refcount? something else?)
	if(prs->node != NULL)
		ln_pdagDelete(prs->node);
	free((void*)prs->name);
	if(prs->data != NULL)
		es_deleteStr(prs->data);
	if(prs->raw_data != NULL)
		es_deleteStr(prs->raw_data);
	// TODO: delete parser_data, but we need the real way of handling
	// it before doing so.
	if(prs->prsid == PRS_LITERAL) // quick and dirty for the current only case!
		free(prs->parser_data);
#if 0
	if(prs->parser_data != NULL && prs->parser_data_destructor != NULL)
		prs->parser_data_destructor(&(prs->parser_data));
#endif
	//free(prs);
}

void
ln_pdagDelete(struct ln_pdag *const __restrict__ pdag)
{
	if(pdag == NULL)
		goto done;

	if(pdag->tags != NULL)
		json_object_put(pdag->tags);

	for(int i = 0 ; i < pdag->nparsers ; ++i) {
		pdagDeletePrs(pdag->parsers+i);
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
		ln_dbgprintf(ctx, "opt path compact: add %p to %p\n", child_prs, prs);
		const size_t len = strlen((char*)prs->parser_data);
		const size_t child_len = strlen((char*)child_prs->parser_data);
		char *const newlit = realloc(prs->parser_data, len+child_len+1);
		CHKN(newlit);
		prs->parser_data = newlit;
		memcpy((char*)prs->parser_data+len, child_prs->parser_data, child_len+1);
		ln_pdag *const node_del = prs->node;
		prs->node = child_prs->node;

		child_prs->node = NULL; /* remove, else this would be destructed! */
		ln_pdagDelete(node_del);
	}
done:
	return r;
}

/**
 * Optimize the pdag.
 */
int
ln_pdagOptimize(ln_ctx ctx, struct ln_pdag *const dag)
{
	int r = 0;

	for(int i = 0 ; i < dag->nparsers ; ++i) {
		ln_parser_t *prs = dag->parsers+i;
		ln_dbgprintf(dag->ctx, "optimizing %p: field %d type '%s', name '%s': '%s':",
			prs->node, i, parserName(prs->prsid), prs->name, prs->parser_data);
		
		optLitPathCompact(ctx, prs);

		ln_pdagOptimize(ctx, prs->node);
	}
ln_dbgprintf(ctx, "---AFTER OPTIMIZATION------------------");
ln_displayPDAG(dag, 0);
ln_dbgprintf(ctx, "=======================================");
	return r;
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
int
ln_pdagAddParser(struct ln_pdag **pdag, ln_parser_t *parser)
{
	int r;
	struct ln_pdag *const dag = *pdag;

	ln_dbgprintf(dag->ctx, "pdag: %p, *pdag: %p, parser %p", pdag, *pdag, parser);
	/* check if we already have this parser, if so, merge
	 */
	int i;
	for(i = 0 ; i < dag->nparsers ; ++i) {
		if(   dag->parsers[i].prsid == parser->prsid
		   && !strcmp(dag->parsers[i].name, parser->name)) {
			// FIXME: work-around for literal parser with different
			//        literals (see header TODO)
			if(parser->prsid == PRS_LITERAL &&
			   ((char*)dag->parsers[i].parser_data)[0] != ((char*)parser->parser_data)[0])
			   	continue;
			*pdag = dag->parsers[i].node;
			r = 0;
			ln_dbgprintf(dag->ctx, "merging with dag %p", *pdag);
			pdagDeletePrs(parser); /* no need for data items */
			goto done;
		}
	}
	/* if we reach this point, we have a new parser type */
	CHKN(parser->node = ln_newPDAG(dag->ctx)); /* we need a new node */
	ln_parser_t *const newtab
		= realloc(dag->parsers, (dag->nparsers+1) * sizeof(ln_parser_t));
	CHKN(newtab);
	dag->parsers = newtab;
	memcpy(dag->parsers+dag->nparsers, parser, sizeof(ln_parser_t));
	dag->nparsers++;

	r = 0;
	ln_dbgprintf(dag->ctx, "prev subdag %p", dag);
	*pdag = parser->node;
	ln_dbgprintf(dag->ctx, "new subdag %p", *pdag);

done:
	free(parser);
	return r;
}


/* developer debug aid, to be used for example as follows:
   ln_dbgprintf(dag->ctx, "---------------------------------------");
   ln_displayPDAG(dag, 0);
   ln_dbgprintf(dag->ctx, "=======================================");
 */
void
ln_displayPDAG(struct ln_pdag *dag, int level)
{
	char indent[2048];

	if(level > 1023)
		level = 1023;
	memset(indent, ' ', level * 2);
	indent[level * 2] = '\0';

	ln_dbgprintf(dag->ctx, "%ssubDAG%s %p (children: %d parsers)",
		     indent, dag->flags.isTerminal ? " [TERM]" : "", dag, dag->nparsers);

	for(int i = 0 ; i < dag->nparsers ; ++i) {
		char *ed;
		if(dag->parsers->data == NULL)
			ed = strdup("");
		else
			ed = es_str2cstr(dag->parsers->data, " ");
		ln_dbgprintf(dag->ctx, "%sfield type '%s', name '%s': '%s', ed '%s':", indent,
			parserName(dag->parsers[i].prsid),
			dag->parsers[i].name,
			dag->parsers[i].parser_data, ed);
		free(ed);
		ln_displayPDAG(dag->parsers[i].node, level + 1);
	}
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
/**
 * recursive handler for DOT graph generator.
 */
static void
ln_genDotPDAGGraphRec(struct ln_pdag *dag, es_str_t **str)
{
	ln_dbgprintf(dag->ctx, "in dot: %p", dag);
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
			for(const char *p = (const char*) prs->parser_data ; *p ; ++p) {
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
	size_t *const __restrict__ pParsedTo,
	struct json_object *json,
	struct ln_pdag **endNode)
{
	int r = LN_WRONGPARSER;
	int localR;
	size_t i;
	size_t iprs;
	size_t parsedTo = *pParsedTo;
	char *cstr;
	size_t parsed = 0;
	struct json_object *value;
	
ln_dbgprintf(dag->ctx, "%zu: enter parser, dag node %p", offs, dag);
// TODO: parser priorities are desperately needed --> rest

	/* now try the parsers */
	for(iprs = 0 ; iprs < dag->nparsers ; ++iprs) {
		const ln_parser_t *const prs = dag->parsers + iprs;
		if(dag->ctx->debug) {
			ln_dbgprintf(dag->ctx, "%zu:trying '%s' parser for field '%s'",
					offs, parserName(prs->prsid), prs->name);
		}
		i = offs;
		value = NULL;
		localR = parser_lookup_table[prs->prsid].parser(str, strLen,
			&i, prs, &parsed, &value);
		ln_dbgprintf(dag->ctx, "parser returns %d, parsed %zu", localR, parsed);
		if(localR == 0) {
			parsedTo = i + parsed;
			/* potential hit, need to verify */
			ln_dbgprintf(dag->ctx, "%zu: potential hit, trying subtree %p", offs, prs->node);
			r = ln_normalizeRec(prs->node, str, strLen, parsedTo, &parsedTo, json, endNode);
			ln_dbgprintf(dag->ctx, "%zu: subtree returns %d", offs, r);
			if(r == 0 && (*endNode)->flags.isTerminal) {
				ln_dbgprintf(dag->ctx, "%zu: parser matches at %zu", offs, i);
				if(strcmp(prs->name, "-")) {
					/* Store the value here; create json if not already created */
					// TODO: ensure JSON is always created!
					if (value == NULL) { 
						CHKN(cstr = strndup(str + i, parsed));
						value = json_object_new_string(cstr);
						free(cstr);
					}
					if (value == NULL) {
						ln_dbgprintf(dag->ctx, "unable to create json");
						goto done;
					}
					json_object_object_add(json, prs->name, value);
				} else {
					if (value != NULL) {
						/* Free the unneeded value */
						json_object_put(value);
					}
				}
				r = 0;
				goto done;
			}
			ln_dbgprintf(dag->ctx, "%zu nonmatch, backtracking required, parsed to=%zu",
					offs, parsedTo);
			if (value != NULL) { /* Free the value if it was created */
				json_object_put(value);
			}
		}
		/* did we have a longer parser --> then update */
		if(parsedTo > *pParsedTo)
			*pParsedTo = parsedTo;
		ln_dbgprintf(dag->ctx, "parsedTo %zu, *pParsedTo %zu", parsedTo, *pParsedTo);
	}

ln_dbgprintf(dag->ctx, "offs %zu, strLen %zu", offs, strLen);
	if(offs == strLen) {
		*endNode = dag;
		r = 0;
		goto done;
	}

done:
	ln_dbgprintf(dag->ctx, "%zu returns %d", offs, r);
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

	r = ln_normalizeRec(ctx->pdag, str, strLen, 0, &parsedTo, *json_p, &endNode);

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
