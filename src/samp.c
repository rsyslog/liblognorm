/* samp.c -- code for ln_samp objects.
 *
 * Copyright 2010-2015 by Rainer Gerhards and Adiscon GmbH.
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "json_compatibility.h"
#include "liblognorm.h"
#include "lognorm.h"
#include "samp.h"
#include "internal.h"
#include "parser.h"
#include "pdag.h"


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

static int
ln_parseLegacyFieldDescr(ln_ctx ctx,
	const char *const buf,
	const size_t lenBuf,
	size_t *bufOffs,
	es_str_t **str,
	json_object **prscnf)
{
	int r = 0;
	char *cstr;	/* for debug mode strings */
	char *ftype = NULL;
	char name[MAX_FIELDNAME_LEN];
	size_t iDst;
	struct json_object *json = NULL;
	char *ed = NULL;
	es_size_t i = *bufOffs;
	es_str_t *edata = NULL;

	for(  iDst = 0
	    ; iDst < (MAX_FIELDNAME_LEN - 1) && i < lenBuf && buf[i] != ':'
	    ; ++iDst) {
		name[iDst] = buf[i++];
	}
	name[iDst] = '\0';
	if(iDst == (MAX_FIELDNAME_LEN - 1)) {
		ln_errprintf(ctx, 0, "field name too long in: %s", buf+(*bufOffs));
		FAIL(LN_INVLDFDESCR);
	}
	if(i == lenBuf) {
		ln_errprintf(ctx, 0, "field definition wrong in: %s", buf+(*bufOffs));
		FAIL(LN_INVLDFDESCR);
	}

	if(iDst == 0) {
		FAIL(LN_INVLDFDESCR);
	}

	if(ctx->debug) {
		ln_dbgprintf(ctx, "parsed field: '%s'", name);
	}

	if(buf[i] != ':') {
		FAIL(LN_INVLDFDESCR);
	}
	++i; /* skip ':' */

	/* parse and process type (trailing whitespace must be trimmed) */
	es_emptyStr(*str);
 	size_t j = i;
	/* scan for terminator */
	while(j < lenBuf && buf[j] != ':' && buf[j] != '{' && buf[j] != '%')
		++j;
	/* now trim trailing space backwards */
	size_t next = j;
	--j;
	while(j >= i && isspace(buf[j]))
		--j;
	/* now copy */
	while(i <= j) {
		CHKR(es_addChar(str, buf[i++]));
	}
	/* finally move i to consumed position */
	i = next;

	if(i == lenBuf) {
		FAIL(LN_INVLDFDESCR);
	}

	ftype = es_str2cstr(*str, NULL);
	ln_dbgprintf(ctx, "field type '%s', i %d", ftype, i);

	if(buf[i] == '{') {
		struct json_tokener *tokener = json_tokener_new();
		json = json_tokener_parse_ex(tokener, buf+i, (int) (lenBuf - i));
		if(json == NULL) {
			ln_errprintf(ctx, 0, "invalid json in '%s'", buf+i);
		}
		i += tokener->char_offset;
		json_tokener_free(tokener);
	}

	if(buf[i] == '%') {
		i++;
	} else {
		/* parse extra data */
		CHKN(edata = es_newStr(8));
		i++;
		while(i < lenBuf) {
			if(buf[i] == '%') {
				++i;
				break; /* end of field */
			}
			CHKR(es_addChar(&edata, buf[i++]));
		}
		es_unescapeStr(edata);
		if(ctx->debug) {
			cstr = es_str2cstr(edata, NULL);
			ln_dbgprintf(ctx, "parsed extra data: '%s'", cstr);
			free(cstr);
		}
	}

	struct json_object *val;
	*prscnf = json_object_new_object();
	CHKN(val = json_object_new_string(name));
	json_object_object_add(*prscnf, "name", val);
	CHKN(val = json_object_new_string(ftype));
	json_object_object_add(*prscnf, "type", val);
	if(edata != NULL) {
		ed = es_str2cstr(edata, " ");
		CHKN(val = json_object_new_string(ed));
		json_object_object_add(*prscnf, "extradata", val);
	}
	if(json != NULL) {
		/* now we need to merge the json params into the main object */
		json_object_object_foreach(json, key, v) {
			json_object_get(v);
			json_object_object_add(*prscnf, key, v);
		}
	}

	*bufOffs = i;
