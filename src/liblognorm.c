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
#include <errno.h>

#include "json_compatibility.h"
#include "liblognorm.h"
#include "lognorm.h"
#include "annot.h"
#include "samp.h"
#include "v1_liblognorm.h"
#include "v1_ptree.h"

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
ln_initCtx(void)
{
	ln_ctx ctx;
	if((ctx = calloc(1, sizeof(struct ln_ctx_s))) == NULL)
		goto done;

	ctx->objID = LN_ObjID_CTX;
	ctx->dbgCB = NULL;
	ctx->allowRegex = 0;

	/* we add an root for the empty word, this simplifies parse
	 * dag handling.
	 */
	if((ctx->pdag = ln_newPDAG(ctx)) == NULL) {
		free(ctx);
		ctx = NULL;
		goto done;
	}
	/* same for annotation set */
	if((ctx->pas = ln_newAnnotSet(ctx)) == NULL) {
		ln_pdagDelete(ctx->pdag);
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

	ln_dbgprintf(ctx, "exitCtx %p", ctx);
	ctx->objID = LN_ObjID_None; /* prevent double free */
	/* support for old cruft */
	if(ctx->ptree != NULL)
		ln_deletePTree(ctx->ptree);
	/* end support for old cruft */
	if(ctx->pdag != NULL)
		ln_pdagDelete(ctx->pdag);
	for(int i = 0 ; i < ctx->nTypes ; ++i) {
		free((void*)ctx->type_pdags[i].name);
		ln_pdagDelete(ctx->type_pdags[i].pdag);
	}
	free(ctx->type_pdags);
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
ln_setErrMsgCB(ln_ctx ctx, void (*cb)(void*, const char*, size_t), void *cookie)
{
	int r = 0;

	CHECK_CTX;
	ctx->errmsgCB = cb;
	ctx->errmsgCookie = cookie;
done:
	return r;
}


/* check rulebase format version. Returns 2 if this is v2 rulebase,
 * 1 for any pre-v2 and -1 if there was a problem reading the file.
 */
static int
checkVersion(FILE *const fp)
{
	char buf[64];

	if(fgets(buf, sizeof(buf), fp) == NULL)
		return -1;
	if(!strcmp(buf, "version=2\n")) {
		return 2;
	} else {
		return 1;
	}
}

/* we have a v1 rulebase, so let's do all stuff that we need
 * to make that ole piece of ... work.
 */
static int
doOldCruft(ln_ctx ctx, const char *file)
{
	int r = -1;
	if((ctx->ptree = ln_newPTree(ctx, NULL)) == NULL) {
		free(ctx);
		r = -1;
		goto done;
	}
	r = ln_v1_loadSamples(ctx, file);
done:
	return r;
}

int
ln_loadSamples(ln_ctx ctx, const char *file)
{
	int r = 0;
	FILE *repo;
	struct ln_samp *samp;
	int isEof = 0;

	CHECK_CTX;
	if(file == NULL) ERR_ABORT;
	if((repo = fopen(file, "r")) == NULL) {
		ln_errprintf(ctx, errno, "cannot open file %s", file);
		ERR_ABORT;
	}
	ctx->version = checkVersion(repo);
	ln_dbgprintf(ctx, "rulebase version is %d\n", ctx->version);
	if(ctx->version == -1) {
		ln_errprintf(ctx, errno, "error determing version of %s", file);
		ERR_ABORT;
	}
	if(ctx->version == 1) {
		fclose(repo);
		r = doOldCruft(ctx, file);
		goto done;
	}

	/* now we are in our native code */
	while(!isEof) {
		if((samp = ln_sampRead(ctx, repo, &isEof)) == NULL) {
			/* TODO: what exactly to do? */
		}
	}
	fclose(repo);

	ln_pdagOptimize(ctx);
done:
	return r;
}

