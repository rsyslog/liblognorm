/* liblognorm - a fast samples-based log normalization library
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
#ifndef LIBLOGNORM_H_INCLUDED
#define	LIBLOGNORM_H_INCLUDED

/* event_t needs to come from libcee, or whatever it will be called. We
 * provide a dummy to be able to compile the initial skeletons.
 */
typedef void * event_t;

/* the library context descriptor
 */
typedef struct ln_context* ln_ctx_t;

/* API */
ln_ctx_t ln_initCtx(void);
int ln_exitCtx(ln_ctx_t);
int ln_normalizeMsg(ln_ctx_t, char *, event_t *);

#endif /* #ifndef LOGNORM_H_INCLUDED */
