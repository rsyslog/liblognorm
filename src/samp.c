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
#include <assert.h>
#include <libee/libee.h>
#include <libee/primitivetype.h>

#include "liblognorm.h"
#include "lognorm.h"
#include "samp.h"
#include "internal.h"

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

done:	return samp;
}

void
ln_sampFree(ln_ctx __attribute__((unused)) ctx, struct ln_samp *samp)
{
	free(samp);
}


/**
 * Extract a field description from a sample.
 * The field description is added to the tail of the current
 * subtree's field list. The parse buffer must be position on the
 * leading '%' that starts a field definition. It is a program error
 * if this condition is not met.
 *
 * Note that we break up the object model and access ptree members
 * directly. Let's consider us a friend of ptree. This is necessary
 * to optimize the structure for a high-speed parsing process.
 *
 * @param[in] str a temporary work string. This is passed in to save the
 * 		  creation overhead
 * @returns 0 on success, something else otherwise
 */
static inline int
parseFieldDescr(ln_ctx ctx, struct ln_ptree **subtree, es_str_t *rule,
	        es_size_t *bufOffs, es_str_t **str)
{
	int r;
	ln_fieldList_t *node;
	es_size_t i = *bufOffs;
	char *cstr;	/* for debug mode strings */
	unsigned char *buf;
	es_size_t lenBuf;

	assert(subtree != NULL);

	buf = es_getBufAddr(rule);
	lenBuf = es_strlen(rule);
	assert(buf[i] == '%');
	++i;	/* "eat" ':' */
	CHKN(node = malloc(sizeof(ln_fieldList_t)));
	node->subtree = NULL;
	node->next = NULL;
	node->data = NULL;
	CHKN(node->name = es_newStr(16));

	while(i < lenBuf && buf[i] != ':') {
		CHKR(es_addChar(&node->name, buf[i++]));
	}

	if(es_strlen(node->name) == 0) {
		FAIL(LN_INVLDFDESCR);
	} 

	if(ctx->debug) {
		cstr = es_str2cstr(node->name, NULL);
		ln_dbgprintf(ctx, "parsed field: '%s'", cstr);
		free(cstr);
	}

	if(buf[i] != ':') {
		/* may be valid later if we have a loaded CEE dictionary
		 * and the name is present inside it.
		 */
		FAIL(LN_INVLDFDESCR);
	}
	++i; /* skip ':' */

	/* parse and process type
	 * Note: if we have a CEE dictionary, this part can be optional!
	 */
	es_emptyStr(*str);
	while(i < lenBuf && buf[i] != ':' && buf[i] != '%') {
		CHKR(es_addChar(str, buf[i++]));
	}

	if(i == lenBuf) {
		FAIL(LN_INVLDFDESCR);
	}

	if(!es_strconstcmp(*str, "date-rfc3164")) {
		node->parser = ee_parseRFC3164Date;
	} else if(!es_strconstcmp(*str, "number")) {
		node->parser = ee_parseNumber;
	} else if(!es_strconstcmp(*str, "ipv4")) {
		node->parser = ee_parseIPv4;
	} else if(!es_strconstcmp(*str, "word")) {
		node->parser = ee_parseWord;
	} else if(!es_strconstcmp(*str, "char-to")) {
		// TODO: check extra data!!!! (very important)
		node->parser = ee_parseCharTo;
	} else {
		cstr = es_str2cstr(*str, NULL);
		ln_dbgprintf(ctx, "ERROR: invalid field type '%s'", cstr);
		free(cstr);
		FAIL(LN_INVLDFDESCR);
	}

	if(buf[i] == '%') {
		i++;
	} else {
		/* parse extra data */
		CHKN(node->data = es_newStr(8));
		i++;
		while(i < lenBuf) {
			if(buf[i] == '%') {
				++i;
				if(i == lenBuf || buf[i] != '%') {
					break; /* end of field */
				}
			}
			CHKR(es_addChar(&node->data, buf[i++]));
		}
		es_unescapeStr(node->data);
		if(ctx->debug) {
			cstr = es_str2cstr(node->data, NULL);
			ln_dbgprintf(ctx, "parsed extra data: '%s'", cstr);
			free(cstr);
		}
	}


	/* finished */
	CHKR(ln_addFDescrToPTree(subtree, node));
	*bufOffs = i;
	r = 0;

done:	return r;
}


