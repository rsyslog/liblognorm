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

#define ObjID_None 0xFEFE0001
#define ObjID_CTX 0xFEFE0001

struct ln_ctx_s {
	unsigned objID;	/**< a magic number to prevent some memory adressing errors */
	void (*dbgCB)(void *cookie, char *msg, size_t lenMsg);
		/**< user-provided debug output callback */
	void *dbgCookie; /**< cookie to be passed to debug callback */
	ln_ptree *ptree; /**< parse tree being used by this context */
	unsigned nNodes; /**< number of nodes in our parse tree */
};

void ln_dbgprintf(ln_ctx ctx, char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif /* #ifndef LIBLOGNORM_LOGNORM_HINCLUDED */