done:
	free(ed);
	if(edata != NULL)
		es_deleteStr(edata);
	free(ftype);
	if(json != NULL)
		json_object_put(json);
	return r;
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
	CHKR(r);
 * 		  creation overhead
 * @returns 0 on success, something else otherwise
 */
static inline int
addFieldDescr(ln_ctx ctx, struct ln_pdag **pdag, es_str_t *rule,
	        size_t *bufOffs, es_str_t **str)
{
	int r = 0;
	es_size_t i = *bufOffs;
	char *ftype = NULL;
	const char *buf;
	es_size_t lenBuf;
	struct json_object *prs_config = NULL;

	buf = (const char*)es_getBufAddr(rule);
	lenBuf = es_strlen(rule);
	assert(buf[i] == '%');
	++i;	/* "eat" ':' */

	/* skip leading whitespace in field name */
	while(i < lenBuf && isspace(buf[i]))
		++i;
	/* check if we have new-style json config */
	if(buf[i] == '{' || buf[i] == '[') {
		struct json_tokener *tokener = json_tokener_new();
		prs_config = json_tokener_parse_ex(tokener, buf+i, (int) (lenBuf - i));
		i += tokener->char_offset;
		json_tokener_free(tokener);
		if(prs_config == NULL || i == lenBuf || buf[i] != '%') {
			ln_errprintf(ctx, 0, "invalid json in '%s'", buf+i);
			r = -1;
			goto done;
		}
		*bufOffs = i+1; /* eat '%' - if above ensures it is present */
	} else {
		*bufOffs = i;
		CHKR(ln_parseLegacyFieldDescr(ctx, buf, lenBuf, bufOffs, str, &prs_config));
	}

	CHKR(ln_pdagAddParser(ctx, pdag, prs_config));

done:
	free(ftype);
	return r;
}


/**
 *  Construct a literal parser json definition.
 */
static inline struct json_object *
newLiteralParserJSONConf(char lit)
{
	char buf[] = "x";
	buf[0] = lit;
	struct json_object *val;
	struct json_object *prscnf = json_object_new_object();

	val = json_object_new_string("literal");
	json_object_object_add(prscnf, "type", val);

	val = json_object_new_string(buf);
	json_object_object_add(prscnf, "text", val);

	return prscnf;
}

/**
 * Parse a Literal string out of the template and add it to the tree.
 * This function is used to create the unoptimized tree. So we do
 * one node for each character. These will be compacted by the optimizer
 * in a later stage. The advantage is that we do not need to care about
 * splitting the tree. As such the processing is fairly simple:
 *
 *   for each character in literal (left-to-right):
 *      create literal parser object o
 *      add new DAG node o, advance to it
 *
 * @param[in] ctx the context
 * @param[in/out] subtree on entry, current subtree, on exist newest
 *    		deepest subtree
 * @param[in] rule string with current rule
 * @param[in/out] bufOffs parse pointer, up to which offset is parsed
 * 		(is updated so that it points to first char after consumed
 * 		string on exit).
 * @param    str a work buffer, provided to prevent creation of a new object
 * @return 0 on success, something else otherwise
 */
