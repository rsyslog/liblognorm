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
	{ "literal", NULL, NULL }, /* PRS_LITERAL */
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

#if 0
void
ln_deletePDAGNode(ln_parser_t *node)
{
	ln_deletePDAG(node->node);
	es_deleteStr(node->name);
	if(node->data != NULL)
		es_deleteStr(node->data);
	if(node->raw_data != NULL)
		es_deleteStr(node->raw_data);
	if(node->parser_data != NULL && node->parser_data_destructor != NULL)
		node->parser_data_destructor(&(node->parser_data));
	free(node);
}
#endif

void
ln_deletePDAG(struct ln_pdag *dag)
{
	if(dag == NULL)
		goto done;

	if(dag->tags != NULL)
		json_object_put(dag->tags);
#if 0
	ln_fieldList_t *node, *nextnode;
	size_t i;

	for(node = dag->froot; node != NULL; node = nextnode) {
		nextnode = node->next;
		ln_deletePDAGNode(node);
	}

	/* need to free a large prefix buffer? */
	if(dag->lenPrefix > sizeof(tree->prefix))
		free(dag->prefix.ptr);

	for(i = 0 ; i < 256 ; ++i)
		if(dag->subtree[i] != NULL)
			ln_deletePDAG(dag->subtree[i]);
#endif
	free(dag);

done:	return;
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
		if(dag->parsers[i].prsid == parser->prsid) {
			// FIXME: work-around for literal parser with different
			//        literals (see header TODO)
			if(parser->prsid == PRS_LITERAL &&
			   ((char*)dag->parsers[i].parser_data)[0] != ((char*)parser->parser_data)[0])
			   	continue;
			*pdag = dag->parsers[i].node;
			r = 0;
			ln_dbgprintf(dag->ctx, "merging with dag %p", *pdag);
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

done:	return r;
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

	ln_dbgprintf(dag->ctx, "%ssubtree%s %p (children: %d parsers)",
		     indent, dag->flags.isTerminal ? " TERM" : "", dag, dag->nparsers);

	/* display parser subdags */
	for(int i = 0 ; i < dag->nparsers ; ++i) {
		//char *cstr;
		//cstr = es_str2cstr(node->name, NULL);
		ln_dbgprintf(dag->ctx, "%sfield type %s: '%s':", indent,
			parser_lookup_table[dag->parsers[i].prsid].name,
			dag->parsers[i].parser_data);
		//free(cstr);
		ln_displayPDAG(dag->parsers[i].node, level + 1);
	}
}


#if 0
/* the following is a quick hack, which should be moved to the
 * string class.
 */
static inline void dotAddPtr(es_str_t **str, void *p
{
	char buf[64];
	int i;
	i = snprintf(buf, sizeof(buf), "%p", p);
	es_addBuf(str, buf, i);
}
/**
 * recursive handler for DOT graph generator.
 */
static void
ln_genDotPDAGGraphRec(struct ln_pdag *dag, es_str_t **str)
{
	int i;
	ln_fieldList_t *node;


	dotAddPtr(str, dag);
	es_addBufConstcstr(str, " [label=\"");
	if(dag->lenPrefix > 0) {
		es_addChar(str, '\'');
		es_addBuf(str, (char*) prefixBase(dag), tree->lenPrefix);
		es_addChar(str, '\'');
	}
	es_addBufConstcstr(str, "\"");
	if(isLeaf(dag)) {
		es_addBufConstcstr(str, " style=\"bold\"");
	}
	es_addBufConstcstr(str, "]\n");

	/* display char subdags */
	for(i = 0 ; i < 256 ; ++i) {
		if(dag->subtree[i] != NULL) {
			dotAddPtr(str, dag);
			es_addBufConstcstr(str, " -> ");
			dotAddPtr(str, dag->subtree[i]);
			es_addBufConstcstr(str, " [label=\"");
			es_addChar(str, (char) i);
			es_addBufConstcstr(str, "\"]\n");
			ln_genDotPDAGGraphRec(dag->subtree[i], str);
		}
	}

	/* display field subdags */
	for(node = dag->froot ; node != NULL ; node = node->next ) {
		dotAddPtr(str, dag);
		es_addBufConstcstr(str, " -> ");
		dotAddPtr(str, node->subdag);
		es_addBufConstcstr(str, " [label=\"");
		es_addStr(str, node->name);
		es_addBufConstcstr(str, "\" style=\"dotted\"]\n");
		ln_genDotPDAGGraphRec(node->subdag, str);
	}
}


void
ln_genDotPDAGGraph(struct ln_pdag *dag, es_str_t **str)
{
	es_addBufConstcstr(str, "digraph pdag {\n");
	ln_genDotPDAGGraphRec(dag, str);
	es_addBufConstcstr(str, "}\n");
}
#endif


#if 0
/**
 * add unparsed string to event.
 */
static inline int
addUnparsedField(const char *str, size_t strLen, int offs, struct json_object *json)
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
#endif


/**
 * Recursive step of the normalizer. It walks the parse dag and calls itself
 * recursively when this is appropriate. It also implements backtracking in
 * those (hopefully rare) cases where it is required.
 *
 * @param[in] dag current tree to process
 * @param[in] string string to be matched against (the to-be-normalized data)
 * @param[in] strLen length of the to-be-matched string
 * @param[in] offs start position in input data
 * @param[in/out] json ... that is being created during normalization
 * @param[out] endNode if a match was found, this is the matching node (undefined otherwise)
 *
 * @return number of characters left unparsed by following the subdag, negative if
 *         the to-be-parsed message is shorter than the rule sample by this number of
 *         characters.
 */
#if 0
static int
ln_normalizeRec(struct ln_pdag *dag, const char *str, size_t strLen, size_t offs, struct json_object *json,
		struct ln_pdag **endNode)
{
	int r;
	int localR;
	size_t i;
	int left;
	ln_fieldList_t *node;
	ln_fieldList_t *restMotifNode = NULL;
	char *cstr;
	const char *c;
	unsigned char *cpfix;
	unsigned ipfix;
	size_t parsed;
	char *namestr;
	struct json_object *value;
	
	if(offs >= strLen) {
		*endNode = dag;
		r = -dag->lenPrefix;
		goto done;
	}

ln_dbgprintf(dag->ctx, "%zu: enter parser, tree %p", offs, tree);
	c = str;
	cpfix = prefixBase(dag);
	node = dag->froot;
	r = strLen - offs;
	/* first we need to check if the common prefix matches (and consume input data while we do) */
	ipfix = 0;
	while(offs < strLen && ipfix < dag->lenPrefix) {
		ln_dbgprintf(dag->ctx, "%zu: prefix compare '%c', '%c'", offs, c[offs], cpfix[ipfix]);
		if(c[offs] != cpfix[ipfix]) {
			r -= ipfix;
			goto done;
		}
		++offs, ++ipfix;
	}
	
	if(ipfix != dag->lenPrefix) {
		/* incomplete prefix match --> to-be-normalized string too short */
		r = ipfix - dag->lenPrefix;
		goto done;
	}

	r -= ipfix;
	ln_dbgprintf(dag->ctx, "%zu: prefix compare succeeded, still valid", offs);

	/* now try the parsers */
	while(node != NULL) {
		if(dag->ctx->debug) {
			cstr = es_str2cstr(node->name, NULL);
			ln_dbgprintf(dag->ctx, "%zu:trying parser for field '%s': %p",
					offs, cstr, node->parser);
			free(cstr);
		}
		i = offs;
		if(node->isIPTables) {
			localR = ln_iptablesParser(dag, str, strLen, &i, json);
			ln_dbgprintf(dag->ctx, "%zu iptables parser return, i=%zu",
						offs, i);
			if(localR == 0) {
				/* potential hit, need to verify */
				ln_dbgprintf(dag->ctx, "potential hit, trying subtree");
				left = ln_normalizeRec(node->subdag, str, strLen, i, json, endNode);
				if(left == 0 && (*endNode)->flags.isTerminal) {
					ln_dbgprintf(dag->ctx, "%zu: parser matches at %zu", offs, i);
					r = 0;
					goto done;
				}
				ln_dbgprintf(dag->ctx, "%zu nonmatch, backtracking required, left=%d",
						offs, left);
				if(left < r)
					r = left;
			}
		} else if(node->parser == ln_parseRest) {
			/* This is a quick and dirty adjustment to handle "rest" more intelligently.
			 * It's just a tactical fix: in the longer term, we'll handle the whole
			 * situation differently. However, it makes sense to fix this now, as this
			 * solves some real-world problems immediately. -- rgerhards, 2015-04-15
			 */
			restMotifNode = node;
		} else {
			value = NULL;
			localR = node->parser(str, strLen, &i, node, &parsed, &value);
			ln_dbgprintf(dag->ctx, "parser returns %d, parsed %zu", localR, parsed);
			if(localR == 0) {
				/* potential hit, need to verify */
				ln_dbgprintf(dag->ctx, "%zu: potential hit, trying subtree %p", offs, node->subtree);
				left = ln_normalizeRec(node->subdag, str, strLen, i + parsed, json, endNode);
				ln_dbgprintf(dag->ctx, "%zu: subtree returns %d", offs, r);
				if(left == 0 && (*endNode)->flags.isTerminal) {
					ln_dbgprintf(dag->ctx, "%zu: parser matches at %zu", offs, i);
					if(es_strbufcmp(node->name, (unsigned char*)"-", 1)) {
						/* Store the value here; create json if not already created */
						if (value == NULL) { 
							CHKN(cstr = strndup(str + i, parsed));
							value = json_object_new_string(cstr);
							free(cstr);
						}
						if (value == NULL) {
							ln_dbgprintf(dag->ctx, "unable to create json");
							goto done;
						}
						namestr = ln_es_str2cstr(&node->name);
						json_object_object_add(json, namestr, value);
					} else {
						if (value != NULL) {
							/* Free the unneeded value */
							json_object_put(value);
						}
					}
					r = 0;
					goto done;
				}
				ln_dbgprintf(dag->ctx, "%zu nonmatch, backtracking required, left=%d",
						offs, left);
				if (value != NULL) {
					/* Free the value if it was created */
					json_object_put(value);
				}
				if(left > 0 && left < r)
					r = left;
				ln_dbgprintf(dag->ctx, "%zu nonmatch, backtracking required, left=%d, r now %d", offs, left, r);
			}
		}
		node = node->next;
	}

	if(offs == strLen) {
		*endNode = dag;
		r = 0;
		goto done;
	}

if(offs < strLen) {
unsigned char cc = str[offs];
ln_dbgprintf(dag->ctx, "%zu no field, trying subtree char '%c': %p", offs, cc, tree->subtree[cc]);
} else {
ln_dbgprintf(dag->ctx, "%zu no field, offset already beyond end", offs);
}
	/* now let's see if we have a literal */
	if(dag->subtree[(unsigned char)str[offs]] != NULL) {
		left = ln_normalizeRec(dag->subtree[(unsigned char)str[offs]],
				       str, strLen, offs + 1, json, endNode);
ln_dbgprintf(dag->ctx, "%zu got left %d, r %d", offs, left, r);
		if(left < r)
			r = left;
ln_dbgprintf(dag->ctx, "%zu got return %d", offs, r);
	}

	if(r == 0 && (*endNode)->flags.isTerminal)
		goto done;

	/* and finally give "rest" a try if it was present. Note that we MUST do this after
	 * literal evaluation, otherwise "rest" can never be overriden by other rules.
	 */
	if(restMotifNode != NULL) {
		ln_dbgprintf(dag->ctx, "rule has rest motif, forcing match via it\n");
		value = NULL;
		restMotifNode->parser(str, strLen, &i, restMotifNode, &parsed, &value);
#		ifndef NDEBUG
		left = /* we only need this for the assert below */
#		endif
		       ln_normalizeRec(restMotifNode->subdag, str, strLen, i + parsed, json, endNode);
		assert(left == 0); /* with rest, we have this invariant */
		assert((*endNode)->flags.isTerminal); /* this one also */
		ln_dbgprintf(dag->ctx, "%zu: parser matches at %zu", offs, i);
		if(es_strbufcmp(restMotifNode->name, (unsigned char*)"-", 1)) {
			/* Store the value here; create json if not already created */
			if (value == NULL) { 
				CHKN(cstr = strndup(str + i, parsed));
				value = json_object_new_string(cstr);
				free(cstr);
			}
			if (value == NULL) {
				ln_dbgprintf(dag->ctx, "unable to create json");
				goto done;
			}
			namestr = ln_es_str2cstr(&restMotifNode->name);
			json_object_object_add(json, namestr, value);
		} else {
			if (value != NULL) {
				/* Free the unneeded value */
				json_object_put(value);
			}
		}
		r = 0;
		goto done;
	}
done:
	ln_dbgprintf(dag->ctx, "%zu returns %d", offs, r);
	return r;
}


int
ln_normalize(ln_ctx ctx, const char *str, size_t strLen, struct json_object **json_p)
{
	int r;
	int left;
	struct ln_pdag *endNode = NULL;

	if(*json_p == NULL) {
		CHKN(*json_p = json_object_new_object());
	}

	left = ln_normalizeRec(ctx->pdag, str, strLen, 0, *json_p, &endNode);

	if(ctx->debug) {
		if(left == 0) {
			ln_dbgprintf(ctx, "final result for normalizer: left %d, endNode %p, "
				     "isTerminal %d, tagbucket %p",
				     left, endNode, endNode->flags.isTerminal, endNode->tags);
		} else {
			ln_dbgprintf(ctx, "final result for normalizer: left %d, endNode %p",
				     left, endNode);
		}
	}
	if(left != 0 || !endNode->flags.isTerminal) {
		/* we could not successfully parse, some unparsed items left */
		if(left < 0) {
			addUnparsedField(str, strLen, strLen, *json_p);
		} else {
			addUnparsedField(str, strLen, strLen - left, *json_p);
		}
	} else {
		/* success, finalize event */
		if(endNode->tags != NULL) {
			/* add tags to an event */
			json_object_get(endNode->tags);
			json_object_object_add(*json_p, "event.tags", endNode->tags);
			CHKR(ln_annotate(ctx, *json_p, endNode->tags));
		}
	}

	r = 0;

done:	return r;
}
#endif
