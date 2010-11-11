/**
 * @file internal.h
 * @brief Internal things just needed for building the library, but
 * not to be installed.
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
 *
 * The public interface of this library is describe in liblognorm.h.
 *
 * Liblognorm fully supports Unicode. Like most Linux tools, it operates
 * on UTF-8 natively, called "passive mode". This was decided because we
 * so can keep the size of data structures small while still supporting
 * all of the world's languages (actually more than when we did UCS-2).
 *
 * At the  technical level, we can handle UTF-8 multibyte sequences transparently.
 * Liblognorm needs to look at a few US-ASCII characters to do the
 * sample base parsing (things to indicate fields), so this is no
 * issue. Inside the parse tree, a multibyte sequence can simple be processed
 * as if it were a sequence of different characters that make up a their
 * own symbol each. In fact, this even allows for somewhat greater parsing
 * speed.
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
#ifndef INTERNAL_H_INCLUDED
#define	INTERNAL_H_INCLUDED

/* support for simple error checking */

#define CHKR(x) \
	if((r = (x)) != 0) goto done

#define CHKN(x) \
	if((x) == NULL) { \
		r = LN_NOMEM; \
		goto done; \
	}

#define FAIL(e) {r = (e); goto done;}

#endif /* #ifndef LOGNORM_H_INCLUDED */
