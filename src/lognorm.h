/**
 * @file lognorm.h
 * @brief Private data structures used by the liblognorm API.
 *//*
 *
 * liblognorm - a fast samples-based log normalization library
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
#ifndef LIBLOGNORM_LOGNORM_HINCLUDED
#define	LIBLOGNORM_LOGNORM_HINCLUDED
#include <stdlib.h>	/* we need size_t */
#include "ptree.h"
#include "annot.h"

#define LN_ObjID_None 0xFEFE0001
#define LN_ObjID_CTX 0xFEFE0001

struct ln_ctx_s {
	unsigned objID;	/**< a magic number to prevent some memory adressing errors */
	void (*dbgCB)(void *cookie, char *msg, size_t lenMsg);
		/**< user-provided debug output callback */
	void *dbgCookie; /**< cookie to be passed to debug callback */
	ee_ctx eectx;
	ln_ptree *ptree; /**< parse tree being used by this context */
	ln_annotSet *pas; /**< associated set of annotations */
	unsigned nNodes; /**< number of nodes in our parse tree */
	unsigned char debug; /**< boolean: are we in debug mode? */
	es_str_t *rulePrefix; /**< work variable for loading rule bases
			       * this is the prefix string that will be prepended
			       * to all rules before they are submitted to tree
			       * building.
			       */
};

void ln_dbgprintf(ln_ctx ctx, char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif /* #ifndef LIBLOGNORM_LOGNORM_HINCLUDED */
