/**
 * @file sample.c
 * @brief A very basic, yet complete, sample of how to use liblognorm.
 *
 * This is the most basic example demonstrating how to use liblognorm.
 * It loads log samples from the files specified on the command line,
 * reads to-be-normalized data from stdin and writes the normalized
 * form to stdout.
 *
 * @author Rainer Gerhards <rgerhards@adiscon.com>
 *
 *//*
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
#include <stdio.h>
#include "liblognorm.h"
#include "lognorm.h" // TODO: remove!

static ln_ctx ctx;

void
dbgCallBack(void __attribute__((unused)) *cookie, char *msg,
	    size_t __attribute__((unused)) lenMsg)
{
	printf("liblognorm: %s\n", msg);
}

void errout(char *errmsg)
{
	fprintf(stderr, "%s\n", errmsg);
	exit(1);
}


int main(int argc, char *argv[])
{
	if(argc != 2) {
		errout("samples repository must be given");
	}

	printf("Using liblognorm version %s.\n", ln_version());

	if((ctx = ln_initCtx()) == NULL) {
		errout("Could not initialize liblognorm context");
	}

	ln_setDebugCB(ctx, dbgCallBack, NULL);

	ln_loadSamples(ctx, argv[1]);

printf("number of tree nodes: %d\n", ctx->nNodes);
ln_displayPTree(ctx, ctx->ptree, 0);
	ln_exitCtx(ctx);
	return 0;
}
