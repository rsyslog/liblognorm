/* liblognorm - a fast samples-based log normalization library
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

#include "liblognorm.h"
#include "lognorm.h"
#include "ptree.h"

/**
 * Generate some debug message and call the caller provided callback.
 *
 * Will first check if a user callback is registered. If not, returns
 * immediately.
 */
void
ln_dbgprintf(ln_ctx ctx, char *fmt, ...)
{
	va_list ap;
	char buf[8*1024];
	size_t lenBuf;

	if(ctx->dbgCB == NULL)
		goto done;
	
	va_start(ap, fmt);
	lenBuf = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if(lenBuf >= sizeof(buf)) {
		/* prevent buffer overrruns and garbagge display */
		buf[sizeof(buf) - 5] = '.';
		buf[sizeof(buf) - 4] = '.';
		buf[sizeof(buf) - 3] = '.';
		buf[sizeof(buf) - 2] = '\n';
		buf[sizeof(buf) - 1] = '\0';
		lenBuf = sizeof(buf) - 1;
	}

	ctx->dbgCB(ctx->dbgCookie, buf, lenBuf);
done:	return;
}


void
ln_enableDebug(ln_ctx ctx, int i)
{
	ctx->debug = i & 0x01;
}
