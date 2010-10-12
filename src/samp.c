/* samp.c -- code for ln_samp objects.
 *
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

struct ln_sampRepos*
ln_sampOpen(ln_ctx __attribute((unused)) ctx, char *name)
{
	struct ln_sampRepos *repo = NULL;
	FILE *fp;

	if((fp = fopen(name, "r")) == NULL)
		goto done;

	if((repo = calloc(1, sizeof(struct ln_sampRepos))) == NULL) {
		fclose(fp);
		goto done;
	}

	repo->fp = fp;

done:
	return repo;
}


void
ln_sampClose(ln_ctx __attribute((unused)) ctx, struct ln_sampRepos *repo)
{
	if(repo->fp != NULL)
		fclose(repo->fp);
	free(repo);
}


/**
 * Construct a sample object.
 */
struct ln_samp*
ln_sampCreate(ln_ctx __attribute__((unused)) ctx)
{
	struct ln_samp* samp;

	if((samp = calloc(1, sizeof(struct ln_samp))) == NULL)
		goto done;
	
	/* place specific init code here (none at this time) */

done:
	return samp;
}

void
ln_sampFree(ln_ctx __attribute__((unused)) ctx, struct ln_samp *samp)
{
	free(samp);
}


struct ln_samp *
ln_sampRead(ln_ctx ctx, struct ln_sampRepos *repo, int *isEof)
{
	struct ln_samp *samp = NULL;
	unsigned i;
	int done = 0;
	char buf[10*1024]; /**< max size of log sample */ // TODO: make configurable
	size_t lenBuf;

	/* we ignore empty lines and lines that begin with "#" */
	while(!done) {
		if(feof(repo->fp)) {
			*isEof = 1;
			goto done;
		}

		buf[0] = '\0'; /* fgets does not empty its buffer! */
		fgets(buf, sizeof(buf), repo->fp);
		lenBuf = strlen(buf);
		if(lenBuf == 0 || buf[0] == '#' || buf[0] == '\n')
			continue;
		if(buf[lenBuf - 1] == '\n') {
			buf[lenBuf - 1] = '\0';
			lenBuf--;
		}
		done = 1; /* we found a valid line */
	}

	ln_dbgprintf(ctx, "read sample line: '%s'", buf);

	/* skip tags, which we do not currently support (as we miss libcee) */
	for(i = 0 ; i < lenBuf && buf[i] != ':' ; )
		i++;
	if(i == lenBuf) {
		ln_dbgprintf(ctx, "error, tag part is missing");
		// TODO: provide some error indicator to app? We definitely must do (a callback?)
		goto done;
	}

	++i;
	if(i == lenBuf) {
		ln_dbgprintf(ctx, "error, actual message sample part is missing");
		// TODO: provide some error indicator to app? We definitely must do (a callback?)
		goto done;
	}

	ln_dbgprintf(ctx, "actual sample is '%s'", buf+i);
done:
	return samp;
}