/**
 * Parse a Literal string out of the template and add it to the tree.
 * @param[in] ctx the context
 * @param[in/out] subtree on entry, current subtree, on exist newest
 *    		deepest subtree
 * @param[in] rule string with current rule
 * @param[in/out] bufOffs parse pointer, up to which offset is parsed
 * 		(is updated so that it points to first char after consumed
 * 		string on exit).
 * @param[out] str literal extracted (is empty, when no litral could be found)
 * @return 0 on success, something else otherwise
 */
static inline int
parseLiteral(ln_ctx ctx, struct ln_ptree **subtree, es_str_t *rule,
	     es_size_t *bufOffs, es_str_t **str)
{
	int r;
	es_size_t i = *bufOffs;
	unsigned char *buf;
	es_size_t lenBuf;

	es_emptyStr(*str);
	buf = es_getBufAddr(rule);
	lenBuf = es_strlen(rule);
	/* extract maximum length literal */
	while(i < lenBuf) {
		if(buf[i] == '%') {
			if(i+1 < lenBuf && buf[i+1] != '%') {
				break; /* field start is end of literal */
			}
			++i;
		}
		CHKR(es_addChar(str, buf[i]));
		++i;
	}
	if(es_strlen(*str) == 0)
		goto done;

	es_unescapeStr(*str);
	if(ctx->debug) {
		char *cstr = es_str2cstr(*str, NULL);
		ln_dbgprintf(ctx, "parsed literal: '%s'", cstr);
		free(cstr);
	}

	*subtree = ln_buildPTree(*subtree, *str, 0);
	*bufOffs = i;
	r = 0;

done:	return r;
}


/* Implementation note:
 * We read in the sample, and split it into chunks of literal text and
 * fields. Each literal text is added as whole to the tree, as is each
 * field individually. To do so, we keep track of our current subtree 
 * root, which changes whenever a new part of the tree is build. It is
 * set to the then-lowest part of the tree, where the next step sample
 * data is to be added.
 * 
 * This function processes the whole string or returns an error.
 *
 * format: literal1%field:type:extra-data%literal2
 *
 * @returns the new subtree root (or NULL in case of error)
 */
static inline int
addSampToTree(ln_ctx ctx, es_str_t *rule)
{
	int r;
	struct ln_ptree* subtree;
	es_str_t *str = NULL;
	es_size_t i;

	subtree = ctx->ptree;
	CHKN(str = es_newStr(256));
	i = 0;
	while(i < es_strlen(rule)) {
		CHKR(parseLiteral(ctx, &subtree, rule, &i, &str));
		if(es_strlen(str) == 0) {
			/* we had no literal, so let's parse a field description */
			CHKR(parseFieldDescr(ctx, &subtree, rule, &i, &str));
		}
	}

done:
	if(str != NULL)
		es_deleteStr(str);
	return r;
}



/**
 * get the initial word of a rule line that tells us the type of the
 * line.
 * @param[in] buf line buffer
 * @param[in] len length of buffer
 * @param[out] offs offset after "="
 * @param[out] str string with "linetype-word" (newly created)
 * @returns 0 on success, something else otherwise
 */
static inline int
getLineType(char *buf, es_size_t lenBuf, es_size_t *offs, es_str_t **str)
{
	int r;
	es_size_t i;

	*str = es_newStr(16);
	for(i = 0 ; i < lenBuf && buf[i] != '=' ; ++i) {
		CHKR(es_addChar(str, buf[i]));
	}

	if(i < lenBuf)
		++i; /* skip over '=' */
	*offs = i;

done:	return r;
}


