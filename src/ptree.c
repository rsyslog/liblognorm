/**
 * @file ptree.c
 * @brief Implementation of the parse tree object.
 * @class ln_ptree ptree.h
 *//*
 * Copyright 2010 by Rainer Gerhards and Adiscon GmbH.
 *
 * This file is part of liblognorm.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * A copy of the LGPL v2.1 can be found in the file "COPYING" in this distribution.
 */
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <libestr.h>

#include "liblognorm.h"
#include "lognorm.h"
#include "samp.h"
#include "ptree.h"
#include "annot.h"
#include "internal.h"

/**
 * Get base addr of common prefix. Takes length of prefix in account
 * and selects the right buffer.
 */
static inline unsigned char*
prefixBase(struct ln_ptree *tree)
{
	return (tree->lenPrefix <= sizeof(tree->prefix))
	       ? tree->prefix.data : tree->prefix.ptr;
}


struct ln_ptree*
ln_newPTree(ln_ctx ctx, struct ln_ptree **parentptr)
{
	struct ln_ptree *tree;

	if((tree = calloc(1, sizeof(struct ln_ptree))) == NULL)
		goto done;
	
	tree->parentptr = parentptr;
	tree->ctx = ctx;
	ctx->nNodes++;
done:	return tree;
}


void
ln_deletePTree(struct ln_ptree *tree)
{
	ln_fieldList_t *node, *nodeDel;
	es_size_t i;

	if(tree == NULL)
		goto done;

	if(tree->tags != NULL)
		ee_deleteTagbucket(tree->tags);
	for(node = tree->froot ; node != NULL ; ) {
		ln_deletePTree(node->subtree);
		nodeDel = node;
		es_deleteStr(node->name);
		if(node->data != NULL)
			es_deleteStr(node->data);
		node = node->next;
		free(nodeDel);
	}

	/* need to free a large prefix buffer? */
	if(tree->lenPrefix > sizeof(tree->prefix))
		free(tree->prefix.ptr);

	for(i = 0 ; i < 256 ; ++i)
		if(tree->subtree[i] != NULL)
			ln_deletePTree(tree->subtree[i]);
	free(tree);

done:	return;
}


struct ln_ptree*
ln_traversePTree(struct ln_ptree *subtree, es_str_t *str, es_size_t *parsedTo)
{
	es_size_t i = 0;
	unsigned char *c;
	struct ln_ptree *curr = subtree;
	struct ln_ptree *prev = NULL;

	ln_dbgprintf(subtree->ctx, "traversePTree: begin at %p", curr);
	c = es_getBufAddr(str);
	while(curr != NULL && i < es_strlen(str)) {
		// TODO: implement commonPrefix
		ln_dbgprintf(subtree->ctx, "traversePTree: curr %p, char '%u'", curr, c[i]);
		prev = curr;
		curr = curr->subtree[c[i++]];
	};
	ln_dbgprintf(subtree->ctx, "traversePTree: after search %p", curr);

	if(curr == NULL) {
		curr = prev;
	}

	if(i == es_strlen(str))
		--i;

	*parsedTo = i;
	ln_dbgprintf(subtree->ctx, "traversePTree: returns node %p, offset %u", curr, (unsigned) i);
	return curr;
}



/**
 * Set the common prefix inside a note, taking into account the subtle
 * issues associated with it.
 * @return 0 on success, something else otherwise
 */
static int
setPrefix(struct ln_ptree *tree, unsigned char *buf, es_size_t lenBuf, es_size_t offs)
{
	int r;
ln_dbgprintf(tree->ctx, "setPrefix lenBuf %u, offs %d", lenBuf, offs); 
	tree->lenPrefix = lenBuf - offs;
	if(tree->lenPrefix > sizeof(tree->prefix)) {
		/* too-large for standard buffer, need to alloc one */
		if((tree->prefix.ptr = malloc(tree->lenPrefix * sizeof(unsigned char))) == NULL) {
			r = LN_NOMEM;
			goto done; /* fail! */
		}
		memcpy(tree->prefix.ptr, buf, tree->lenPrefix);
	} else {
		/* note: r->lenPrefix may be 0, but that is OK */
		memcpy(tree->prefix.data, buf, tree->lenPrefix);
	}
	r = 0;

done:	return r;
}


