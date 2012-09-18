/**
 * @file normalizer.c
 * @brief A small tool to normalize data.
 *
 * This is the most basic example demonstrating how to use liblognorm.
 * It loads log samples from the files specified on the command line,
 * reads to-be-normalized data from stdin and writes the normalized
 * form to stdout. Besides being an example, it also carries out useful
 * processing.
 *
 * @author Rainer Gerhards <rgerhards@adiscon.com>
 *
 *//*
 * liblognorm - a fast samples-based log normalization library
 * Copyright 2010-2011 by Rainer Gerhards and Adiscon GmbH.
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
#include <string.h>
#include <getopt.h>
#include <libestr.h>
#include <libee/libee.h>
#include "liblognorm.h"
#include "ptree.h"
#include "lognorm.h"

static ln_ctx ctx;
static ee_ctx eectx;

static int verbose = 0;
static int parsedOnly = 0;	/**< output unparsed messages? */
static int flatTags = 0;	/**< output unparsed messages? */
static FILE *fpDOT;
static es_str_t *encFmt = NULL; /**< a format string for encoder use */
static es_str_t *mandatoryTag = NULL; /**< tag which must be given so that mesg will
					   be output. NULL=all */
static enum { f_syslog, f_json, f_xml, f_csv } outfmt = f_syslog;

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


/* param str is just a performance enhancement, which saves us re-creation
 * of the string on every call.
 */
static inline void
outputEvent(struct ee_event *event)
{
	char *cstr;
	es_str_t *str = NULL;

	switch(outfmt) {
	case f_json:
		ee_fmtEventToJSON(event, &str);
		break;
	case f_syslog:
		ee_fmtEventToRFC5424(event, &str);
		break;
	case f_xml:
		ee_fmtEventToXML(event, &str);
		break;
	case f_csv:
		ee_fmtEventToCSV(event, &str, encFmt);
		break;
	}
	cstr = es_str2cstr(str, NULL);
	if(verbose > 0) printf("normalized: '%s'\n", cstr);
	printf("%s\n", cstr);
	free(cstr);
	es_deleteStr(str);
}


/* normalize input data
 */
void
normalize(void)
{
	FILE *fp = stdin;
	char buf[10*1024];
	es_str_t *str;
	struct ee_event *event = NULL;
	es_str_t *constUnparsed;
	long long unsigned numUnparsed = 0;
	long long unsigned numWrongTag = 0;

	constUnparsed = es_newStrFromBuf("unparsed-data", sizeof("unparsed-data") - 1);

	while((fgets(buf, sizeof(buf), fp)) != NULL) {
		buf[strlen(buf)-1] = '\0';
		if(strlen(buf) > 0 && buf[strlen(buf)-1] == '\r')
			buf[strlen(buf)-1] = '\0';
		if(verbose > 0) printf("To normalize: '%s'\n", buf);
		str = es_newStrFromCStr(buf, strlen(buf));
		ln_normalize(ctx, str, &event);
		//printf("normalize result: %d\n", ln_normalizeRec(ctx, ctx->ptree, str, 0, &event));
		if(event != NULL) {
			if(   mandatoryTag == NULL
			   || (mandatoryTag != NULL && ee_EventHasTag(event, mandatoryTag))) {
				if(   parsedOnly == 1
				   && ee_getEventField(event, constUnparsed) != NULL){
					numUnparsed++;
				} else {
					outputEvent(event);
				}
			} else {
				numWrongTag++;
			}
			ee_deleteEvent(event);
			event = NULL;
		}
		es_deleteStr(str);
	}
	if(numUnparsed > 0)
		fprintf(stderr, "%llu unparsable entries\n", numUnparsed);
	if(numWrongTag > 0)
		fprintf(stderr, "%llu entries with wrong tag dropped\n", numWrongTag);
	es_deleteStr(constUnparsed);
}


/**
 * Generate a command file for the GNU DOT tools.
 */
static void
genDOT()
{
	es_str_t *str;

	str = es_newStr(1024);
	ln_genDotPTreeGraph(ctx->ptree, &str);
	fwrite(es_getBufAddr(str), 1, es_strlen(str), fpDOT);
}


int main(int argc, char *argv[])
{
	int opt;
	char *repository = NULL;
	
	while((opt = getopt(argc, argv, "d:e:r:E:vpt:T")) != -1) {
		switch (opt) {
		case 'd': /* generate DOT file */
			if(!strcmp(optarg, "")) {
				fpDOT = stdout;
			} else {
				if((fpDOT = fopen(optarg, "w")) == NULL) {
					errout("cannot open DOT file");
				}
			}
		case 'v':
			verbose++;
			break;
		case 'E': /* encoder-specific format string (will be validated by encoder) */ 
			encFmt = es_newStrFromCStr(optarg, strlen(optarg));
			break;
		case 'p':
			parsedOnly = 1;
			break;
		case 'T':
			flatTags = 1;
			break;
		case 'e': /* encoder to use */
			if(!strcmp(optarg, "json")) {
				outfmt = f_json;
			} else if(!strcmp(optarg, "xml")) {
				outfmt = f_xml;
			} else if(!strcmp(optarg, "csv")) {
				outfmt = f_csv;
			}
			break;
		case 'r': /* rule base to use */
			repository = optarg;
			break;
		case 't': /* if given, only messages tagged with the argument
			     are output */
			mandatoryTag = es_newStrFromCStr(optarg, strlen(optarg));
			break;
		}
	}
	
	if(repository == NULL) {
		errout("samples repository must be given");
	}

	if((ctx = ln_initCtx()) == NULL) {
		errout("Could not initialize liblognorm context");
	}

	if((eectx = ee_initCtx()) == NULL) {
		errout("Could not initialize libee context");
	}
	if(flatTags) {
		ee_setFlags(eectx, EE_CTX_FLAG_INCLUDE_FLAT_TAGS);
	}

	if(verbose) {
		ln_setDebugCB(ctx, dbgCallBack, NULL);
		ln_enableDebug(ctx, 1);
	}
	ln_setEECtx(ctx, eectx);

	ln_loadSamples(ctx, repository);

	if(verbose > 0)
		printf("number of tree nodes: %d\n", ctx->nNodes);

	if(fpDOT != NULL) {
		genDOT();
		exit(1);
	}

	if(verbose > 2) ln_displayPTree(ctx->ptree, 0);

	normalize();

	ln_exitCtx(ctx);
	return 0;
}