/**
 * Get a new common prefix from the config file. That is actually everything from
 * the current offset to the end of line.
 *
 * @param[in] buf line buffer
 * @param[in] len length of buffer
 * @param[in] offs offset after "="
 * @param[in/out] str string to store common offset. If NULL, it is created,
 * 	 	otherwise it is emptied.
 * @returns 0 on success, something else otherwise
 */
static inline int
getPrefix(char *buf, es_size_t lenBuf, es_size_t offs, es_str_t **str)
{
	int r;

	if(*str == NULL) {
		CHKN(*str = es_newStr(lenBuf - offs));
	} else {
		es_emptyStr(*str);
	}
		
	r = es_addBuf(str, buf + offs, lenBuf - offs);
done:	return r;
}


/**
 * Extend the common prefix. This means that the line is concatenated
 * to the prefix. This is useful if the same rulebase is to be used with
 * different prefixes (well, not strictly necessary, but probably useful).
 *
 * @param[in] ctx current context
 * @param[in] buf line buffer
 * @param[in] len length of buffer
 * @param[in] offs offset to-be-added text starts
 * @returns 0 on success, something else otherwise
 */
static inline int
extendPrefix(ln_ctx ctx, char *buf, es_size_t lenBuf, es_size_t offs)
{
	return es_addBuf(&ctx->rulePrefix, buf+offs, lenBuf - offs);
}


/**
 * Process a new rule and add it to tree.
 *
 * @param[in] ctx current context
 * @param[in] buf line buffer
 * @param[in] len length of buffer
 * @param[in] offs offset where rule starts
 * @returns 0 on success, something else otherwise
 */
static inline int
processRule(ln_ctx ctx, char *buf, es_size_t lenBuf, es_size_t offs)
{
	int r = -1;
	es_str_t *str;

	ln_dbgprintf(ctx, "sample line to add; '%s'\n", buf+offs);
	/* skip tags, which we do not currently support (as we miss libcee) */
	for( ; offs < lenBuf && buf[offs] != ':' ; )
		offs++;
	if(offs == lenBuf) {
		ln_dbgprintf(ctx, "error, tag part is missing");
		// TODO: provide some error indicator to app? We definitely must do (a callback?)
		goto done;
	}

	++offs;
	if(offs == lenBuf) {
		ln_dbgprintf(ctx, "error, actual message sample part is missing");
		// TODO: provide some error indicator to app? We definitely must do (a callback?)
		goto done;
	}
	if(ctx->rulePrefix == NULL) {
		CHKN(str = es_newStr(lenBuf));
	} else {
		CHKN(str = es_strdup(ctx->rulePrefix));
	}
	CHKR(es_addBuf(&str, buf + offs, lenBuf - offs));
	addSampToTree(ctx, str);
	es_deleteStr(str);
	r = 0;
done:	return r;
}


struct ln_samp *
ln_sampRead(ln_ctx ctx, struct ln_sampRepos *repo, int *isEof)
{
	struct ln_samp *samp = NULL;
	es_size_t offs;
	int done = 0;
	char buf[10*1024]; /**< max size of rule */ // TODO: make configurable
	es_size_t lenBuf;
	es_str_t *typeStr = NULL;

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

	if(getLineType(buf, lenBuf, &offs, &typeStr) != 0) goto done;
	if(!es_strconstcmp(typeStr, "prefix")) {
		if(getPrefix(buf, lenBuf, offs, &ctx->rulePrefix) != 0) goto done;
	} else if(!es_strconstcmp(typeStr, "extendprefix")) {
		if(extendPrefix(ctx, buf, lenBuf, offs) != 0) goto done;
	} else if(!es_strconstcmp(typeStr, "rule")) {
		if(processRule(ctx, buf,lenBuf, offs) != 0) goto done;
	} else {
		// TODO error reporting
		goto done;
	}


	//ln_displayPTree(ctx, ctx->ptree, 0);
done:
	if(typeStr != NULL)
		es_deleteStr(typeStr);
	return samp;
}