/**
 * Check if the provided tree is a leaf. This means that it
 * does not contain any subtrees.
 * @return 1 if it is a leaf, 0 otherwise
 */
static inline int
isLeaf(struct ln_ptree *tree)
{
	int r = 0;
	int i;

	if(tree->froot != NULL)
		goto done;
	
	for(i = 0 ; i < 256 ; ++i) {
		if(tree->subtree[i] != NULL)
			goto done;
	}
	r = 1;

done:	return r;
}


/**
 * Check if the provided tree is a true leaf. This means that it
 * does not contain any subtrees of any kind and no prefix.
 * @return 1 if it is a leaf, 0 otherwise
 */
static inline int
isTrueLeaf(struct ln_ptree *tree)
{
	return((tree->lenPrefix == 0) && isLeaf(tree));
}


struct ln_ptree *
ln_addPTree(struct ln_ptree *tree, es_str_t *str, es_size_t offs)
{
	struct ln_ptree *r;
	struct ln_ptree **parentptr;	 /**< pointer in parent that needs to be updated */

ln_dbgprintf(tree->ctx, "addPTree: offs %u", offs);
	parentptr = &(tree->subtree[es_getBufAddr(str)[offs]]);
	/* First check if tree node is totaly empty. If so, we can simply add
	 * the prefix to this node. This case is important, because it happens
	 * every time with a new field.
	 */
	if(isTrueLeaf(tree)) {
		if(setPrefix(tree, es_getBufAddr(str), es_strlen(str), offs) != 0) {
			r = NULL;
		} else {
			r = tree;
		}
		goto done;
	}

	if(tree->ctx->debug) {
		char * cstr = es_str2cstr(str, NULL);
		ln_dbgprintf(tree->ctx, "addPTree: add '%s', offs %u, tree %p",
			     cstr+offs, (unsigned) offs, tree);
		free(cstr);
	}

	if((r = ln_newPTree(tree->ctx, parentptr)) == NULL)
		goto done;

	if(setPrefix(r, es_getBufAddr(str) + offs + 1, es_strlen(str) - offs - 1, 0) != 0) {
		free(r);
		r = NULL;
		goto done;
	}

	*parentptr = r;

done:	return r;
}


/**
 * Split the provided tree (node) into two at the provided index into its
 * common prefix. This function exists to support splitting nodes when
 * a mismatch in the common prefix requires that. This function more or less
 * keeps the tree as it is, just changes the structure. No new node is added.
 * Usually, it is desired to add a new node. This must be made afterwards.
 * Note that we need to create a new tree *in front of* the current one, as 
 * the current one contains field etc. subtree pointers.
 * @param[in] tree tree to split
 * @param[in] offs offset into common prefix (must be less than prefix length!)
 */