static inline int
parseLiteral(ln_ctx ctx, struct ln_pdag **pdag, es_str_t *rule,
	     size_t *const __restrict__ bufOffs, es_str_t **str)
{
	int r = 0;
	size_t i = *bufOffs;
	unsigned char *buf = es_getBufAddr(rule);
	const size_t lenBuf = es_strlen(rule);
	const char *cstr = NULL;

	es_emptyStr(*str);
	while(i < lenBuf) {
		if(buf[i] == '%') {
			if(i+1 < lenBuf && buf[i+1] != '%') {
				break; /* field start is end of literal */
			}
			if (++i == lenBuf) break;
		}
		CHKR(es_addChar(str, buf[i]));
		++i;
	}

	es_unescapeStr(*str);
	cstr = es_str2cstr(*str, NULL);
	if(ctx->debug) {
		ln_dbgprintf(ctx, "parsed literal: '%s'", cstr);
	}

	*bufOffs = i;

	/* we now add the string to the tree */
	for(i = 0 ; cstr[i] != '\0' ; ++i) {
		struct json_object *const prscnf = 
			newLiteralParserJSONConf(cstr[i]);
		CHKN(prscnf);
		CHKR(ln_pdagAddParser(ctx, pdag, prscnf));
	}

	r = 0;

done:
	free((void*)cstr);
	return r;
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
 * @returns the new dag root (or NULL in case of error)
 */
static inline int
addSampToTree(ln_ctx ctx,
	es_str_t *rule,
	ln_pdag *dag,
	struct json_object *tagBucket)
{
	int r = -1;
	es_str_t *str = NULL;
	size_t i;

	CHKN(str = es_newStr(256));
	i = 0;
	while(i < es_strlen(rule)) {
		ln_dbgprintf(ctx, "addSampToTree %zu of %d", i, es_strlen(rule));
		CHKR(parseLiteral(ctx, &dag, rule, &i, &str));
		/* After the literal there can be field only*/
		if (i < es_strlen(rule)) {
			CHKR(addFieldDescr(ctx, &dag, rule, &i, &str));
			if (i == es_strlen(rule)) {
				/* finish the tree with empty literal to avoid false merging*/
				CHKR(parseLiteral(ctx, &dag, rule, &i, &str));
			}
		}
	}

	ln_dbgprintf(ctx, "end addSampToTree %zu of %d", i, es_strlen(rule));
	/* we are at the end of rule processing, so this node is a terminal */
	dag->flags.isTerminal = 1;
	dag->tags = tagBucket;

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
getLineType(const char *buf, es_size_t lenBuf, size_t *offs, es_str_t **str)
{
	int r = -1;
	size_t i;

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
getPrefix(const char *buf, es_size_t lenBuf, es_size_t offs, es_str_t **str)
{
	int r;

	if(*str == NULL) {
		CHKN(*str = es_newStr(lenBuf - offs));
	} else {
		es_emptyStr(*str);
	}

	r = es_addBuf(str, (char*)buf + offs, lenBuf - offs);
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
extendPrefix(ln_ctx ctx, const char *buf, es_size_t lenBuf, es_size_t offs)
{
	return es_addBuf(&ctx->rulePrefix, (char*)buf+offs, lenBuf - offs);
}


/**
 * Add a tag to the tag bucket. Helper to processTags.
 * @param[in] ctx current context
 * @param[in] tagname string with tag name
 * @param[out] tagBucket tagbucket to which new tags shall be added
 *                       the tagbucket is created if it is NULL
 * @returns 0 on success, something else otherwise
 */
static inline int
addTagStrToBucket(ln_ctx ctx, es_str_t *tagname, struct json_object **tagBucket)
{
	int r = -1;
	char *cstr;
	struct json_object *tag; 

	if(*tagBucket == NULL) {
		CHKN(*tagBucket = json_object_new_array());
	}
	cstr = es_str2cstr(tagname, NULL);
	ln_dbgprintf(ctx, "tag found: '%s'", cstr);
	CHKN(tag = json_object_new_string(cstr));
	json_object_array_add(*tagBucket, tag);
	free(cstr);
	r = 0;

done:	return r;
}


/**
 * Extract the tags and create a tag bucket out of them
 *
 * @param[in] ctx current context
 * @param[in] buf line buffer
 * @param[in] len length of buffer
 * @param[in,out] poffs offset where tags start, on exit and success
 *                      offset after tag part (excluding ':')
 * @param[out] tagBucket tagbucket to which new tags shall be added
 *                       the tagbucket is created if it is NULL
 * @returns 0 on success, something else otherwise
 */
static inline int
processTags(ln_ctx ctx, const char *buf, es_size_t lenBuf, es_size_t *poffs, struct json_object **tagBucket)
{
	int r = -1;
	es_str_t *str = NULL;
	es_size_t i;

	assert(poffs != NULL);
	i = *poffs;
	while(i < lenBuf && buf[i] != ':') {
		if(buf[i] == ',') {
			/* end of this tag */
			CHKR(addTagStrToBucket(ctx, str, tagBucket));
			es_deleteStr(str);
			str = NULL;
		} else {
			if(str == NULL) {
				CHKN(str = es_newStr(32));
			}
			CHKR(es_addChar(&str, buf[i]));
		}
		++i;
	}

	if(buf[i] != ':')
		goto done;
	++i; /* skip ':' */

	if(str != NULL) {
		CHKR(addTagStrToBucket(ctx, str, tagBucket));
		es_deleteStr(str);
	}

	*poffs = i;
	r = 0;

done:	return r;
}



/**
 * Process a new rule and add it to pdag.
 *
 * @param[in] ctx current context
 * @param[in] buf line buffer
 * @param[in] len length of buffer
 * @param[in] offs offset where rule starts
 * @returns 0 on success, something else otherwise
 */
static inline int
processRule(ln_ctx ctx, const char *buf, es_size_t lenBuf, es_size_t offs)
{
	int r = -1;
	es_str_t *str;
	struct json_object *tagBucket = NULL;

	ln_dbgprintf(ctx, "rule line to add: '%s'", buf+offs);
	CHKR(processTags(ctx, buf, lenBuf, &offs, &tagBucket));

	if(offs == lenBuf) {
		ln_errprintf(ctx, 0, "error: actual message sample part is missing");
		goto done;
	}
	if(ctx->rulePrefix == NULL) {
		CHKN(str = es_newStr(lenBuf));
	} else {
		CHKN(str = es_strdup(ctx->rulePrefix));
	}
	CHKR(es_addBuf(&str, (char*)buf + offs, lenBuf - offs));
	addSampToTree(ctx, str, ctx->pdag, tagBucket);
	es_deleteStr(str);
	r = 0;
done:	return r;
}


static inline int
getTypeName(ln_ctx ctx,
	const char *const __restrict__ buf,
	const size_t lenBuf,
	size_t *const __restrict__ offs,
	char *const __restrict__ dstbuf)
{
	int r = -1;
	size_t iDst;
	size_t i = *offs;
	
	if(buf[i] != '@') {
		ln_errprintf(ctx, 0, "user-defined type name must "
			"start with '@'");
		goto done;
	}
	for(  iDst = 0
	    ; i < lenBuf && buf[i] != ':' && iDst < MAX_TYPENAME_LEN - 1
	    ; ++i, ++iDst) {
		if(isspace(buf[i])) {
			ln_errprintf(ctx, 0, "user-defined type name must "
				"not contain whitespace");
			goto done;
		}
		dstbuf[iDst] = buf[i];
	}
	dstbuf[iDst] = '\0';

	if(i < lenBuf && buf[i] == ':') {
		r = 0,
		*offs = i+1; /* skip ":" */
	}
done:
	return r;
}

/**
 * Process a type definition and add it to the PDAG
 * disconnected components. 
 *
 * @param[in] ctx current context
 * @param[in] buf line buffer
 * @param[in] len length of buffer
 * @param[in] offs offset where rule starts
 * @returns 0 on success, something else otherwise
 */
static inline int
processType(ln_ctx ctx,
	const char *const __restrict__ buf,
	const size_t lenBuf,
	size_t offs)
{
	int r = -1;
	es_str_t *str;
	char typename[MAX_TYPENAME_LEN];

	ln_dbgprintf(ctx, "type line to add: '%s'", buf+offs);
	CHKR(getTypeName(ctx, buf, lenBuf, &offs, typename));
	ln_dbgprintf(ctx, "type name is '%s'", typename);

	ln_dbgprintf(ctx, "type line to add: '%s'", buf+offs);
	if(offs == lenBuf) {
		ln_errprintf(ctx, 0, "error: actual message sample part is missing in type def");
		goto done;
	}
	// TODO: optimize
	CHKN(str = es_newStr(lenBuf));
	CHKR(es_addBuf(&str, (char*)buf + offs, lenBuf - offs));
	struct ln_type_pdag *const td = ln_pdagFindType(ctx, typename, 1);
	CHKN(td);
	addSampToTree(ctx, str, td->pdag, NULL);
	es_deleteStr(str);
	r = 0;
done:	return r;
}


/**
 * Obtain a field name from a rule base line.
 *
 * @param[in] ctx current context
 * @param[in] buf line buffer
 * @param[in] len length of buffer
 * @param[in/out] offs on entry: offset where tag starts,
 * 		       on exit: updated offset AFTER TAG and (':')
 * @param [out] strTag obtained tag, if successful
 * @returns 0 on success, something else otherwise
 */
static inline int
getFieldName(ln_ctx __attribute__((unused)) ctx, const char *buf, es_size_t lenBuf, es_size_t *offs, es_str_t **strTag)
{
	int r = -1;
	es_size_t i;

	i = *offs;
	while(i < lenBuf &&
	       (isalnum(buf[i]) || buf[i] == '_' || buf[i] == '.')) {
		if(*strTag == NULL) {
			CHKN(*strTag = es_newStr(32));
		}
		CHKR(es_addChar(strTag, buf[i]));
		++i;
	}
	*offs = i;
	r = 0;
done:	return r;
}


/**
 * Skip over whitespace.
 * Skips any whitespace present at the offset.
 *
 * @param[in] ctx current context
 * @param[in] buf line buffer
 * @param[in] len length of buffer
 * @param[in/out] offs on entry: offset first unprocessed position
 */
static inline void
skipWhitespace(ln_ctx __attribute__((unused)) ctx, const char *buf, es_size_t lenBuf, es_size_t *offs)
{
	while(*offs < lenBuf && isspace(buf[*offs])) {
		(*offs)++;
	}
}


/**
 * Obtain an annotation (field) operation.
 * This usually is a plus or minus sign followed by a field name
 * followed (if plus) by an equal sign and the field value. On entry,
 * offs must be positioned on the first unprocessed field (after ':' for
 * the initial field!). Extra whitespace is detected and, if present,
 * skipped. The obtained operation is added to the annotation set provided.
 * Note that extracted string objects are passed to the annotation; thus it
 * is vital NOT to free them (most importantly, this is *not* a memory leak).
 *
 * @param[in] ctx current context
 * @param[in] annot active annotation set to which the operation is to be added
 * @param[in] buf line buffer
 * @param[in] len length of buffer
 * @param[in/out] offs on entry: offset where tag starts,
 * 		       on exit: updated offset AFTER TAG and (':')
 * @param [out] strTag obtained tag, if successful
 * @returns 0 on success, something else otherwise
 */
static inline int
getAnnotationOp(ln_ctx ctx, ln_annot *annot, const char *buf, es_size_t lenBuf, es_size_t *offs)
{
	int r = -1;
	es_size_t i;
	es_str_t *fieldName = NULL;
	es_str_t *fieldVal = NULL;
	ln_annot_opcode opc;

	i = *offs;
	skipWhitespace(ctx, buf, lenBuf, &i);
	if(i == lenBuf) {
		r = 0;
		goto done; /* nothing left to process (no error!) */
	}

	if(buf[i] == '+') {
		opc = ln_annot_ADD;
	} else if(buf[i] == '-') {
		ln_dbgprintf(ctx, "annotate op '-' not yet implemented - failing");
		goto fail;
	} else {
		ln_dbgprintf(ctx, "invalid annotate opcode '%c' - failing" , buf[i]);
		goto fail;
	}
	i++;

	if(i == lenBuf) goto fail; /* nothing left to process */

	CHKR(getFieldName(ctx, buf, lenBuf, &i, &fieldName));
	if(i == lenBuf) goto fail; /* nothing left to process */
	if(buf[i] != '=') goto fail; /* format error */
	i++;

	skipWhitespace(ctx, buf, lenBuf, &i);
	if(buf[i] != '"') goto fail; /* format error */
	++i;

	while(i < lenBuf && buf[i] != '"') {
		if(fieldVal == NULL) {
			CHKN(fieldVal = es_newStr(32));
		}
		CHKR(es_addChar(&fieldVal, buf[i]));
		++i;
	}
	*offs = (i == lenBuf) ? i : i+1;
	CHKR(ln_addAnnotOp(annot, opc, fieldName, fieldVal));
	r = 0;
done:	return r;
fail:	return -1;
}


/**
 * Process a new annotation and add it to the annotation set.
 *
 * @param[in] ctx current context
 * @param[in] buf line buffer
 * @param[in] len length of buffer
 * @param[in] offs offset where annotation starts
 * @returns 0 on success, something else otherwise
 */
static inline int
processAnnotate(ln_ctx ctx, const char *buf, es_size_t lenBuf, es_size_t offs)
{
	int r;
	es_str_t *tag = NULL;
	ln_annot *annot;

	ln_dbgprintf(ctx, "sample annotation to add: '%s'", buf+offs);
	CHKR(getFieldName(ctx, buf, lenBuf, &offs, &tag));
	skipWhitespace(ctx, buf, lenBuf, &offs);
	if(buf[offs] != ':' || tag == NULL) {
		ln_dbgprintf(ctx, "invalid tag field in annotation, line is '%s'", buf);
		r=-1;
		goto done;
	}
	++offs;

	/* we got an annotation! */
	CHKN(annot = ln_newAnnot(tag));

	while(offs < lenBuf) {
		CHKR(getAnnotationOp(ctx, annot, buf, lenBuf, &offs));
	}

	r = ln_addAnnotToSet(ctx->pas, annot);

done:	return r;
}

struct ln_samp *
ln_processSamp(ln_ctx ctx, const char *buf, const size_t lenBuf)
{
	struct ln_samp *samp = NULL;
	es_str_t *typeStr = NULL;
	size_t offs;

	if(getLineType(buf, lenBuf, &offs, &typeStr) != 0)
		goto done;

	if(!es_strconstcmp(typeStr, "prefix")) {
		if(getPrefix(buf, lenBuf, offs, &ctx->rulePrefix) != 0) goto done;
	} else if(!es_strconstcmp(typeStr, "extendprefix")) {
		if(extendPrefix(ctx, buf, lenBuf, offs) != 0) goto done;
	} else if(!es_strconstcmp(typeStr, "rule")) {
		if(processRule(ctx, buf, lenBuf, offs) != 0) goto done;
	} else if(!es_strconstcmp(typeStr, "type")) {
		if(processType(ctx, buf, lenBuf, offs) != 0) goto done;
	} else if(!es_strconstcmp(typeStr, "annotate")) {
		if(processAnnotate(ctx, buf, lenBuf, offs) != 0) goto done;
	} else {
		char *str;
		str = es_str2cstr(typeStr, NULL);
		ln_errprintf(ctx, 0, "invalid record type detected: '%s'", str);
		free(str);
		goto done;
	}

done:
	if(typeStr != NULL)
		es_deleteStr(typeStr);

	return samp;
}


struct ln_samp *
ln_sampRead(ln_ctx ctx, FILE *const __restrict__ repo, int *const __restrict__ isEof)
{
	struct ln_samp *samp = NULL;
	char buf[64*1024]; /**< max size of rule - TODO: make configurable */

	int linenbr = 1;
	size_t i = 0;
	int inParser = 0;
	int done = 0;
	while(!done) {
		int c = fgetc(repo);
		if(c == EOF) {
			*isEof = 1;
			goto done;
		} else if(c == '\n') {
			++linenbr;
			if(!inParser && i != 0)
				done = 1;
		} else if(c == '#' && i == 0) {
			/* note: comments are only supported at beginning of line! */
			/* skip to end of line */
			do {
				c = fgetc(repo);
			} while(c != EOF && c != '\n');
			++linenbr;
			i = 0; /* back to beginning */
		} else {
			if(c == '%')
				inParser = (inParser) ? 0 : 1;
			buf[i++] = c;
			if(i >= sizeof(buf)) {
				ln_errprintf(ctx, 0, "line %d is too long", linenbr);
				goto done;
			}
		}
	}
	buf[i] = '\0';

	ln_dbgprintf(ctx, "read rule base line: '%s'", buf);
	ln_processSamp(ctx, buf, i);

done:
	return samp;
}
