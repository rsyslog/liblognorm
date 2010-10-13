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

#include "liblognorm.h"
#include "lognorm.h"
#include "samp.h"

struct ln_ptree*
newPTreeNode(void)
{
	struct ln_ptree *tree;

	if((tree = calloc(1, sizeof(struct ln_ptree))) == NULL)
		goto done;

done:
	return tree;
}


void traversePTree(ln_ctx ctx, struct ln_ptree *subtree, char *str,
                   int lenStr, int *parsedTo)
{
	int i = 0;
	struct ln_ptree* curr = NULL;

	assert(lenStr >= 0);
	do {
		// TODO: implement commonPrefix
		prev = curr;
		curr = subtree->subtree[str[i++]];
	} while(curr != NULL && i < lenStr);

	if(curr == NULL) {
		curr = prev;
	}

	--i;
done:
	*parsedTo = i;
	return curr;
}