static inline struct ln_ptree*
splitTree(struct ln_ptree *tree, unsigned short offs)
{
	unsigned char *c;
	struct ln_ptree *r;
	unsigned short newlen;
	ln_ptree **newparentptr;	 /**< pointer in parent that needs to be updated */

	assert(offs < tree->lenPrefix);
	if((r = ln_newPTree(tree->ctx, tree->parentptr)) == NULL)
		goto done;

	ln_dbgprintf(tree->ctx, "splitTree %p at offs %u", tree, offs);
	/* note: the overall prefix is reduced by one char, which is now taken
	 * care of inside the "branch table".
	 */
	c = prefixBase(tree);
//ln_dbgprintf(tree->ctx, "splitTree new bb, *(c+offs): '%s'", c);
	if(setPrefix(r, c, offs, 0) != 0) {
		ln_deletePTree(r);
		r = NULL;
		goto done; /* fail! */
	}

ln_dbgprintf(tree->ctx, "splitTree new tree %p lenPrefix=%u, char '%c'", r, r->lenPrefix, r->prefix.data[0]);
	/* add the proper branch table entry for the new node. must be done
	 * here, because the next step will destroy the required index char!
	 */
	newparentptr = &(r->subtree[c[offs]]);
	r->subtree[c[offs]] = tree;

	/* finally fix existing common prefix */
	newlen = tree->lenPrefix - offs - 1;
	if(tree->lenPrefix > sizeof(tree->prefix) && (newlen <= sizeof(tree->prefix))) {
		/* note: c is a different pointer; the original
		 * pointer is overwritten by memcpy! */
ln_dbgprintf(tree->ctx, "splitTree new case one bb, offs %u, lenPrefix %u, newlen %u", offs, tree->lenPrefix, newlen);
//ln_dbgprintf(tree->ctx, "splitTree new case one bb, *(c+offs): '%s'", c);
		memcpy(tree->prefix.data, c+offs+1, newlen);
		free(c);
	} else {
ln_dbgprintf(tree->ctx, "splitTree new case two bb, offs=%u, newlen %u", offs, newlen);
		memmove(c, c+offs+1, newlen);
	}
	tree->lenPrefix = tree->lenPrefix - offs - 1;

	if(tree->parentptr == 0)
		tree->ctx->ptree = r;	/* root does not have a parent! */
	else
		*(tree->parentptr) = r;
	tree->parentptr = newparentptr;

done:	return r;
}


struct ln_ptree *
ln_buildPTree(struct ln_ptree *tree, es_str_t *str, es_size_t offs)
{
	struct ln_ptree *r;
	unsigned char *c;
	unsigned char *cpfix;
	es_size_t i;
	unsigned short ipfix;

	assert(tree != NULL);
	ln_dbgprintf(tree->ctx, "buildPTree: begin at %p, offs %u", tree, offs);
	c = es_getBufAddr(str);

	/* check if the prefix matches and, if not, at what offset it is different */
	ipfix = 0;
	cpfix = prefixBase(tree);
	for(  i = offs
	    ; (i < es_strlen(str)) && (ipfix < tree->lenPrefix) && (c[i] == cpfix[ipfix])
	    ; ++i, ++ipfix) {
	    	; /*DO NOTHING - just find end of match */
		ln_dbgprintf(tree->ctx, "buildPTree: tree %p, i %d, char '%c'", tree, (int)i, c[i]);
	}

	/* if we reach this point, we have processed as much of the common prefix
	 * as we could. The following code now does the proper actions based on
	 * the possible cases.
	 */
	if(i == es_strlen(str)) {
		/* all of our input is consumed, no more recursion */
		if(ipfix == tree->lenPrefix) {
			ln_dbgprintf(tree->ctx, "case 1.1");
			/* exact match, we are done! */
			r = tree;
		} else {
			ln_dbgprintf(tree->ctx, "case 1.2");
			/* we need to split the node at the current position */
			r = splitTree(tree, ipfix);
		}
	} else if(ipfix < tree->lenPrefix) {
			ln_dbgprintf(tree->ctx, "case 2, i=%u, ipfix=%u", i, ipfix);
			/* we need to split the node at the current position */
			if((r = splitTree(tree, ipfix)) == NULL)
				goto done; /* fail */
ln_dbgprintf(tree->ctx, "pre addPTree: i %u", i);
			if((r = ln_addPTree(r, str, i)) == NULL)
				goto done;
			//r = ln_buildPTree(r, str, i + 1);
	} else {
		/* we could consume the current common prefix, but now need
		 * to traverse the rest of the tree based on the next char.
		 */
		if(tree->subtree[c[i]] == NULL) {
			ln_dbgprintf(tree->ctx, "case 3.1");
			/* non-match, need new subtree */
			r = ln_addPTree(tree, str, i);
		} else {
			ln_dbgprintf(tree->ctx, "case 3.2");
			/* match, follow subtree */
			r = ln_buildPTree(tree->subtree[c[i]], str, i + 1);
		}
	}

//ln_dbgprintf(tree->ctx, "---------------------------------------");
//ln_displayPTree(tree, 0);
//ln_dbgprintf(tree->ctx, "=======================================");
done:	return r;
}


