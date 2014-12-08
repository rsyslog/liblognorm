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
 * Copyright 2010-2013 by Rainer Gerhards and Adiscon GmbH.
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

#include "json_compatibility.h"
#include "liblognorm.h"
#include "ptree.h"
#include "lognorm.h"
#include "enc.h"

static ln_ctx ctx;

static int verbose = 0;
static int parsedOnly = 0;	/**< output unparsed messages? */
static int flatTags = 0;	/**< print event.tags in JSON? */
static FILE *fpDOT;
static es_str_t *encFmt = NULL; /**< a format string for encoder use */
static es_str_t *mandatoryTag = NULL; /**< tag which must be given so that mesg will
					   be output. NULL=all */
static enum { f_syslog, f_json, f_xml, f_csv } outfmt = f_syslog;

void
dbgCallBack(void __attribute__((unused)) *cookie, const char *msg,
	    size_t __attribute__((unused)) lenMsg)
{
	printf("liblognorm: %s\n", msg);
}

void complain(const char *errmsg)
{
	fprintf(stderr, "%s\n", errmsg);
}


/* param str is just a performance enhancement, which saves us re-creation
 * of the string on every call.
 */
static inline void
outputEvent(struct json_object *json)
{
	char *cstr;
	es_str_t *str = NULL;

	switch(outfmt) {
	case f_json:
		if(!flatTags) {
			json_object_object_del(json, "event.tags");
		}
		cstr = (char*)json_object_to_json_string(json);
		break;
	case f_syslog:
		ln_fmtEventToRFC5424(json, &str);
		break;
	case f_xml:
		ln_fmtEventToXML(json, &str);
		break;
	case f_csv:
	ln_fmtEventToCSV(json, &str, encFmt);
		break;
	}
	if (str != NULL)
		cstr = es_str2cstr(str, NULL);
	if(verbose > 0) printf("normalized: '%s'\n", cstr);
	printf("%s\n", cstr);
	if (str != NULL)
		free(cstr);
	es_deleteStr(str);
}

/* test if the tag exists */
static int
eventHasTag(struct json_object *json, const char *tag)
{
	struct json_object *tagbucket, *tagObj;
	int i;
	const char *tagCstr;
	
	if (tag == NULL)
		return 1;
	if ((tagbucket = json_object_object_get(json, "event.tags")) != NULL) {
		if (json_object_get_type(tagbucket) == json_type_array) {
			for (i = json_object_array_length(tagbucket) - 1; i >= 0; i--) {
				tagObj = json_object_array_get_idx(tagbucket, i);
				tagCstr = json_object_get_string(tagObj);
				if (!strcmp(tag, tagCstr))
					return 1;
			}
		}
	}
	if (verbose > 1)
		printf("Mandatory tag '%s' has not been found\n", tag);
	return 0;
}

/* normalize input data
 */
void
normalize(void)
{
	FILE *fp = stdin;
	char buf[10*1024];
	struct json_object *json = NULL;
	long long unsigned numUnparsed = 0;
	long long unsigned numWrongTag = 0;
	char *mandatoryTagCstr = NULL;
	
	if (mandatoryTag != NULL) {
		mandatoryTagCstr = es_str2cstr(mandatoryTag, NULL);
	}

	while((fgets(buf, sizeof(buf), fp)) != NULL) {
		buf[strlen(buf)-1] = '\0';
		if(strlen(buf) > 0 && buf[strlen(buf)-1] == '\r')
			buf[strlen(buf)-1] = '\0';
		if(verbose > 0) printf("To normalize: '%s'\n", buf);
		ln_normalize(ctx, buf, strlen(buf), &json);
		if(json != NULL) {
			if(eventHasTag(json, mandatoryTagCstr)) {
				if( parsedOnly == 1
						&& json_object_object_get(json, "unparsed-data") != NULL) {
					numUnparsed++;
				} else {
					outputEvent(json);
				}
			} else {
				numWrongTag++;
			}
			json_object_put(json);
			json = NULL;
		}
	}
	if(numUnparsed > 0)
		fprintf(stderr, "%llu unparsable entries\n", numUnparsed);
	if(numWrongTag > 0)
		fprintf(stderr, "%llu entries with wrong tag dropped\n", numWrongTag);
	free(mandatoryTagCstr);
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

static void
handle_generic_option(const char* opt) {
	if (strcmp("allowRegex", opt) == 0) {
		ln_setCtxOpts(ctx, 1);
	}
}

static void usage(void)
{
fprintf(stderr,
	"Options:\n"
	"    -r<rulebase> Rulebase to use. This is required option\n"
	"    -e<json|xml|csv>\n"
	"                 Change output format. By default, Mitre CEE is used\n"
	"    -E<format>   Encoder-specific format (used for CSV, read docs)\n"
	"    -T           Include 'event.tags' in JSON format\n"
	"    -oallowRegex Allow regexp matching (read docs about performance penalty)\n"
	"    -p           Print back only if the message has been parsed succesfully\n"
	"    -t<tag>      Print back only messages matching the tag\n"
	"    -v           Print debug. When used 3 times, prints parse tree\n"
	"    -d           Print DOT file to stdout and exit\n"
	"    -d<filename> Save DOT file to the filename\n"
	"\n"
	);
}

int main(int argc, char *argv[])
{
	int opt;
	char *repository = NULL;
	int ret = 0;

	if((ctx = ln_initCtx()) == NULL) {
		complain("Could not initialize liblognorm context");
		ret = 1;
		goto exit;
	}
	
	while((opt = getopt(argc, argv, "d:e:r:E:vpt:To:h")) != -1) {
		switch (opt) {
		case 'd': /* generate DOT file */
			if(!strcmp(optarg, "")) {
				fpDOT = stdout;
			} else {
				if((fpDOT = fopen(optarg, "w")) == NULL) {
					complain("Cannot open DOT file");
					ret = 1;
					goto exit;
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
		case 'o':
			handle_generic_option(optarg);
			break;
		case 'h':
			usage();
			ret = 1;
			goto exit;
			break;
		}
	}
	
	if(repository == NULL) {
		complain("Samples repository must be given (-r)");
		ret = 1;
		goto exit;
	}

	if(verbose) {
		ln_setDebugCB(ctx, dbgCallBack, NULL);
		ln_enableDebug(ctx, 1);
	}

	ln_loadSamples(ctx, repository);

	if(verbose > 0)
		printf("number of tree nodes: %d\n", ctx->nNodes);

	if(fpDOT != NULL) {
		genDOT();
		ret=1;
		goto exit;
	}

	if(verbose > 2) ln_displayPTree(ctx->ptree, 0);

	normalize();

exit:
	if (ctx) ln_exitCtx(ctx);
	if (encFmt != NULL)
		free(encFmt);
	return ret;
}
