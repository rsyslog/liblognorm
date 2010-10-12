/* tree.h -- the parse tree object
 * 
 * Copyright 2010 by Rainer Gerhards and Adiscon GmbH.
 *
 * This file is meant to be included by applications using liblognorm.
 * For lognorm library files themselves, include "lognorm.h".
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
#ifndef LIBLOGNORM_TREE_H_INCLUDED
#define	LIBLOGNORM_TREE_H_INCLUDED

typedef struct ln_ptree_s ln_ptree_t;
typedef struct ln_fieldList_s ln_fieldList_t;

/* value list. This is a single-linked list. In a later stage, we should
 * optimize it so that frequently used fields are moved "up" towards
 * the root of the list. In any case, we do NOT expect this list to
 * be long, as the parser should already have gotten quite specific when
 * we hit a field.
 */
struct ln_fieldList_s {
	/* TODO: add syntax/names for fields */
	ln_ptree_t *subtree;
	ln_fieldList_t *next;
};

/* parse tree object
 */
struct ln_ptree_s {
	ln_fieldList_t	*fields; /* these will be parsed first */
	/* the respresentation below requires a lof of memory but is
	 * very fast. As an alternate approach, we can use a hash table
	 * where we ignore control characters. That should work quite well.
	 * But we do not do this in the initial step.
	 */
	ln_ptree_t	*subtree[sizeof(char)];
};


/* library context
 */
typedef struct ln_context_s {
	ln_ptree_t *ptree;
} ln_context;


#endif /* #ifndef LOGNORM_TREE_H_INCLUDED */