int 
ln_addFDescrToPTree(struct ln_ptree **tree, ln_fieldList_t *node)
{
	int r;
	ln_fieldList_t *curr;

	assert(tree != NULL);assert(*tree != NULL);
	assert(node != NULL);

	if((node->subtree = ln_newPTree((*tree)->ctx, &node->subtree)) == NULL) {
		r = -1;
		goto done;
	}
	ln_dbgprintf((*tree)->ctx, "got new subtree %p", node->subtree);

	/* check if we already have this field, if so, merge
	 * TODO: optimized, check logic
	 */
	for(curr = (*tree)->froot ; curr != NULL ; curr = curr->next) {
		if(!es_strcmp(curr->name, node->name)) {
			*tree = curr->subtree;
			r = 0;
			ln_dbgprintf((*tree)->ctx, "merging with tree %p\n", *tree);
			goto done;
		}
	}

	if((*tree)->froot == NULL) {
		(*tree)->froot = (*tree)->ftail = node;
	} else {
		(*tree)->ftail->next = node;
		(*tree)->ftail = node;
	}
	r = 0;
	ln_dbgprintf((*tree)->ctx, "prev subtree %p", *tree);
	*tree = node->subtree;
	ln_dbgprintf((*tree)->ctx, "new subtree %p", *tree);

done:	return r;
}


void
ln_displayPTree(struct ln_ptree *tree, int level)
{
	int i;
	int nChildLit;
	int nChildField;
	es_str_t *str;
	char *cstr;
	ln_fieldList_t *node;
	char indent[2048];

	if(level > 1023)
		level = 1023;
	memset(indent, ' ', level * 2);
	indent[level * 2] = '\0';

	nChildField = 0;
	for(node = tree->froot ; node != NULL ; node = node->next ) {
		++nChildField;
	}

	nChildLit = 0;
	for(i = 0 ; i < 256 ; ++i) {
		if(tree->subtree[i] != NULL) {
			nChildLit++;
		}
	}

	str = es_newStr(sizeof(tree->prefix));
	es_addBuf(&str, (char*) prefixBase(tree), tree->lenPrefix);
	cstr = es_str2cstr(str, NULL);
	es_deleteStr(str);
	ln_dbgprintf(tree->ctx, "%ssubtree%s %p (prefix: '%s', children: %d literals, %d fields)",
		     indent, tree->flags.isTerminal ? " TERM" : "", tree, cstr, nChildLit, nChildField);
	free(cstr);
	/* display char subtrees */
	for(i = 0 ; i < 256 ; ++i) {
		if(tree->subtree[i] != NULL) {
			ln_dbgprintf(tree->ctx, "%schar %2.2x(%c):", indent, i, i);
			ln_displayPTree(tree->subtree[i], level + 1);
		}
	}

	/* display field subtrees */
	for(node = tree->froot ; node != NULL ; node = node->next ) {
		cstr = es_str2cstr(node->name, NULL);
		ln_dbgprintf(tree->ctx, "%sfield %s:", indent, cstr);
		free(cstr);
		ln_displayPTree(node->subtree, level + 1);
	}
}


/* the following is a quick hack, which should be moved to the
 * string class.
 */
static inline void dotAddPtr(es_str_t **str, void *p)
{
	char buf[64];
	int i;
	i = snprintf(buf, sizeof(buf), "%llu", (unsigned long long) p);
	es_addBuf(str, buf, i);
}
/**
 * recursive handler for DOT graph generator.
 */
