/* This file implements the liblognorm API.
 * See header file for descriptions.
 *
 * liblognorm - a fast samples-based log normalization library
 * Copyright 2013 by Rainer Gerhards and Adiscon GmbH.
 *
 * Modified by Pavel Levshin (pavel@levshin.spb.ru) in 2013
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
#include <string.h>

#include "json_compatibility.h"
#include "liblognorm.h"
#include "lognorm.h"
#include "annot.h"
#include "samp.h"

#define ERR_ABORT {r = 1; goto done; }

#define CHECK_CTX \
	if(ctx->objID != LN_ObjID_CTX) { \
		r = -1; \
		goto done; \
	}

char *
ln_version(void)
{
	return VERSION;
}

ln_ctx
ln_inherittedCtx(ln_ctx parent)
{
	ln_ctx child = ln_initCtx();
	if (child != NULL) {
		child->allowRegex = parent->allowRegex;
		child->dbgCB = parent->dbgCB;
		child->dbgCookie = parent->dbgCookie;
	}
	return child;
}

ln_ctx
ln_initCtx(void)
{
	ln_ctx ctx;
	if((ctx = calloc(1, sizeof(struct ln_ctx_s))) == NULL)
		goto done;

	ctx->objID = LN_ObjID_CTX;
	ctx->dbgCB = NULL;
	ctx->allowRegex = 0;

	/* we add an root for the empty word, this simplifies parse
	 * tree handling.
	 */
	if((ctx->ptree = ln_newPTree(ctx, NULL)) == NULL) {
		free(ctx);
		ctx = NULL;
		goto done;
	}
	/* same for annotation set */
	if((ctx->pas = ln_newAnnotSet(ctx)) == NULL) {
		ln_deletePTree(ctx->ptree);
		free(ctx);
		ctx = NULL;
		goto done;
	}

done:
	return ctx;
}

void
ln_setCtxOpts(ln_ctx ctx, int allow_regex) {
	ctx->allowRegex = allow_regex;
}


int
ln_exitCtx(ln_ctx ctx)
{
	int r = 0;

	CHECK_CTX;

	ctx->objID = LN_ObjID_None; /* prevent double free */
	if(ctx->ptree != NULL)
		ln_deletePTree(ctx->ptree);
	if(ctx->rulePrefix != NULL)
		es_deleteStr(ctx->rulePrefix);
	if(ctx->pas != NULL)
		ln_deleteAnnotSet(ctx->pas);
	free(ctx);
done:
	return r;
}


int
ln_setDebugCB(ln_ctx ctx, void (*cb)(void*, const char*, size_t), void *cookie)
{
	int r = 0;

	CHECK_CTX;
	ctx->dbgCB = cb;
	ctx->dbgCookie = cookie;
done:
	return r;
}


int
ln_loadSample(ln_ctx ctx, const char *buf)
{
    // Something bad happened - no new sample
    if (ln_processSamp(ctx, buf, strlen(buf)) == NULL) {
        return 1;
    }
    return 0;
}


int
ln_loadSamples(ln_ctx ctx, const char *file)
{
	int r = 0;
	struct ln_sampRepos *repo;
	struct ln_samp *samp;
	int isEof = 0;

	CHECK_CTX;
	if(file == NULL) ERR_ABORT;
	if((repo = ln_sampOpen(ctx, file)) == NULL) ERR_ABORT;
	while(!isEof) {
		if((samp = ln_sampRead(ctx, repo, &isEof)) == NULL) {
			/* TODO: what exactly to do? */
		}
	}
	ln_sampClose(ctx, repo);

done:
	return r;
}

