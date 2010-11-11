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

#include "liblognorm.h"
#include "lognorm.h"
#include "samp.h"

struct ln_ptree*
ln_newPTree(ln_ctx ctx, struct ln_ptree *parent)
{
	struct ln_ptree *tree;

	if((tree = calloc(1, sizeof(struct ln_ptree))) == NULL)
		goto done;
	
	tree->parent = parent;
	ctx->nNodes++;
done:
	return tree;
}


void
ln_freePTree(ln_ctx ctx, struct ln_ptree *tree)
{
	free(tree); // TODO: leak, leak do recursive!
}


struct ln_ptree*
ln_traversePTree(ln_ctx ctx, struct ln_ptree *subtree, char *str,
                 int lenStr, int *parsedTo)
{
	int i = 0;
	struct ln_ptree *curr = subtree;
	struct ln_ptree *prev = NULL;

	ln_dbgprintf(ctx, "traversePTree: search '%s'(%d), subtree %p",
		str, lenStr, subtree);
	assert(lenStr >= 0);
	while(curr != NULL && i < lenStr) {
		// TODO: implement commonPrefix
		ln_dbgprintf(ctx, "traversePTree: curr %p, char %u", curr, (unsigned char) str[i]);
		prev = curr;
		curr = curr->subtree[(unsigned char)str[i++]];
	};

	if(curr == NULL) {
		curr = prev;
	}

	--i;
done:
	*parsedTo = i;
	ln_dbgprintf(ctx, "traversePTree: returns node %p, offset %d", curr, i);
	return curr;
}


int
ln_addPTree(ln_ctx ctx, struct ln_ptree *parent, char *str,
              int lenStr)
{
	int r =0;
	struct ln_ptree *new;

	if(lenStr == 0) /* break recursion */
		goto done;

	ln_dbgprintf(ctx, "addPTree: add '%s'(%d), parent %p\n",
	             str, lenStr, parent);

	if((new = ln_newPTree(ctx, parent)) == NULL) {
		r = -1;
		goto done;
	}

	parent->subtree[(unsigned char)*str] = new;
	r = ln_addPTree(ctx, new, str+1, lenStr-1);

done:
	return r;
}


void
ln_displayPTree(ln_ctx ctx, struct ln_ptree *tree, int level)
{
	char indent[2048];
	int i;
	int nChilds;

	if(level > 1023)
		level = 1023;
	memset(indent, ' ', level * 2);
	indent[level * 2 + 1] = '\0';
	nChilds = 0;
	for(i = 0 ; i < 256 ; ++i) {
		if(tree->subtree[i] != NULL) {
			nChilds++;
		}
	}
	ln_dbgprintf(ctx, "%sSubtree %p (%d children)", indent, tree, nChilds);
	/* display char subtrees */
	for(i = 0 ; i < 256 ; ++i) {
		if(tree->subtree[i] != NULL) {
			if(level < 10)
			ln_dbgprintf(ctx, "%schar %2.2x(%c):", indent, i, i);
			ln_displayPTree(ctx, tree->subtree[i], level + 1);
		}
	}
}