static void
ln_genDotPTreeGraphRec(struct ln_ptree *tree, es_str_t **str)
{
	int i;
	ln_fieldList_t *node;


	dotAddPtr(str, tree);
	es_addBufConstcstr(str, " [label=\"");
	if(tree->lenPrefix > 0) {
		es_addChar(str, '\'');
		es_addBuf(str, (char*) prefixBase(tree), tree->lenPrefix);
		es_addChar(str, '\'');
	}
	es_addBufConstcstr(str, "\"");
	if(isLeaf(tree)) {
		es_addBufConstcstr(str, " style=\"bold\"");
	}
	es_addBufConstcstr(str, "]\n");

	/* display char subtrees */
	for(i = 0 ; i < 256 ; ++i) {
		if(tree->subtree[i] != NULL) {
			dotAddPtr(str, tree);
			es_addBufConstcstr(str, " -> ");
			dotAddPtr(str, tree->subtree[i]);
			es_addBufConstcstr(str, " [label=\"");
			es_addChar(str, (char) i);
			es_addBufConstcstr(str, "\"]\n");
			ln_genDotPTreeGraphRec(tree->subtree[i], str);
		}
	}

	/* display field subtrees */
	for(node = tree->froot ; node != NULL ; node = node->next ) {
		dotAddPtr(str, tree);
		es_addBufConstcstr(str, " -> ");
		dotAddPtr(str, node->subtree);
		es_addBufConstcstr(str, " [label=\"");
		es_addStr(str, node->name);
		es_addBufConstcstr(str, "\" style=\"dotted\"]\n");
		ln_genDotPTreeGraphRec(node->subtree, str);
	}
}


void
ln_genDotPTreeGraph(struct ln_ptree *tree, es_str_t **str)
{
	es_addBufConstcstr(str, "digraph ptree {\n");
	ln_genDotPTreeGraphRec(tree, str);
	es_addBufConstcstr(str, "}\n");
}


/* TODO: Move to a better location? */

static inline int
addField(ln_ctx ctx, struct ee_event **event, es_str_t *name, struct ee_value *value)
{
	int r;
	struct ee_field *field;

	if(*event == NULL) {
		CHKN(*event = ee_newEvent(ctx->eectx));
	}

	CHKN(field = ee_newField(ctx->eectx));
	CHKR(ee_nameField(field, name));
	CHKR(ee_addValueToField(field, value));
	CHKR(ee_addFieldToEvent(*event, field));
	r = 0;

done:	return r;
}


/**
 * add unparsed string to event.
 */
static inline int
addUnparsedField(ln_ctx ctx, es_str_t *str, es_size_t offs, struct ee_event **event)
{
	struct ee_value *value;
	es_str_t *namestr;
	es_str_t *valstr;
	int r;

	CHKN(value = ee_newValue(ctx->eectx));
	CHKN(namestr = es_newStrFromCStr("originalmsg", sizeof("originalmsg") - 1));
	CHKN(valstr = es_strdup(str));
	ee_setStrValue(value, valstr);
	addField(ctx, event, namestr, value);
	es_deleteStr(namestr);

	CHKN(value = ee_newValue(ctx->eectx));
	CHKN(namestr = es_newStrFromCStr("unparsed-data", sizeof("unparsed-data") - 1));
	CHKN(valstr = es_newStrFromSubStr(str, offs, es_strlen(str) - offs));
	ee_setStrValue(value, valstr);
	addField(ctx, event, namestr, value);
	es_deleteStr(namestr);
	r = 0;
done:	return r;
}


/**
 * Special parser for iptables-like name/value pairs.
 * The pull multiple fields. Note that once this parser has been selected,
 * it is very unlikely to be left, as it is *very* generic. This parser is
 * required because practice shows that already-structured data like iptables
 * can otherwise not be processed by liblognorm in a meaningful way.
 *
 * @param[in] tree current tree to process
 * @param[in] string string to be matched against (the to-be-normalized data)
 * @param[in/out] offs start position in input data, on exit first unparsed position
 * @param[in/out] event handle to event that is being created during normalization
 *
 * @return 0 if parser was successfully, something else on error
 */
