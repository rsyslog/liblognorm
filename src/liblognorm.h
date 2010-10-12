/**
 * @file liblognorm.h
 * @brief The public liblognorm API.
 *
 * <b>Functions other than those defined here MUST not be called by
 * a liblognorm "user" application.</b>
 *
 * This file is meant to be included by applications using liblognorm.
 * For lognorm library files themselves, include "lognorm.h".
 *//**
 * @mainpage
 * Liblognorm is an easy to use and fast samples-based log normalization 
 * library.
 * 
 * It can be passed a stream of arbitrary log messages, one at a time, and for
 * each message it will output well-defined name-value pairs and a set of
 * tags describing the message.
 *
 * For further details, see it's initial announcement available at
 *    http://blog.gerhards.net/2010/10/introducing-liblognorm.html
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
#ifndef LIBLOGNORM_H_INCLUDED
#define	LIBLOGNORM_H_INCLUDED
#include <stdlib.h>	/* we need size_t */

/* event_t needs to come from libcee, or whatever it will be called. We
 * provide a dummy to be able to compile the initial skeletons.
 */
typedef void * event_t;

/**
 * The library context descriptor.
 * This is used to permit multiple independednt instances of the
 * library to be called within a single program. This is most 
 * useful for plugin-based architectures.
 */
typedef struct ln_context* ln_ctx_t;

/* API */
/**
 * Return library version string.
 *
 * Returns the version of the currently used library.
 *
 * @return Zero-Terminated library version string.
 */
/* Note: this MUST NOT be inline to make sure the actual library
 * has the right version, not just what was used to compile!
 */
char *ln_version(void);

/**
 * Initialize a library context.
 *
 * To prevent memory leaks, ln_exitCtx() must be called on a library
 * context that is no longer needed.
 *
 * @return new library context or NULL if an error occured
 */
ln_ctx_t ln_initCtx(void);

/**
 * Discard a library context.
 *
 * Free's the ressources associated with the given library context. It
 * MUST NOT be accessed after calling this function.
 *
 * @param ctx The context to be discarded.
 *
 * @return Returns zero on success, something else otherwise.
 */
int ln_exitCtx(ln_ctx_t ctx);

/**
 * Set a debug message handler (callback).
 *
 * Liblognorm can provide helpful information for debugging
 * - it's internal processing
 * - the way a log message is being normalized
 *
 * It does so by emiting "interesting" information about its processing
 * at various stages. A caller can obtain this information by registering
 * an entry point. When done so, liblognorm will call the entry point
 * whenever it has something to emit. Note that debugging can be rather
 * verbose.
 *
 * The callback will be called with the following three parameters in that order:
 * - the caller-provided cookie
 * - a zero-terminated string buffer
 * - the length of the string buffer, without the trailing NUL byte
 *
 * @note
 * The provided callback function <b>must not</b> call any liblognorm
 * APIs except when specifically flagged as safe for calling by a debug
 * callback handler.
 *
 * @param[in] ctx The library context to apply callback to.
 * @param[in] cb The function to be called for debugging
 * @param[in] cookie Opaque cookie to be passed down to debug handler. Can be
 *                   used for some state tracking by the caller. This is defined as
 *                   void* to support pointers. To play it safe, a pointer should be
 *                   passed (but advantorous folks may also use an unsigned).
 */
int ln_setDebugCB(ln_ctx_t ctx, void (*cb)(void*, char*, size_t), void *cookie);

/**
 * Normalize a message.
 *
 * This is the main library entry point. It is called with a message
 * to normalize and will return a normalized in-memory representation
 * of it.
 *
 * If an error occurs, the function returns -1. In that case, an 
 * in-memory event representation has been generated if event is
 * non-NULL. In that case, the event contains further error details in
 * normalized form.
 *
 * @note
 * This function works on byte-counted strings and as such is able to
 * process NUL bytes if they occur inside the message. On the other hand,
 * this means the the correct messages size, \b excluding the NUL byte,
 * must be provided.
 *
 * @param[in] ctx The library context to use.
 * @param[in] msg The message string (see note above).
 * @param[in] lenmsg The length of the message in bytes.
 * @param[out] event A new event record or NULL if an error occured. <b>Must be 
 *                   destructed if no longer needed.</b>
 *
 * @return Returns zero on success, something else otherwise.
 */
int ln_normalizeMsg(ln_ctx_t ctx, char *msg, size_t lenmsg, event_t *event);

#endif /* #ifndef LOGNORM_H_INCLUDED */
