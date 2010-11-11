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
#include <libestr.h>

#include "liblognorm.h"
#include "lognorm.h"
#include "samp.h"
#include "ptree.h"
#include "internal.h"

struct ln_ptree*
ln_newPTree(ln_ctx ctx, struct ln_ptree *parent)
{
	struct ln_ptree *tree;

	if((tree = calloc(1, sizeof(struct ln_ptree))) == NULL)
		goto done;
	
	tree->parent = parent;
	ctx->nNodes++;
done:	return tree;
}


void
ln_deletePTree(ln_ctx ctx, struct ln_ptree *tree)
{
	ln_fieldList_t *node, *nodeDel;
	size_t i;

	if(tree == NULL)
		goto done;

	for(node = tree->froot ; node != NULL ; ) {
		ln_deletePTree(ctx, node->subtree);
		nodeDel = node;
		es_deleteStr(node->name);
		es_deleteStr(node->data);
		node = node->next;
		free(nodeDel);
	}

	for(i = 0 ; i < 256 ; ++i)
		if(tree->subtree[i] != NULL)
			ln_deletePTree(ctx, tree->subtree[i]);
	free(tree);

done:	return;
}


struct ln_ptree*
ln_traversePTree(ln_ctx ctx, struct ln_ptree *subtree, es_str_t *str, size_t *parsedTo)
{
	size_t i = 0;
	unsigned char *c;
	struct ln_ptree *curr = subtree;
	struct ln_ptree *prev = NULL;

	c = es_getBufAddr(str);
	while(curr != NULL && i < es_strlen(str)) {
		// TODO: implement commonPrefix
		ln_dbgprintf(ctx, "traversePTree: curr %p, char %u", curr, c[i]);
		prev = curr;
		curr = curr->subtree[c[i++]];
	};

	if(curr == NULL) {
		curr = prev;
	}

	--i;

	*parsedTo = i;
	ln_dbgprintf(ctx, "traversePTree: returns node %p, offset %u", curr, (unsigned) i);
	return curr;
}


struct ln_ptree *
ln_addPTree(ln_ctx ctx, struct ln_ptree *tree, es_str_t *str, size_t offs)
{
	struct ln_ptree *r;
	struct ln_ptree *new;

	if(ctx->debug) {
		char * cstr = es_str2cstr(str, NULL);
		ln_dbgprintf(ctx, "addPTree: add '%s', offs %u, tree %p",
			     cstr+offs, (unsigned) offs, tree);
		free(cstr);
	}

	if((new = ln_newPTree(ctx, tree)) == NULL) {
		r = NULL;
		goto done;
	}

	tree->subtree[es_getBufAddr(str)[offs]] = new;
	if(offs + 1 < es_strlen(str)) { /* need another recursion step? */
		r = ln_addPTree(ctx, new, str, offs + 1);
	} else {
		r = new;
	}

done:	return r;
}


int 
ln_addFDescrToPTree(ln_ctx ctx, struct ln_ptree *tree, ln_fieldList_t *node)
{
	int r;

	assert(tree != NULL);
	assert(node != NULL);

	if((node->subtree = ln_newPTree(ctx, tree)) == NULL) {
		r = -1;
		goto done;
	}

	if(tree->froot == NULL) {
		tree->froot = tree->ftail = node;
	} else {
		tree->ftail->next = node;
		tree->ftail = node;
	}
	r = 0;

done:	return r;
}


void
ln_displayPTree(ln_ctx ctx, struct ln_ptree *tree, int level)
{
	int i;
	int nChildLit;
	int nChildField;
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

	ln_dbgprintf(ctx, "%ssubtree %p (children: %d literals, %d fields)",
		     indent, tree, nChildLit, nChildField);
	/* display char subtrees */
	for(i = 0 ; i < 256 ; ++i) {
		if(tree->subtree[i] != NULL) {
			ln_dbgprintf(ctx, "%schar %2.2x(%c):", indent, i, i);
			ln_displayPTree(ctx, tree->subtree[i], level + 1);
		}
	}

	/* display field subtrees */
	for(node = tree->froot ; node != NULL ; node = node->next ) {
		cstr = es_str2cstr(node->name, NULL);
		ln_dbgprintf(ctx, "%sfield %s:", indent, cstr);
		free(cstr);
		ln_displayPTree(ctx, node->subtree, level + 1);
	}
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


/* check fields, if any are present (fields take precedence over
 * literals).
 * Offset is only modified if some data could be processed.
 * @return 0 if a field was processed, 1 otherwise.
 */
static inline int
checkFields(ln_ctx ctx, es_str_t *str, struct ee_event **event, size_t *offs,
	   struct ln_ptree **tree)
{
	int r;
	size_t i;
	ln_fieldList_t *node;
	struct ee_value *value;
	char *cstr;

	i = *offs;
	node = (*tree)->froot;
	ln_dbgprintf(ctx, "enter checkfields, node %p, offs %u, str %p",
			node, (unsigned) i, str);
	while(node != NULL) {
		cstr = es_str2cstr(node->name, NULL);
		ln_dbgprintf(ctx, "trying parser for field '%s': %p",
			     cstr, node->parser);
		free(cstr);
		if((r = node->parser(ctx->eectx, str, &i, &value)) == 0) {
			/* got it! */
			ln_dbgprintf(ctx, "parser call was successful, offs now %u",
				     (unsigned) i);
			CHKR(addField(ctx, event, node->name, value));
			*offs = i;
			*tree = node->subtree;
			r = 0;
			break;
		}
		node = node->next;
	}
	r = 1; /* we failed! */

done:
	//ln_dbgprintf(ctx, "CheckFields returns %d\n", r);
	return r;
}


int
ln_normalize(ln_ctx ctx, es_str_t *str, struct ee_event **event)
{
	int r;
	size_t offs;
	struct ln_ptree *tree;
	unsigned char *c;

	offs = 0;
	tree = ctx->ptree;
	c = es_getBufAddr(str);

	while(tree != NULL && offs < es_strlen(str)) {
		// TODO: implement commonPrefix
		ln_dbgprintf(ctx, "normalize: tree %p, char '%c'", tree, c[offs]);
		if(checkFields(ctx, str, event, &offs, &tree) == 1) {
			/* field matching failed, on to next literal */
			ln_dbgprintf(ctx, "normalize: trying literal tree %p, char '%c'", tree, c[offs]);
			tree = tree->subtree[c[offs++]];
			ln_dbgprintf(ctx, "normalize: next tree %p", tree);
		}
	};

done:	return r;
}