static int
ln_iptablesParser(struct ln_ptree *tree, es_str_t *str, es_size_t *offs,
		  struct ee_event **event)
{
	int r;
	es_size_t o = *offs;
	es_str_t *fname;
	es_str_t *fval;
	struct ee_value *value;
	unsigned char *pstr;
	unsigned char *end;

ln_dbgprintf(tree->ctx, "%d enter iptable parser, len %d", (int) *offs, (int) es_strlen(str));
	if(o == es_strlen(str)) {
		r = -1; /* can not be, we have no n/v pairs! */
		goto done;
	}
	
	end = es_getBufAddr(str) + es_strlen(str);
	pstr = es_getBufAddr(str) + o;
	while(pstr < end) {
		while(isspace(*pstr))
			++pstr;
		fname = es_newStr(16);
		while(!isspace(*pstr) && *pstr != '=') {
			es_addChar(&fname, *pstr);
			++pstr;
		}
		if(*pstr == '=') {
			fval = es_newStr(16);
			++pstr;
			/* error on space */
			while(!isspace(*pstr) && pstr < end) {
				es_addChar(&fval, *pstr);
				++pstr;
			}
		} else {
			fval = es_newStrFromCStr("[*PRESENT*]", sizeof("[*PRESENT*]")-1);
		}
		char *cn, *cv;
		cn = es_str2cstr(fname, NULL);
		cv = es_str2cstr(fval, NULL);
ln_dbgprintf(tree->ctx, "iptable parser extracts %s=%s", cn, cv);
		value = ee_newValue(tree->ctx->eectx);
		ee_setStrValue(value, fval);
		CHKR(addField(tree->ctx, event, fname, value));
	}

	r = 0;
	*offs = es_strlen(str);

done:
	ln_dbgprintf(tree->ctx, "%d iptable parser returns %d", (int) *offs, (int) r);
	return r;
}


/**
 * Recursive step of the normalizer. It walks the parse tree and calls itself
 * recursively when this is appropriate. It also implements backtracking in
 * those (hopefully rare) cases where it is required.
 *
 * @param[in] tree current tree to process
 * @param[in] string string to be matched against (the to-be-normalized data)
 * @param[in] offs start position in input data
 * @param[in/out] event handle to event that is being created during normalization
 * @param[out] endNode if a match was found, this is the matching node (undefined otherwise)
 *
 * @return number of characters left unparsed by following the subtree, negative if
 *         the to-be-parsed message is shorter than the rule sample by this number of
 *         characters.
 */
static int
ln_normalizeRec(struct ln_ptree *tree, es_str_t *str, es_size_t offs, struct ee_event **event,
		struct ln_ptree **endNode)
{
	int r;
	int localR;
	es_size_t i;
	int left;
	ln_fieldList_t *node;
	struct ee_value *value;
	char *cstr;
	unsigned char *c;
	unsigned char *cpfix;
	unsigned ipfix;
	
	if(offs >= es_strlen(str)) {
		*endNode = tree;
		r = -tree->lenPrefix;
		goto done;
	}

	c = es_getBufAddr(str);
	cpfix = prefixBase(tree);
	node = tree->froot;
	r = es_strlen(str) - offs;
	/* first we need to check if the common prefix matches (and consume input data while we do) */
	ipfix = 0;
	while(offs < es_strlen(str) && ipfix < tree->lenPrefix) {
		ln_dbgprintf(tree->ctx, "%d: prefix compare '%c', '%c'", (int) offs, c[offs], cpfix[ipfix]);
		if(c[offs] != cpfix[ipfix]) {
			r -= ipfix;
			goto done;
		}
		++offs, ++ipfix;
	}
	
	if(ipfix != tree->lenPrefix) {
		/* incomplete prefix match --> to-be-normalized string too short */
		r = ipfix - tree->lenPrefix;
		goto done;
	}

