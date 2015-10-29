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
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <libestr.h>

#include "json_compatibility.h"
#include "liblognorm.h"
#include "lognorm.h"
#include "enc.h"

static ln_ctx ctx;

static int verbose = 0;
#define OUTPUT_PARSED_RECS 0x01
#define OUTPUT_UNPARSED_RECS 0x02
static int recOutput = OUTPUT_PARSED_RECS | OUTPUT_UNPARSED_RECS; 
				/**< controls which records to output */
static int addErrLineNbr = 0;	/**< add line number info to unparsed events */
static int flatTags = 0;	/**< print event.tags in JSON? */
static FILE *fpDOT;
static es_str_t *encFmt = NULL; /**< a format string for encoder use */
static es_str_t *mandatoryTag = NULL; /**< tag which must be given so that mesg will
					   be output. NULL=all */
static enum { f_syslog, f_json, f_xml, f_csv } outfmt = f_json;

void
errCallBack(void __attribute__((unused)) *cookie, const char *msg,
	    size_t __attribute__((unused)) lenMsg)
{
	fprintf(stderr, "liblognorm error: %s\n", msg);
}

void
dbgCallBack(void __attribute__((unused)) *cookie, const char *msg,
	    size_t __attribute__((unused)) lenMsg)
{
	fprintf(stderr, "liblognorm: %s\n", msg);
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
	char *cstr = NULL;
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
	if(verbose > 0) fprintf(stderr, "normalized: '%s'\n", cstr);
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
	if (json_object_object_get_ex(json, "event.tags", &tagbucket)) {
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

static void
amendLineNbr(json_object *const json, const int line_nbr)
{
	
	if(addErrLineNbr) {
		struct json_object *jval;
		jval = json_object_new_int(line_nbr);
		json_object_object_add(json, "lognormalizer.line_nbr", jval);
	}
}

/* normalize input data
 */
void
normalize(void)
{
	FILE *fp = stdin;
	char buf[10*1024];
	struct json_object *json = NULL;
	long long unsigned numParsed = 0;
	long long unsigned numUnparsed = 0;
	long long unsigned numWrongTag = 0;
	char *mandatoryTagCstr = NULL;
	int line_nbr = 0;	/* must be int to keep compatible with older json-c */
	
	if (mandatoryTag != NULL) {
		mandatoryTagCstr = es_str2cstr(mandatoryTag, NULL);
	}

	while((fgets(buf, sizeof(buf), fp)) != NULL) {
		++line_nbr;
		size_t bufLen = strlen(buf) - 1;
		buf[bufLen] = '\0';
		if(bufLen > 0 && buf[bufLen-1] == '\r') {
			buf[bufLen-1] = '\0';
			--bufLen;
		}
		if(verbose > 0) fprintf(stderr, "To normalize: '%s'\n", buf);
		ln_normalize(ctx, buf, bufLen, &json);
		if(json != NULL) {
			if(eventHasTag(json, mandatoryTagCstr)) {
				struct json_object *dummy;
				const int parsed = !json_object_object_get_ex(json,
					"unparsed-data", &dummy);
				if(parsed) {
					numParsed++;
					if(recOutput & OUTPUT_PARSED_RECS) {
						outputEvent(json);
					}
				} else {
					numUnparsed++;
					amendLineNbr(json, line_nbr);
					if(recOutput & OUTPUT_UNPARSED_RECS) {
						outputEvent(json);
					}
				}
			} else {
				numWrongTag++;
			}
			json_object_put(json);
			json = NULL;
		}
	}
	if((recOutput & OUTPUT_PARSED_RECS) && numUnparsed > 0)
		fprintf(stderr, "%llu unparsable entries\n", numUnparsed);
	if(numWrongTag > 0)
		fprintf(stderr, "%llu entries with wrong tag dropped\n", numWrongTag);
	fprintf(stderr, "%llu records processed, %llu parsed, %llu unparsed\n",
		numParsed+numUnparsed, numParsed, numUnparsed);
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
	ln_genDotPDAGGraph(ctx->pdag, &str);
	fwrite(es_getBufAddr(str), 1, es_strlen(str), fpDOT);
}

static void
handle_generic_option(const char* opt) {
	if (strcmp("allowRegex", opt) == 0) {
		ln_setCtxOpts(ctx, LN_CTXOPT_ALLOW_REGEX);
	} else if (strcmp("addExecPath", opt) == 0) {
		ln_setCtxOpts(ctx, LN_CTXOPT_ADD_EXEC_PATH);
	} else if (strcmp("addOriginalMsg", opt) == 0) {
		ln_setCtxOpts(ctx, LN_CTXOPT_ADD_ORIGINALMSG);
	} else if (strcmp("addRule", opt) == 0) {
		ln_setCtxOpts(ctx, LN_CTXOPT_ADD_RULE);
	} else if (strcmp("addRuleLocation", opt) == 0) {
		ln_setCtxOpts(ctx, LN_CTXOPT_ADD_RULE_LOCATION);
	} else {
		fprintf(stderr, "invalid -o option '%s'\n", opt);
		exit(1);
	}
}

static void usage(void)
{
fprintf(stderr,
	"Options:\n"
	"    -r<rulebase> Rulebase to use. This is required option\n"
	"    -e<json|xml|csv|cee-syslog>\n"
	"                 Change output format. By default, json is used\n"
	"    -E<format>   Encoder-specific format (used for CSV, read docs)\n"
	"    -T           Include 'event.tags' in JSON format\n"
	"    -oallowRegex Allow regexp matching (read docs about performance penalty)\n"
	"    -oaddRule    Add a mockup of the matching rule.\n"
	"    -oaddRuleLocation Add location of matching rule to metadata\n"
	"    -oaddExecPath Add exec_path attribute to output\n"
	"    -oaddOriginalMsg Always add original message to output, not just in error case\n"
	"    -p           Print back only if the message has been parsed succesfully\n"
	"    -P           Print back only if the message has NOT been parsed succesfully\n"
	"    -L           Add source file line number information to unparsed line output\n"
	"    -t<tag>      Print back only messages matching the tag\n"
	"    -v           Print debug. When used 3 times, prints parse tree\n"
	"    -d           Print DOT file to stdout and exit\n"
	"    -d<filename> Save DOT file to the filename\n"
	"    -s<filename> Print parse dag statistics and exit\n"
	"    -S<filename> Print extended parse dag statistics and exit (includes -s)\n"
	"    -x<filename> Print statistics as dot file (called only)\n"
	"\n"
	);
}

int main(int argc, char *argv[])
{
	int opt;
	char *repository = NULL;
	int ret = 0;
	FILE *fpStats = NULL;
	FILE *fpStatsDOT = NULL;
	int extendedStats = 0;

	if((ctx = ln_initCtx()) == NULL) {
		complain("Could not initialize liblognorm context");
		ret = 1;
		goto exit;
	}
	
	while((opt = getopt(argc, argv, "d:s:S:e:r:E:vpPt:To:hLx:")) != -1) {
		switch (opt) {
		case 'd': /* generate DOT file */
			if(!strcmp(optarg, "")) {
				fpDOT = stdout;
			} else {
				if((fpDOT = fopen(optarg, "w")) == NULL) {
					perror(optarg);
					complain("Cannot open DOT file");
					ret = 1;
					goto exit;
				}
			}
			break;
		case 'x': /* generate statistics DOT file */
			if(!strcmp(optarg, "")) {
				fpStatsDOT = stdout;
			} else {
				if((fpStatsDOT = fopen(optarg, "w")) == NULL) {
					perror(optarg);
					complain("Cannot open statistics DOT file");
					ret = 1;
					goto exit;
				}
			}
			break;
		case 'S': /* generate pdag statistic file */
			extendedStats = 1;
			/* INTENTIONALLY NO BREAK! - KEEP order! */
		case 's': /* generate pdag statistic file */
			if(!strcmp(optarg, "-")) {
				fpStats = stdout;
			} else {
				if((fpStats = fopen(optarg, "w")) == NULL) {
					perror(optarg);
					complain("Cannot open parser statistics file");
					ret = 1;
					goto exit;
				}
			}
			break;
		case 'v':
			verbose++;
			break;
		case 'E': /* encoder-specific format string (will be validated by encoder) */ 
			encFmt = es_newStrFromCStr(optarg, strlen(optarg));
			break;
		case 'p':
			recOutput = OUTPUT_PARSED_RECS;
			break;
		case 'P':
			recOutput = OUTPUT_UNPARSED_RECS;
			break;
		case 'L':
			addErrLineNbr = 1;
			break;
		case 'T':
			flatTags = 1;
			break;
		case 'e': /* encoder to use */
			if(!strcmp(optarg, "json")) {
				outfmt = f_json;
			} else if(!strcmp(optarg, "xml")) {
				outfmt = f_xml;
			} else if(!strcmp(optarg, "cee-syslog")) {
				outfmt = f_syslog;
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

	ln_setErrMsgCB(ctx, errCallBack, NULL);
	if(verbose) {
		ln_setDebugCB(ctx, dbgCallBack, NULL);
		ln_enableDebug(ctx, 1);
	}

	if(ln_loadSamples(ctx, repository)) {
		fprintf(stderr, "fatal error: cannot load rulebase\n");
		exit(1);
	}

	if(verbose > 0)
		fprintf(stderr, "number of tree nodes: %d\n", ctx->nNodes);

	if(fpDOT != NULL) {
		genDOT();
		ret=1;
		goto exit;
	}

#if 0
	if(fpStats != NULL) {
		ln_fullPdagStats(ctx, fpStats, extendedStats);
		ret=1;
		goto exit;
	}
#endif

	if(verbose > 2) ln_displayPDAG(ctx);

	normalize();

	if(fpStats != NULL) {
		ln_fullPdagStats(ctx, fpStats, extendedStats);
	}

	if(fpStatsDOT != NULL) {
		ln_fullPDagStatsDOT(ctx, fpStatsDOT);
	}

exit:
	if (ctx) ln_exitCtx(ctx);
	if (encFmt != NULL)
		free(encFmt);
	return ret;
}