	r -= ipfix;
	ln_dbgprintf(tree->ctx, "%d: prefix compare succeeded, still valid", (int) offs);

	if(offs == es_strlen(str)) {
		*endNode = tree;
		r = 0;
		goto done;
	}


	/* now try the parsers */
	while(node != NULL) {
		if(tree->ctx->debug) {
			cstr = es_str2cstr(node->name, NULL);
			ln_dbgprintf(tree->ctx, "%d:trying parser for field '%s': %p",
					(int) offs, cstr, node->parser);
			free(cstr);
		}
		i = offs;
		if(node->isIPTables) {
			localR = ln_iptablesParser(tree, str, &i, event);
			ln_dbgprintf(tree->ctx, "%d iptables parser return, i=%d",
						(int) offs, (int)i);
			if(localR == 0) {
				/* potential hit, need to verify */
				ln_dbgprintf(tree->ctx, "potential hit, trying subtree");
				left = ln_normalizeRec(node->subtree, str, i, event, endNode);
				if(left == 0 && (*endNode)->flags.isTerminal) {
					ln_dbgprintf(tree->ctx, "%d: parser matches at %d", (int) offs, (int)i);
					r = 0;
					goto done;
				}
				ln_dbgprintf(tree->ctx, "%d nonmatch, backtracking required, left=%d",
						(int) offs, (int)left);
				if(left < r)
					r = left;
			}
		} else {
			localR = node->parser(tree->ctx->eectx, str, &i, node->data, &value);
			if(localR == 0) {
				/* potential hit, need to verify */
				ln_dbgprintf(tree->ctx, "potential hit, trying subtree");
				left = ln_normalizeRec(node->subtree, str, i, event, endNode);
				if(left == 0 && (*endNode)->flags.isTerminal) {
					ln_dbgprintf(tree->ctx, "%d: parser matches at %d", (int) offs, (int)i);
					if(!es_strbufcmp(node->name, (unsigned char*)"-", 1))
						ee_deleteValue(value); /* filler, discard */
					else
						CHKR(addField(tree->ctx, event, node->name, value));
					r = 0;
					goto done;
				} else {
					ee_deleteValue(value); /* was created, now not needed */
				}
				ln_dbgprintf(tree->ctx, "%d nonmatch, backtracking required, left=%d",
						(int) offs, (int)left);
				if(left < r)
					r = left;
			}
		}
		node = node->next;
	}

if(offs < es_strlen(str)) {
unsigned char cc = es_getBufAddr(str)[offs];
ln_dbgprintf(tree->ctx, "%u no field, trying subtree char '%c': %p", offs, cc, tree->subtree[cc]);
} else {
ln_dbgprintf(tree->ctx, "%u no field, offset already beyond end", offs);
}
	/* now let's see if we have a literal */
	if(tree->subtree[es_getBufAddr(str)[offs]] != NULL) {
		left = ln_normalizeRec(tree->subtree[es_getBufAddr(str)[offs]],
				       str, offs + 1, event, endNode);
		if(left < r)
			r = left;
	}

done:
	ln_dbgprintf(tree->ctx, "%d returns %d", (int) offs, (int) r);
	return r;
}


int
ln_normalize(ln_ctx ctx, es_str_t *str, struct ee_event **event)
{
	int r;
	int left;
	struct ln_ptree *endNode;

	left = ln_normalizeRec(ctx->ptree, str, 0, event, &endNode);

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
			addUnparsedField(ctx, str, es_strlen(str), event);
		} else {
			addUnparsedField(ctx, str, es_strlen(str) - left, event);
		}
	} else {
		/* success, finalize event */
		if(endNode->tags != NULL) {
			if(*event == NULL) {
				CHKN(*event = ee_newEvent(ctx->eectx));
			}
			CHKR(ee_assignTagbucketToEvent(*event, ee_addRefTagbucket(endNode->tags)));
			CHKR(ln_annotateEvent(ctx, *event));
		}
	}

	r = 0;

done:	return r;
}
