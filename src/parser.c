/*
 * liblognorm - a fast samples-based log normalization library
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
#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "json_compatibility.h"
#include "liblognorm.h"
#include "lognorm.h"
#include "internal.h"
#include "parser.h"
#include "samp.h"

#ifdef FEATURE_REGEXP
#include <pcre.h>
#include <errno.h>			
#endif

/* some helpers */
static inline int
hParseInt(const unsigned char **buf, size_t *lenBuf)
{
	const unsigned char *p = *buf;
	size_t len = *lenBuf;
	int i = 0;

	while(len > 0 && isdigit(*p)) {
		i = i * 10 + *p - '0';
		++p;
		--len;
	}

	*buf = p;
	*lenBuf = len;
	return i;
}

/* parsers for the primitive types
 *
 * All parsers receive 
 *
 * @param[in] str the to-be-parsed string
 * @param[in] strLen length of the to-be-parsed string
 * @param[in] offs an offset into the string
 * @param[in] node fieldlist with additional data; for simple
 *            parsers, this sets variable "ed", which just is
 *            string data.
 * @param[out] parsed bytes
 * @param[out] json object containing parsed data (can be unused)
 *
 * They will try to parse out "their" object from the string. If they
 * succeed, they:
 *
 * return 0 on success and LN_WRONGPARSER if this parser could
 *           not successfully parse (but all went well otherwise) and something
 *           else in case of an error.
 */
#define BEGINParser(ParserName) \
int ln_parse##ParserName(const char *str, size_t strLen, size_t *offs,       \
						__attribute__((unused)) const ln_fieldList_t *node,  \
						size_t *parsed,                                      \
						 __attribute__((unused)) struct json_object **value) \
{ \
	int r = LN_WRONGPARSER; \
	__attribute__((unused)) es_str_t *ed = node->data;  \
	*parsed = 0;

#define FAILParser \
	goto done; /* suppress warnings */ \
done: \
	r = 0; \
	goto fail; /* suppress warnings */ \
fail: 

#define ENDFailParser \
	return r; \
}

#define ENDParser \
	goto done; /* suppress warnings */ \
done: \
	r = 0; \
	goto fail; /* suppress warnings */ \
fail: \
	return r; \
}

/**
 * Utilities to allow constructors of complex parser's to
 *  easily process field-declaration arguments.
 */
#define FIELD_ARG_SEPERATOR ":"
#define MAX_FIELD_ARGS 10

struct pcons_args_s {
	int argc;
	char *argv[MAX_FIELD_ARGS];
};

typedef struct pcons_args_s pcons_args_t;

static void free_pcons_args(pcons_args_t** dat_p) {
	pcons_args_t *dat = *dat_p;
	while((--(dat->argc)) >= 0) {
		if (dat->argv[dat->argc] != NULL) free(dat->argv[dat->argc]);
	}
	free(dat);
	*dat_p = NULL;
}

static pcons_args_t* pcons_args(es_str_t *args, int expected_argc) {
	pcons_args_t *dat = NULL;
	char* orig_str = NULL;
	if ((dat = malloc(sizeof(pcons_args_t))) == NULL) goto fail;
	dat->argc = 0;
	if (args != NULL) {
		orig_str = es_str2cstr(args, NULL);
		char *str = orig_str;
		while (dat->argc < MAX_FIELD_ARGS) {
			int i = dat->argc++;
			char *next = (dat->argc == expected_argc) ? NULL : strstr(str, FIELD_ARG_SEPERATOR);
			if (next == NULL) {
				if ((dat->argv[i] = strdup(str)) == NULL) goto fail;
				break;
			} else {
				if ((dat->argv[i] = strndup(str, next - str)) == NULL) goto fail;
				next++;
			}
			str = next;
		}
	}
	goto done;
fail:
	if (dat != NULL) free_pcons_args(&dat);
done:
	if (orig_str != NULL) free(orig_str);
	return dat;
}

static const char* pcons_arg(pcons_args_t *dat, int i, const char* dflt_val) {
	if (i >= dat->argc) return dflt_val;
	return dat->argv[i];
}

static char* pcons_arg_copy(pcons_args_t *dat, int i, const char* dflt_val) {
	const char *str = pcons_arg(dat, i, dflt_val);
	return (str == NULL) ? NULL : strdup(str);
}

static void pcons_unescape_arg(pcons_args_t *dat, int i) {
	char *arg = (char*) pcons_arg(dat, i, NULL);
	es_str_t *str = NULL;
	if (arg != NULL) {
		str = es_newStrFromCStr(arg, strlen(arg));
		if (str != NULL) {
			es_unescapeStr(str);
			free(arg);
			dat->argv[i] = es_str2cstr(str, NULL);
			es_deleteStr(str);
		}
	}
}

/**
 * Parse a TIMESTAMP as specified in RFC5424 (subset of RFC3339).
 */
BEGINParser(RFC5424Date)
	const unsigned char *pszTS;
	/* variables to temporarily hold time information while we parse */
	__attribute__((unused)) int year;
	int month;
	int day;
	int hour; /* 24 hour clock */
	int minute;
	int second;
	__attribute__((unused)) int secfrac;	/* fractional seconds (must be 32 bit!) */
	__attribute__((unused)) int secfracPrecision;
	__attribute__((unused)) char OffsetMode;	/* UTC offset + or - */
	char OffsetHour;	/* UTC offset in hours */
	int OffsetMinute;	/* UTC offset in minutes */
	size_t len;
	size_t orglen;
	/* end variables to temporarily hold time information while we parse */

	pszTS = (unsigned char*) str + *offs;
	len = orglen = strLen - *offs;

	year = hParseInt(&pszTS, &len);

	/* We take the liberty to accept slightly malformed timestamps e.g. in 
	 * the format of 2003-9-1T1:0:0.  */
	if(len == 0 || *pszTS++ != '-') goto fail;
	--len;
	month = hParseInt(&pszTS, &len);
	if(month < 1 || month > 12) goto fail;

	if(len == 0 || *pszTS++ != '-')
		goto fail;
	--len;
	day = hParseInt(&pszTS, &len);
	if(day < 1 || day > 31) goto fail;

	if(len == 0 || *pszTS++ != 'T') goto fail;
	--len;

	hour = hParseInt(&pszTS, &len);
	if(hour < 0 || hour > 23) goto fail;

	if(len == 0 || *pszTS++ != ':')
		goto fail;
	--len;
	minute = hParseInt(&pszTS, &len);
	if(minute < 0 || minute > 59) goto fail;

	if(len == 0 || *pszTS++ != ':') goto fail;
	--len;
	second = hParseInt(&pszTS, &len);
	if(second < 0 || second > 60) goto fail;

	/* Now let's see if we have secfrac */
	if(len > 0 && *pszTS == '.') {
		--len;
		const unsigned char *pszStart = ++pszTS;
		secfrac = hParseInt(&pszTS, &len);
		secfracPrecision = (int) (pszTS - pszStart);
	} else {
		secfracPrecision = 0;
		secfrac = 0;
	}

	/* check the timezone */
	if(len == 0) goto fail;

	if(*pszTS == 'Z') {
		--len;
		pszTS++; /* eat Z */
		OffsetMode = 'Z';
		OffsetHour = 0;
		OffsetMinute = 0;
	} else if((*pszTS == '+') || (*pszTS == '-')) {
		OffsetMode = *pszTS;
		--len;
		pszTS++;

		OffsetHour = hParseInt(&pszTS, &len);
		if(OffsetHour < 0 || OffsetHour > 23)
			goto fail;

		if(len == 0 || *pszTS++ != ':')
			goto fail;
		--len;
		OffsetMinute = hParseInt(&pszTS, &len);
		if(OffsetMinute < 0 || OffsetMinute > 59)
			goto fail;
	} else {
		/* there MUST be TZ information */
		goto fail;
	}

	if(len > 0) {
		if(*pszTS != ' ') /* if it is not a space, it can not be a "good" time */
			goto fail;
	}

	/* we had success, so update parse pointer */
	*parsed = orglen - len;

ENDParser


/**
 * Parse a RFC3164 Date.
 */
BEGINParser(RFC3164Date)
	const unsigned char *p;
	size_t len, orglen;
	/* variables to temporarily hold time information while we parse */
	__attribute__((unused)) int month;
	int day;
	//int year = 0; /* 0 means no year provided */
	int hour; /* 24 hour clock */
	int minute;
	int second;

	p = (unsigned char*) str + *offs;
	orglen = len = strLen - *offs;
	/* If we look at the month (Jan, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec),
	 * we may see the following character sequences occur:
	 *
	 * J(an/u(n/l)), Feb, Ma(r/y), A(pr/ug), Sep, Oct, Nov, Dec
	 *
	 * We will use this for parsing, as it probably is the
	 * fastest way to parse it.
	 */
	if(len < 3)
		goto fail;

	switch(*p++)
	{
	case 'j':
	case 'J':
		if(*p == 'a' || *p == 'A') {
			++p;
			if(*p == 'n' || *p == 'N') {
				++p;
				month = 1;
			} else
				goto fail;
		} else if(*p == 'u' || *p == 'U') {
			++p;
			if(*p == 'n' || *p == 'N') {
				++p;
				month = 6;
			} else if(*p == 'l' || *p == 'L') {
				++p;
				month = 7;
			} else
				goto fail;
		} else
			goto fail;
		break;
	case 'f':
	case 'F':
		if(*p == 'e' || *p == 'E') {
			++p;
			if(*p == 'b' || *p == 'B') {
				++p;
				month = 2;
			} else
				goto fail;
		} else
			goto fail;
		break;
	case 'm':
	case 'M':
		if(*p == 'a' || *p == 'A') {
			++p;
			if(*p == 'r' || *p == 'R') {
				++p;
				month = 3;
			} else if(*p == 'y' || *p == 'Y') {
				++p;
				month = 5;
			} else
				goto fail;
		} else
			goto fail;
		break;
	case 'a':
	case 'A':
		if(*p == 'p' || *p == 'P') {
			++p;
			if(*p == 'r' || *p == 'R') {
				++p;
				month = 4;
			} else
				goto fail;
		} else if(*p == 'u' || *p == 'U') {
			++p;
			if(*p == 'g' || *p == 'G') {
				++p;
				month = 8;
			} else
				goto fail;
		} else
			goto fail;
		break;
	case 's':
	case 'S':
		if(*p == 'e' || *p == 'E') {
			++p;
			if(*p == 'p' || *p == 'P') {
				++p;
				month = 9;
			} else
				goto fail;
		} else
			goto fail;
		break;
	case 'o':
	case 'O':
		if(*p == 'c' || *p == 'C') {
			++p;
			if(*p == 't' || *p == 'T') {
				++p;
				month = 10;
			} else
				goto fail;
		} else
			goto fail;
		break;
	case 'n':
	case 'N':
		if(*p == 'o' || *p == 'O') {

			++p;
			if(*p == 'v' || *p == 'V') {
				++p;
				month = 11;
			} else
				goto fail;
		} else
			goto fail;
		break;
	case 'd':
	case 'D':
		if(*p == 'e' || *p == 'E') {
			++p;
			if(*p == 'c' || *p == 'C') {
				++p;
				month = 12;
			} else
				goto fail;
		} else
			goto fail;
		break;
	default:
		goto fail;
	}

	len -= 3;
	
	/* done month */

	if(len == 0 || *p++ != ' ')
		goto fail;
	--len;

	/* we accept a slightly malformed timestamp with one-digit days. */
	if(*p == ' ') {
		--len;
		++p;
	}

	day = hParseInt(&p, &len);
	if(day < 1 || day > 31)
		goto fail;

	if(len == 0 || *p++ != ' ')
		goto fail;
	--len;

	/* time part */
	hour = hParseInt(&p, &len);
	if(hour > 1970 && hour < 2100) {
		/* if so, we assume this actually is a year. This is a format found
		 * e.g. in Cisco devices.
		 *
		year = hour;
		*/

		/* re-query the hour, this time it must be valid */
		if(len == 0 || *p++ != ' ')
			goto fail;
		--len;
		hour = hParseInt(&p, &len);
	}

	if(hour < 0 || hour > 23)
		goto fail;

	if(len == 0 || *p++ != ':')
		goto fail;
	--len;
	minute = hParseInt(&p, &len);
	if(minute < 0 || minute > 59)
		goto fail;

	if(len == 0 || *p++ != ':')
		goto fail;
	--len;
	second = hParseInt(&p, &len);
	if(second < 0 || second > 60)
		goto fail;

	/* we provide support for an extra ":" after the date. While this is an
	 * invalid format, it occurs frequently enough (e.g. with Cisco devices)
	 * to permit it as a valid case. -- rgerhards, 2008-09-12
	 */
	if(len > 0 && *p == ':') {
		++p; /* just skip past it */
		--len;
	}

	/* we had success, so update parse pointer */
	*parsed = orglen - len;

ENDParser


/**
 * Parse a Number.
 * Note that a number is an abstracted concept. We always represent it
 * as 64 bits (but may later change our mind if performance dictates so).
 */
BEGINParser(Number)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;

	for (i = *offs; i < strLen && isdigit(c[i]); i++);
	if (i == *offs)
		goto fail;
	
	/* success, persist */
	*parsed = i - *offs;

ENDParser

/**
 * Parse a Real-number in floating-pt form.
 */
BEGINParser(Float)
const char *c;
size_t i;

assert(str != NULL);
assert(offs != NULL);
assert(parsed != NULL);
c = str;

int seen_point = 0;

i = *offs;

if (c[i] == '-') i++; 

for (; i < strLen; i++) {
	if (c[i] == '.') {
		if (seen_point != 0) break;
		seen_point = 1;
	} else if (! isdigit(c[i])) {
		break;
	} 
}
if (i == *offs)
	goto fail;
	
/* success, persist */
*parsed = i - *offs;

ENDParser


/**
 * Parse a hex Number.
 * A hex number begins with 0x and contains only hex digits until the terminating
 * whitespace. Note that if a non-hex character is deteced inside the number string,
 * this is NOT considered to be a number.
 */
BEGINParser(HexNumber)
	const char *c;
	size_t i = *offs;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;

	if(c[i] != '0' || c[i+1] != 'x')
		goto fail;

	for (i += 2 ; i < strLen && isxdigit(c[i]); i++);
	if (i == *offs || !isspace(c[i]))
		goto fail;
	
	/* success, persist */
	*parsed = i - *offs;

ENDParser


/**
 * Parse whitespace.
 * This parses all whitespace until the first non-whitespace character
 * is found. This is primarily a tool to skip to the next "word" if
 * the exact number of whitspace characters (and type of whitespace)
 * is not known. The current parsing position MUST be on a whitspace,
 * else the parser does not match.
 * This parser is also a forward-compatibility tool for the upcoming
 * slsa (simple log structure analyser) tool.
 */
BEGINParser(Whitespace)
	const char *c;
	size_t i = *offs;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;

	if(!isspace(c[i]))
		goto fail;

	for (i++ ; i < strLen && isspace(c[i]); i++);
	/* success, persist */
	*parsed = i - *offs;
ENDParser


/**
 * Parse a word.
 * A word is a SP-delimited entity. The parser always works, except if
 * the offset is position on a space upon entry.
 */
BEGINParser(Word)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	/* search end of word */
	while(i < strLen && c[i] != ' ') 
		i++;

	if(i == *offs) {
		goto fail;
	}

	/* success, persist */
	*parsed = i - *offs;

ENDParser


/**
 * Parse everything up to a specific string.
 * swisskid, 2015-01-21
 */
BEGINParser(StringTo)
	const char *c;
	const char *toFind;
	size_t i, j, k, m;
	int chkstr;
	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	assert(ed != NULL);
	k = es_strlen(ed) - 1;
	toFind = es_str2cstr(ed, NULL);
	c = str;
	i = *offs;
	chkstr = 0;

	/* Total hunt for letter */
	while(chkstr == 0 && i < strLen ) {
	    i++;
	    if(c[i] == toFind[0]) {
		/* Found the first letter, now find the rest of the string */
		j = 0;
		m = i;
		while(m < strLen && j < k ) {
		    m++;
		    j++;
		    if(c[m] != toFind[j])
			break;
		    if (j == k) 
			chkstr = 1;
		}
	    }
	}
	if(i == *offs || i == strLen || c[i] != toFind[0]) {
		r = LN_WRONGPARSER;
		goto fail;
	} 

	/* success, persist */
	*parsed = i - *offs;

ENDParser
/**
 * Parse a alphabetic word.
 * A alpha word is composed of characters for which isalpha returns true.
 * The parser fails if there is no alpha character at all.
 */
BEGINParser(Alpha)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	/* search end of word */
	while(i < strLen && isalpha(c[i])) 
		i++;

	if(i == *offs) {
		goto fail;
	}

	/* success, persist */
	*parsed = i - *offs;

ENDParser


/**
 * Parse everything up to a specific character.
 * The character must be the only char inside extra data passed to the parser.
 * It is a program error if strlen(ed) != 1. It is considered a format error if
 * a) the to-be-parsed buffer is already positioned on the terminator character
 * b) there is no terminator until the end of the buffer
 * In those cases, the parsers declares itself as not being successful, in all
 * other cases a string is extracted.
 */
BEGINParser(CharTo)
	const char *c;
	unsigned char cTerm;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	assert(es_strlen(ed) == 1);
	cTerm = *(es_getBufAddr(ed));
	c = str;
	i = *offs;

	/* search end of word */
	while(i < strLen && c[i] != cTerm) 
		i++;

	if(i == *offs || i == strLen || c[i] != cTerm) {
		r = LN_WRONGPARSER;
		goto fail;
	}

	/* success, persist */
	*parsed = i - *offs;

ENDParser


/**
 * Parse everything up to a specific character, or up to the end of string.
 * The character must be the only char inside extra data passed to the parser.
 * It is a program error if strlen(ed) != 1.
 * This parser always returns success.
 * By nature of the parser, it is required that end of string or the separator
 * follows this field in rule.
 */
BEGINParser(CharSeparated)
	const char *c;
	unsigned char cTerm;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	assert(es_strlen(ed) == 1);
	cTerm = *(es_getBufAddr(ed));
	c = str;
	i = *offs;

	/* search end of word */
	while(i < strLen && c[i] != cTerm) 
		i++;

	/* success, persist */
	*parsed = i - *offs;

ENDParser

/**
 * Parse yet-to-be-matched portion of string by re-applying
 * top-level rules again. 
 */
#define DEFAULT_REMAINING_FIELD_NAME "tail"

struct recursive_parser_data_s {
	ln_ctx ctx;
	char* remaining_field;
	int free_ctx;
};

BEGINParser(Recursive)
	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);

	struct recursive_parser_data_s* pData = (struct recursive_parser_data_s*) node->parser_data;

	if (pData != NULL) {
		int remaining_len = strLen - *offs;
		const char *remaining_str = str + *offs;
		json_object *unparsed = NULL;
		CHKN(*value = json_object_new_object());

		ln_normalize(pData->ctx, remaining_str, remaining_len, value);

		if (json_object_object_get_ex(*value, UNPARSED_DATA_KEY, &unparsed)) {
			json_object_put(*value);
			*value = NULL;
			*parsed = 0;
		} else if (pData->remaining_field != NULL && json_object_object_get_ex(*value, pData->remaining_field, &unparsed)) {
			*parsed = strLen - *offs - json_object_get_string_len(unparsed);
			json_object_object_del(*value, pData->remaining_field);
		} else {
			*parsed = strLen - *offs;
		}
	}
ENDParser

typedef ln_ctx (ctx_constructor)(ln_ctx, pcons_args_t*, const char*);

static void* _recursive_parser_data_constructor(ln_fieldList_t *node, ln_ctx ctx, int no_of_args, int remaining_field_arg_idx,
												int free_ctx, ctx_constructor *fn) {
	int r = LN_BADCONFIG;
	char* name = NULL;
	struct recursive_parser_data_s *pData = NULL;
	pcons_args_t *args = NULL;
	CHKN(name = es_str2cstr(node->name, NULL));
	CHKN(pData = calloc(1, sizeof(struct recursive_parser_data_s)));
	pData->free_ctx = free_ctx;
	pData->remaining_field = NULL;
	CHKN(args = pcons_args(node->raw_data, no_of_args));
	CHKN(pData->ctx = fn(ctx, args, name));
	CHKN(pData->remaining_field = pcons_arg_copy(args, remaining_field_arg_idx, DEFAULT_REMAINING_FIELD_NAME));
	r = 0;
done:
	if (r != 0) {
		if (name == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for recursive/descent field name");
		else if (pData == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for parser-data for field: %s", name);
		else if (args == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for argument-parsing for field: %s", name);
		else if (pData->ctx == NULL) ln_dbgprintf(ctx, "recursive/descent normalizer context creation failed for field: %s", name);
		else if (pData->remaining_field == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for remaining-field name for "
															  "recursive/descent field: %s", name);

		recursive_parser_data_destructor((void**) &pData);
	}
	free(name);
	free_pcons_args(&args);
	return pData;
}

static ln_ctx identity_recursive_parse_ctx_constructor(ln_ctx parent_ctx,
													   __attribute__((unused)) pcons_args_t* args,
													   __attribute__((unused)) const char* field_name) {
	return parent_ctx;
}

void* recursive_parser_data_constructor(ln_fieldList_t *node, ln_ctx ctx) {
	return _recursive_parser_data_constructor(node, ctx, 1, 0, 0, identity_recursive_parse_ctx_constructor);
}

static ln_ctx child_recursive_parse_ctx_constructor(ln_ctx parent_ctx, pcons_args_t* args, const char* field_name) {
	int r = LN_BADCONFIG;
	const char* rb = NULL;
	ln_ctx ctx = NULL;
	pcons_unescape_arg(args, 0);
	CHKN(rb = pcons_arg(args, 0, NULL));
	CHKN(ctx = ln_inherittedCtx(parent_ctx));
	CHKR(ln_loadSamples(ctx, rb));
done:
	if (r != 0) {
		if (rb == NULL) ln_dbgprintf(parent_ctx, "file-name for descent rulebase not provided for field: %s", field_name);
		else if (ctx == NULL) ln_dbgprintf(parent_ctx, "couldn't allocate memory to create descent-field normalizer context "
										   "for field: %s", field_name);
		else ln_dbgprintf(parent_ctx, "couldn't load samples into descent context for field: %s", field_name);
		if (ctx != NULL) ln_exitCtx(ctx);
		ctx = NULL;
	}
	return ctx;
}

void* descent_parser_data_constructor(ln_fieldList_t *node, ln_ctx ctx) {
	return _recursive_parser_data_constructor(node, ctx, 2, 1, 1, child_recursive_parse_ctx_constructor);
}

void recursive_parser_data_destructor(void** dataPtr) {
	if (*dataPtr != NULL) {
		struct recursive_parser_data_s *pData = (struct recursive_parser_data_s*) *dataPtr;
		if (pData->free_ctx && pData->ctx != NULL) {
			ln_exitCtx(pData->ctx);
			pData->ctx = NULL;
		}
		if (pData->remaining_field != NULL) free(pData->remaining_field);
		free(pData);
		*dataPtr = NULL;
	}
};

/**
 * Parse string tokenized by given char-sequence
 * The sequence may appear 0 or more times, but zero times means 1 token.
 * NOTE: its not 0 tokens, but 1 token.
 *
 * The token found is parsed according to the field-type provided after
 *  tokenizer char-seq.
 */
#define DEFAULT_MATCHED_FIELD_NAME "default"

struct tokenized_parser_data_s {
	es_str_t *tok_str;
	ln_ctx ctx;
	char *remaining_field;
	int use_default_field;
	int free_ctx;
};

typedef struct tokenized_parser_data_s tokenized_parser_data_t;

BEGINParser(Tokenized) {
	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);

	tokenized_parser_data_t *pData = (tokenized_parser_data_t*) node->parser_data;

	if (pData != NULL ) {
		json_object *json_p = NULL;
		if (pData->use_default_field) CHKN(json_p = json_object_new_object());
		json_object *matches = NULL;
		CHKN(matches = json_object_new_array());

		int remaining_len = strLen - *offs;
		const char *remaining_str = str + *offs;
		json_object *remaining = NULL;
		json_object *match = NULL;

		while (remaining_len > 0) {
			if (! pData->use_default_field) {
				json_object_put(json_p);
				json_p = json_object_new_object();
			} //TODO: handle null condition gracefully

			ln_normalize(pData->ctx, remaining_str, remaining_len, &json_p);

			if (remaining) json_object_put(remaining);

			if (pData->use_default_field && json_object_object_get_ex(json_p, DEFAULT_MATCHED_FIELD_NAME, &match)) {
				json_object_array_add(matches, json_object_get(match));
			} else if (! (pData->use_default_field || json_object_object_get_ex(json_p, UNPARSED_DATA_KEY, &match))) {
				json_object_array_add(matches, json_object_get(json_p));
			} else {
				if (json_object_array_length(matches) > 0) {
					remaining_len += es_strlen(pData->tok_str);
					break;
				} else {
					json_object_put(json_p);
					json_object_put(matches);
					FAIL(LN_WRONGPARSER);
				}
			}

			if (json_object_object_get_ex(json_p, pData->remaining_field, &remaining)) {
				remaining_len = json_object_get_string_len(remaining);
				if (remaining_len > 0) {
					remaining_str = json_object_get_string(json_object_get(remaining));
					json_object_object_del(json_p, pData->remaining_field);
					if (es_strbufcmp(pData->tok_str, (const unsigned char *)remaining_str, es_strlen(pData->tok_str))) {
						json_object_put(remaining);
						break;
					} else {
						remaining_str += es_strlen(pData->tok_str);
						remaining_len -= es_strlen(pData->tok_str);
					}
				}
			} else {
				remaining_len = 0;
				break;
			}

			if (pData->use_default_field) json_object_object_del(json_p, DEFAULT_MATCHED_FIELD_NAME);
		}
		json_object_put(json_p);

		/* success, persist */
		*parsed = (strLen - *offs) - remaining_len;
		*value =  matches;
	} else {
		FAIL(LN_BADPARSERSTATE);
	}

} ENDParser

void tokenized_parser_data_destructor(void** dataPtr) {
	tokenized_parser_data_t *data = (tokenized_parser_data_t*) *dataPtr;
	if (data->tok_str != NULL) es_deleteStr(data->tok_str);
	if (data->free_ctx && (data->ctx != NULL)) ln_exitCtx(data->ctx);
	if (data->remaining_field != NULL) free(data->remaining_field);
	free(data);
	*dataPtr = NULL;
}

static void load_generated_parser_samples(ln_ctx ctx,
	const char* const field_descr, const int field_descr_len,
	const char* const suffix, const int length) {
	static const char* const RULE_PREFIX = "rule=:%"DEFAULT_MATCHED_FIELD_NAME":";//TODO: extract nice constants
	static const int RULE_PREFIX_LEN = 15;

	char *sample_str = NULL;
	es_str_t *field_decl = es_newStrFromCStr(RULE_PREFIX, RULE_PREFIX_LEN);
	if (! field_decl) goto free;

	if (es_addBuf(&field_decl, field_descr, field_descr_len)
		|| es_addBuf(&field_decl, "%", 1)
		|| es_addBuf(&field_decl, suffix, length)) {
		ln_dbgprintf(ctx, "couldn't prepare field for tokenized field-picking: '%s'", field_descr);
		goto free;
	}
	sample_str = es_str2cstr(field_decl, NULL);
	if (! sample_str) {
		ln_dbgprintf(ctx, "couldn't prepare sample-string for: '%s'", field_descr);
		goto free;
	}
	ln_loadSample(ctx, sample_str);
free:
	if (sample_str) free(sample_str);
	if (field_decl) es_deleteStr(field_decl);
}

static ln_ctx generate_context_with_field_as_prefix(ln_ctx parent, const char* field_descr, int field_descr_len) {
	int r = LN_BADCONFIG;
	const char* remaining_field = "%"DEFAULT_REMAINING_FIELD_NAME":rest%";
	ln_ctx ctx = NULL;
	CHKN(ctx = ln_inherittedCtx(parent));
	load_generated_parser_samples(ctx, field_descr, field_descr_len, remaining_field, strlen(remaining_field));
	load_generated_parser_samples(ctx, field_descr, field_descr_len, "", 0);
	r = 0;
done:
	if (r != 0) {
		ln_exitCtx(ctx);
		ctx = NULL;
	}
	return ctx;
}

static ln_fieldList_t* parse_tokenized_content_field(ln_ctx ctx, const char* field_descr, size_t field_descr_len) {
	es_str_t* tmp = NULL;
	es_str_t* descr = NULL;
	ln_fieldList_t *node = NULL;
	int r = 0;
	CHKN(tmp = es_newStr(80));
	CHKN(descr = es_newStr(80));
	const char* field_prefix = "%" DEFAULT_MATCHED_FIELD_NAME ":";
	CHKR(es_addBuf(&descr, field_prefix, strlen(field_prefix)));
	CHKR(es_addBuf(&descr, field_descr, field_descr_len));
	CHKR(es_addChar(&descr, '%'));
	es_size_t offset = 0;
	CHKN(node = ln_parseFieldDescr(ctx, descr, &offset, &tmp, &r));
	if (offset != es_strlen(descr)) FAIL(LN_BADPARSERSTATE);
done:
	if (r != 0) {
		if (node != NULL) ln_deletePTreeNode(node);
		node = NULL;
	}
	if (descr != NULL) es_deleteStr(descr);
	if (tmp != NULL) es_deleteStr(tmp);
	return node;
}

void* tokenized_parser_data_constructor(ln_fieldList_t *node, ln_ctx ctx) {
	int r = LN_BADCONFIG;
	char* name = es_str2cstr(node->name, NULL);
	pcons_args_t *args = NULL;
	tokenized_parser_data_t *pData = NULL;
	const char *field_descr = NULL;
	ln_fieldList_t* field = NULL;
	const char *tok = NULL;

	CHKN(args = pcons_args(node->raw_data, 2));

	CHKN(pData = calloc(1, sizeof(tokenized_parser_data_t)));
	pcons_unescape_arg(args, 0);
	CHKN(tok = pcons_arg(args, 0, NULL));
	CHKN(pData->tok_str = es_newStrFromCStr(tok, strlen(tok)));
	es_unescapeStr(pData->tok_str);
	CHKN(field_descr = pcons_arg(args, 1, NULL));
	const int field_descr_len = strlen(field_descr);
	pData->free_ctx = 1;
	CHKN(field = parse_tokenized_content_field(ctx, field_descr, field_descr_len));
	if (field->parser == ln_parseRecursive) {
		pData->use_default_field = 0;
		struct recursive_parser_data_s *dat = (struct recursive_parser_data_s*) field->parser_data;
		if (dat != NULL) {
			CHKN(pData->remaining_field = strdup(dat->remaining_field));
			pData->free_ctx = dat->free_ctx;
			pData->ctx = dat->ctx;
			dat->free_ctx = 0;
		}
	} else {
		pData->use_default_field = 1;
		CHKN(pData->ctx = generate_context_with_field_as_prefix(ctx, field_descr, field_descr_len));
	}
	if (pData->remaining_field == NULL) CHKN(pData->remaining_field = strdup(DEFAULT_REMAINING_FIELD_NAME));
	r = 0;
done:
	if (r != 0) {
		if (name == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for tokenized-field name");
		else if (args == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for argument-parsing for field: %s", name);
		else if (pData == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for parser-data for field: %s", name);
		else if (tok == NULL) ln_dbgprintf(ctx, "token-separator not provided for field: %s", name);
		else if (pData->tok_str == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for token-separator for field: %s", name);
		else if (field_descr == NULL) ln_dbgprintf(ctx, "field-type not provided for field: %s", name);
		else if (field == NULL) ln_dbgprintf(ctx, "couldn't resolve single-token field-type for tokenized field: %s", name);
		else if (pData->ctx == NULL) ln_dbgprintf(ctx, "couldn't initialize normalizer-context for field: %s", name);
		else if (pData->remaining_field == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for "
															  "remaining-field-name for field: %s", name);
		if (pData) tokenized_parser_data_destructor((void**) &pData);
	}
	if (name != NULL) free(name);
	if (field != NULL) ln_deletePTreeNode(field);
	if (args) free_pcons_args(&args);
	return pData;
}

#ifdef FEATURE_REGEXP

/**
 * Parse string matched by provided posix extended regex.
 *
 * Please note that using regex field in most cases will be
 * significantly slower than other field-types.
 */
struct regex_parser_data_s {
	pcre *re;
	int consume_group;
	int return_group;
	int max_groups;
};

BEGINParser(Regex)
	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	unsigned int* ovector = NULL;

	struct regex_parser_data_s *pData = (struct regex_parser_data_s*) node->parser_data;
	if (pData != NULL) {
		ovector = calloc(pData->max_groups, sizeof(int) * 3);
		if (ovector == NULL) FAIL(LN_NOMEM);

		int result = pcre_exec(pData->re, NULL,	str, strLen, *offs, 0, (int*) ovector, pData->max_groups * 3);
		if (result == 0) result = pData->max_groups;
		if (result > pData->consume_group) {
			//please check 'man 3 pcreapi' for cryptic '2 * n' and '2 * n + 1' magic
			if (ovector[2 * pData->consume_group] == *offs) {
				*parsed = ovector[2 * pData->consume_group + 1] - ovector[2 * pData->consume_group];
				if (pData->consume_group != pData->return_group) {
					char* val = NULL;
					CHKN(val = strndup(str + ovector[2 * pData->return_group],
						ovector[2 * pData->return_group + 1] - ovector[2 * pData->return_group]));
					*value = json_object_new_string(val);
					free(val);
					if (*value == NULL) {
						free(ovector);
						FAIL(LN_NOMEM);
					}
				}
			}
		}
		free(ovector);
	}
ENDParser

static const char* regex_parser_configure_consume_and_return_group(pcons_args_t* args, struct regex_parser_data_s *pData) {
	const char* consume_group_parse_error = "couldn't parse consume-group number";
	const char* return_group_parse_error = "couldn't parse return-group number";

	char* tmp = NULL;

	const char* consume_grp_str = NULL;
	const char* return_grp_str = NULL;

	if ((consume_grp_str = pcons_arg(args, 1, "0")) == NULL ||
		strlen(consume_grp_str) == 0) return consume_group_parse_error;
	if ((return_grp_str = pcons_arg(args, 2, consume_grp_str)) == NULL ||
		strlen(return_grp_str) == 0) return return_group_parse_error;

	errno = 0;
	pData->consume_group = strtol(consume_grp_str, &tmp, 10);
	if (errno != 0 || strlen(tmp) != 0) return consume_group_parse_error;

	pData->return_group = strtol(return_grp_str, &tmp, 10);
	if (errno != 0 || strlen(tmp) != 0) return return_group_parse_error;

	return NULL;
}

void* regex_parser_data_constructor(ln_fieldList_t *node, ln_ctx ctx) {
	int r = LN_BADCONFIG;
	char* exp = NULL;
	const char* grp_parse_err = NULL;
	pcons_args_t* args = NULL;
	char* name = NULL;
	struct regex_parser_data_s *pData = NULL;
	const char *unescaped_exp = NULL;
	const char *error = NULL;
	int erroffset = 0;


	CHKN(name = es_str2cstr(node->name, NULL));

	if (! ctx->allowRegex) FAIL(LN_BADCONFIG);
	CHKN(pData = malloc(sizeof(struct regex_parser_data_s)));
	pData->re = NULL;

	CHKN(args = pcons_args(node->raw_data, 3));
	pData->consume_group = pData->return_group = 0;
	CHKN(unescaped_exp = pcons_arg(args, 0, NULL));
	pcons_unescape_arg(args, 0);
	CHKN(exp = pcons_arg_copy(args, 0, NULL));

	if ((grp_parse_err = regex_parser_configure_consume_and_return_group(args, pData)) != NULL) FAIL(LN_BADCONFIG);

	CHKN(pData->re = pcre_compile(exp, 0, &error, &erroffset, NULL));

	pData->max_groups = ((pData->consume_group > pData->return_group) ? pData->consume_group : pData->return_group) + 1;
	r = 0;
done:
	if (r != 0) {
		if (name == NULL) ln_dbgprintf(ctx, "couldn't allocate memory regex-field name");
		else if (! ctx->allowRegex) ln_dbgprintf(ctx, "regex support is not enabled for: '%s' "
												 "(please check lognorm context initialization)", name);
		else if (pData == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for parser-data for field: %s", name);
		else if (args == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for argument-parsing for field: %s", name);
		else if (unescaped_exp == NULL) ln_dbgprintf(ctx, "regular-expression missing for field: '%s'", name);
		else if (exp == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for regex-string for field: '%s'", name);
		else if (grp_parse_err != NULL)  ln_dbgprintf(ctx, "%s for: '%s'", grp_parse_err, name);
		else if (pData->re == NULL)	ln_dbgprintf(ctx, "couldn't compile regex(encountered error '%s' at char '%d' in pattern) "
												 "for regex-matched field: '%s'", error, erroffset, name);
		regex_parser_data_destructor((void**)&pData);
	}
	if (exp != NULL) free(exp);
	if (args != NULL) free_pcons_args(&args);
	if (name != NULL) free(name);
	return pData;
}

void regex_parser_data_destructor(void** dataPtr) {
	if ((*dataPtr) != NULL) {
		struct regex_parser_data_s *pData = (struct regex_parser_data_s*) *dataPtr;
		if (pData->re != NULL) pcre_free(pData->re);
		free(pData);
		*dataPtr = NULL;
	}
}

#endif

/**
 * Parse yet-to-be-matched portion of string by re-applying
 * top-level rules again. 
 */
typedef enum interpret_type {
	/* If you change this, be sure to update json_type_to_name() too */
	it_b10int,
	it_b16int,
	it_floating_pt,
	it_boolean
} interpret_type;

struct interpret_parser_data_s {
	ln_ctx ctx;
	enum interpret_type intrprt;
};

static json_object* interpret_as_int(json_object *value, int base) {
	if (json_object_is_type(value, json_type_string)) {
		return json_object_new_int64(strtol(json_object_get_string(value), NULL, base));
	} else if (json_object_is_type(value, json_type_int)) {
		return value;
	} else {
		return NULL;
	}
}

static json_object* interpret_as_double(json_object *value) {
	double val = json_object_get_double(value);
	return json_object_new_double(val);
}

static json_object* interpret_as_boolean(json_object *value) {
	json_bool val;
	if (json_object_is_type(value, json_type_string)) {
		const char* str = json_object_get_string(value);
		val = (strcasecmp(str, "false") == 0 || strcasecmp(str, "no") == 0) ? 0 : 1;
	} else {
		val = json_object_get_boolean(value);
	}
	return json_object_new_boolean(val);
}

static int reinterpret_value(json_object **value, enum interpret_type to_type) {
	switch(to_type) {
	case it_b10int:
		*value = interpret_as_int(*value, 10);
		break;
	case it_b16int:
		*value = interpret_as_int(*value, 16);
		break;
	case it_floating_pt:
		*value = interpret_as_double(*value);
		break;
	case it_boolean:
		*value = interpret_as_boolean(*value);
		break;
	default:
		return 0;
	}
	return 1;
}

BEGINParser(Interpret)
	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	json_object *unparsed = NULL;
	json_object *parsed_raw = NULL;

	struct interpret_parser_data_s* pData = (struct interpret_parser_data_s*) node->parser_data;

	if (pData != NULL) {
		int remaining_len = strLen - *offs;
		const char *remaining_str = str + *offs;

		CHKN(parsed_raw = json_object_new_object());

		ln_normalize(pData->ctx, remaining_str, remaining_len, &parsed_raw);

		if (json_object_object_get_ex(parsed_raw, UNPARSED_DATA_KEY, NULL)) {
			*parsed = 0;
		} else {
			json_object_object_get_ex(parsed_raw, DEFAULT_MATCHED_FIELD_NAME, value);
			json_object_object_get_ex(parsed_raw, DEFAULT_REMAINING_FIELD_NAME, &unparsed);
			if (reinterpret_value(value, pData->intrprt)) {
				*parsed = strLen - *offs - json_object_get_string_len(unparsed);
			}
		}
		json_object_put(parsed_raw);
	}
ENDParser

void* interpret_parser_data_constructor(ln_fieldList_t *node, ln_ctx ctx) {
	int r = LN_BADCONFIG;
	char* name = NULL;
	struct interpret_parser_data_s *pData = NULL;
	pcons_args_t *args = NULL;
	int bad_interpret = 0;
	const char* type_str = NULL;
	const char *field_type = NULL;
	CHKN(name = es_str2cstr(node->name, NULL));
	CHKN(pData = calloc(1, sizeof(struct interpret_parser_data_s)));
	CHKN(args = pcons_args(node->raw_data, 2));
	CHKN(type_str = pcons_arg(args, 0, NULL));
	if (strcmp(type_str, "int") == 0 || strcmp(type_str, "base10int") == 0) {
		pData->intrprt = it_b10int;
	} else if (strcmp(type_str, "base16int") == 0) {
		pData->intrprt = it_b16int;
	} else if (strcmp(type_str, "float") == 0) {
		pData->intrprt = it_floating_pt;
	} else if (strcmp(type_str, "bool") == 0) {
		pData->intrprt = it_boolean;
	} else {
		bad_interpret = 1;
		FAIL(LN_BADCONFIG);
	}

	CHKN(field_type = pcons_arg(args, 1, NULL));
	CHKN(pData->ctx = generate_context_with_field_as_prefix(ctx, field_type, strlen(field_type)));
	r = 0;
done:
	if (r != 0) {
		if (name == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for interpret-field name");
		else if (pData == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for parser-data for field: %s", name);
		else if (args == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for argument-parsing for field: %s", name);
		else if (type_str == NULL) ln_dbgprintf(ctx, "no type provided for interpretation of field: %s", name);
		else if (bad_interpret != 0) ln_dbgprintf(ctx, "interpretation to unknown type '%s' requested for field: %s",
												  type_str, name);
		else if (field_type == NULL) ln_dbgprintf(ctx, "field-type to actually match the content not provided for "
												  "field: %s", name);
		else if (pData->ctx == NULL) ln_dbgprintf(ctx, "couldn't instantiate the normalizer context for matching "
												  "field: %s", name);

		interpret_parser_data_destructor((void**) &pData);
	}
	free(name);
	free_pcons_args(&args);
	return pData;
}

void interpret_parser_data_destructor(void** dataPtr) {
	if (*dataPtr != NULL) {
		struct interpret_parser_data_s *pData = (struct interpret_parser_data_s*) *dataPtr;
		if (pData->ctx != NULL) ln_exitCtx(pData->ctx);
		free(pData);
		*dataPtr = NULL;
	}
};

/**
 * Parse suffixed char-sequence, where suffix is one of many possible suffixes.
 */
struct suffixed_parser_data_s {
	int nsuffix;
	int *suffix_offsets;
    int *suffix_lengths;
	char* suffixes_str;
	ln_ctx ctx;
	char* value_field_name;
	char* suffix_field_name;
};

BEGINParser(Suffixed) {
	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	json_object *unparsed = NULL;
	json_object *parsed_raw = NULL;
	json_object *parsed_value = NULL;
    json_object *result = NULL;
    json_object *suffix = NULL;

	struct suffixed_parser_data_s *pData = (struct suffixed_parser_data_s*) node->parser_data;

	if (pData != NULL) {
		int remaining_len = strLen - *offs;
		const char *remaining_str = str + *offs;
		int i;

		CHKN(parsed_raw = json_object_new_object());

		ln_normalize(pData->ctx, remaining_str, remaining_len, &parsed_raw);

		if (json_object_object_get_ex(parsed_raw, UNPARSED_DATA_KEY, NULL)) {
			*parsed = 0;
		} else {
			json_object_object_get_ex(parsed_raw, DEFAULT_MATCHED_FIELD_NAME, &parsed_value);
			json_object_object_get_ex(parsed_raw, DEFAULT_REMAINING_FIELD_NAME, &unparsed);
            const char* unparsed_frag = json_object_get_string(unparsed);
			for(i = 0; i < pData->nsuffix; i++) {
                const char* possible_suffix = pData->suffixes_str + pData->suffix_offsets[i];
				int len = pData->suffix_lengths[i];
				if (strncmp(possible_suffix, unparsed_frag, len) == 0) {
                    CHKN(result = json_object_new_object());
                    CHKN(suffix = json_object_new_string(possible_suffix));
					json_object_get(parsed_value);
                    json_object_object_add(result, pData->value_field_name, parsed_value);
                    json_object_object_add(result, pData->suffix_field_name, suffix);
					*parsed = strLen - *offs - json_object_get_string_len(unparsed) + len;
                    break;
                }
			}
            if (result != NULL) {
                *value = result;
            }
		}
	}
FAILParser
	if (r != 0) {
		if (result != NULL) json_object_put(result);
	}
	if (parsed_raw != NULL) json_object_put(parsed_raw);
} ENDFailParser

static struct suffixed_parser_data_s* _suffixed_parser_data_constructor(ln_fieldList_t *node,
																 ln_ctx ctx,
																 es_str_t* raw_args,
																 const char* value_field,
																 const char* suffix_field) {
	int r = LN_BADCONFIG;
	pcons_args_t* args = NULL;
	char* name = NULL;
	struct suffixed_parser_data_s *pData = NULL;
	const char *escaped_tokenizer = NULL;
	const char *uncopied_suffixes_str = NULL;
	const char *tokenizer = NULL;
	char *suffixes_str = NULL;
	const char *field_type = NULL;

	char *tok_saveptr = NULL;
	char *tok_input = NULL;
	int i = 0;
	char *tok = NULL;

	CHKN(name = es_str2cstr(node->name, NULL));

	CHKN(pData = calloc(1, sizeof(struct suffixed_parser_data_s)));

	if (value_field == NULL) value_field = "value";
	if (suffix_field == NULL) suffix_field = "suffix";
	pData->value_field_name = strdup(value_field);
	pData->suffix_field_name = strdup(suffix_field);

	CHKN(args = pcons_args(raw_args, 3));
	CHKN(escaped_tokenizer = pcons_arg(args, 0, NULL));
	pcons_unescape_arg(args, 0);
	CHKN(tokenizer = pcons_arg(args, 0, NULL));

	CHKN(uncopied_suffixes_str = pcons_arg(args, 1, NULL));
	pcons_unescape_arg(args, 1);
	CHKN(suffixes_str = pcons_arg_copy(args, 1, NULL));

	tok_input = suffixes_str;
	while (strtok_r(tok_input, tokenizer, &tok_saveptr) != NULL) {
		tok_input = NULL;
		pData->nsuffix++;
	}

	CHKN(pData->suffix_offsets = calloc(pData->nsuffix, sizeof(int)));
    CHKN(pData->suffix_lengths = calloc(pData->nsuffix, sizeof(int)));
	CHKN(pData->suffixes_str = pcons_arg_copy(args, 1, NULL));

	tok_input = pData->suffixes_str;
	while ((tok = strtok_r(tok_input, tokenizer, &tok_saveptr)) != NULL) {
		tok_input = NULL;
		pData->suffix_offsets[i] = tok - pData->suffixes_str;
        pData->suffix_lengths[i++] = strlen(tok);
	}

	CHKN(field_type = pcons_arg(args, 2, NULL));
	CHKN(pData->ctx = generate_context_with_field_as_prefix(ctx, field_type, strlen(field_type)));
	
	r = 0;
done:
	if (r != 0) {
		if (name == NULL) ln_dbgprintf(ctx, "couldn't allocate memory suffixed-field name");
		else if (pData == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for parser-data for field: %s", name);
		else if (pData->value_field_name == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for value-field's name for field: %s", name);
		else if (pData->suffix_field_name == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for suffix-field's name for field: %s", name);
		else if (args == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for argument-parsing for field: %s", name);
		else if (escaped_tokenizer == NULL) ln_dbgprintf(ctx, "suffix token-string missing for field: '%s'", name);
		else if (tokenizer == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for unescaping token-string for field: '%s'", name);
		else if (uncopied_suffixes_str == NULL)  ln_dbgprintf(ctx, "suffix-list missing for field: '%s'", name);
		else if (suffixes_str == NULL)  ln_dbgprintf(ctx, "couldn't allocate memory for suffix-list for field: '%s'", name);
		else if (pData->suffix_offsets == NULL)
			ln_dbgprintf(ctx, "couldn't allocate memory for suffix-list element references for field: '%s'", name);
		else if (pData->suffix_lengths == NULL)
			ln_dbgprintf(ctx, "couldn't allocate memory for suffix-list element lengths for field: '%s'", name);
		else if (pData->suffixes_str == NULL)
			ln_dbgprintf(ctx, "couldn't allocate memory for suffix-list for field: '%s'", name);
		else if (field_type == NULL)  ln_dbgprintf(ctx, "field-type declaration missing for field: '%s'", name);
		else if (pData->ctx == NULL)  ln_dbgprintf(ctx, "couldn't allocate memory for normalizer-context for field: '%s'", name);
		suffixed_parser_data_destructor((void**)&pData);
	}
	free_pcons_args(&args);
	if (suffixes_str != NULL) free(suffixes_str);
	if (name != NULL) free(name);
	return pData;
}

void* suffixed_parser_data_constructor(ln_fieldList_t *node, ln_ctx ctx) {
	return _suffixed_parser_data_constructor(node, ctx, node->raw_data, NULL, NULL);
}

void* named_suffixed_parser_data_constructor(ln_fieldList_t *node, ln_ctx ctx) {
	int r = LN_BADCONFIG;
	pcons_args_t* args = NULL;
	char* name = NULL;
	const char* value_field_name = NULL;
	const char* suffix_field_name = NULL;
	const char* remaining_args = NULL;
	es_str_t* unnamed_suffix_args = NULL;
	struct suffixed_parser_data_s* pData = NULL;
	CHKN(name = es_str2cstr(node->name, NULL));
	CHKN(args = pcons_args(node->raw_data, 3));
	CHKN(value_field_name = pcons_arg(args, 0, NULL));
	CHKN(suffix_field_name = pcons_arg(args, 1, NULL));
	CHKN(remaining_args = pcons_arg(args, 2, NULL));
	CHKN(unnamed_suffix_args = es_newStrFromCStr(remaining_args, strlen(remaining_args)));
	
	CHKN(pData = _suffixed_parser_data_constructor(node, ctx, unnamed_suffix_args, value_field_name, suffix_field_name));
	r = 0;
done:
	if (r != 0) {
		if (name == NULL) ln_dbgprintf(ctx, "couldn't allocate memory named_suffixed-field name");
		else if (args == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for argument-parsing for field: %s", name);
		else if (value_field_name == NULL) ln_dbgprintf(ctx, "key-name for value not provided for field: %s", name);
		else if (suffix_field_name == NULL) ln_dbgprintf(ctx, "key-name for suffix not provided for field: %s", name);
		else if (unnamed_suffix_args == NULL) ln_dbgprintf(ctx, "couldn't allocate memory for unnamed-suffix-field args for field: %s", name);
		else if (pData == NULL) ln_dbgprintf(ctx, "couldn't create parser-data for field: %s", name);
		suffixed_parser_data_destructor((void**)&pData);
	}
	if (unnamed_suffix_args != NULL) free(unnamed_suffix_args);
	if (args != NULL) free_pcons_args(&args);
	if (name != NULL) free(name);
	return pData;
}


void suffixed_parser_data_destructor(void** dataPtr) {
	if ((*dataPtr) != NULL) {
		struct suffixed_parser_data_s *pData = (struct suffixed_parser_data_s*) *dataPtr;
		if (pData->suffixes_str != NULL) free(pData->suffixes_str);
		if (pData->suffix_offsets != NULL) free(pData->suffix_offsets);
		if (pData->suffix_lengths != NULL) free(pData->suffix_lengths);
		if (pData->value_field_name != NULL) free(pData->value_field_name);
		if (pData->suffix_field_name != NULL) free(pData->suffix_field_name);
		if (pData->ctx != NULL)	ln_exitCtx(pData->ctx);
		free(pData);
		*dataPtr = NULL;
	}
}

/**
 * Just get everything till the end of string.
 */
BEGINParser(Rest)

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);

	/* silence the warning about unused variable */
	(void)str;
	/* success, persist */
	*parsed = strLen - *offs;

ENDParser

/**
 * Parse a possibly quoted string. In this initial implementation, escaping of the quote
 * char is not supported. A quoted string is one start starts with a double quote,
 * has some text (not containing double quotes) and ends with the first double
 * quote character seen. The extracted string does NOT include the quote characters.
 * swisskid, 2015-01-21
 */
BEGINParser(OpQuotedString)
	const char *c;
	size_t i;
	char *cstr;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	if(c[i] != '"') {
		while(i < strLen && c[i] != ' ') 
			i++;

		if(i == *offs) {
			goto fail;
		}

		/* success, persist */
		*parsed = i - *offs;
		/* create JSON value to save quoted string contents */
		CHKN(cstr = strndup((char*)c + *offs, *parsed));
	} else {
	    ++i;

	    /* search end of string */
	    while(i < strLen && c[i] != '"') 
		    i++;

	    if(i == strLen || c[i] != '"') {
		    r = LN_WRONGPARSER;
		    goto fail;
	    }
	    /* success, persist */
	    *parsed = i + 1 - *offs; /* "eat" terminal double quote */
	    /* create JSON value to save quoted string contents */
	    CHKN(cstr = strndup((char*)c + *offs + 1, *parsed - 2));
	}
	CHKN(*value = json_object_new_string(cstr));
	free(cstr);

ENDParser

/**
 * Parse a quoted string. In this initial implementation, escaping of the quote
 * char is not supported. A quoted string is one start starts with a double quote,
 * has some text (not containing double quotes) and ends with the first double
 * quote character seen. The extracted string does NOT include the quote characters.
 * rgerhards, 2011-01-14
 */
BEGINParser(QuotedString)
	const char *c;
	size_t i;
	char *cstr;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;
	if(i + 2 > strLen)
		goto fail;	/* needs at least 2 characters */

	if(c[i] != '"')
		goto fail;
	++i;

	/* search end of string */
	while(i < strLen && c[i] != '"') 
		i++;

	if(i == strLen || c[i] != '"') {
		r = LN_WRONGPARSER;
		goto fail;
	}

	/* success, persist */
	*parsed = i + 1 - *offs; /* "eat" terminal double quote */
	/* create JSON value to save quoted string contents */
	CHKN(cstr = strndup((char*)c + *offs + 1, *parsed - 2));
	CHKN(*value = json_object_new_string(cstr));
	free(cstr);

ENDParser


/**
 * Parse an ISO date, that is YYYY-MM-DD (exactly this format).
 * Note: we do manual loop unrolling -- this is fast AND efficient.
 * rgerhards, 2011-01-14
 */
BEGINParser(ISODate)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	if(*offs+10 > strLen)
		goto fail;	/* if it is not 10 chars, it can't be an ISO date */

	/* year */
	if(!isdigit(c[i])) goto fail;
	if(!isdigit(c[i+1])) goto fail;
	if(!isdigit(c[i+2])) goto fail;
	if(!isdigit(c[i+3])) goto fail;
	if(c[i+4] != '-') goto fail;
	/* month */
	if(c[i+5] == '0') {
		if(c[i+6] < '1' || c[i+6] > '9') goto fail;
	} else if(c[i+5] == '1') {
		if(c[i+6] < '0' || c[i+6] > '2') goto fail;
	} else {
		goto fail;
	}
	if(c[i+7] != '-') goto fail;
	/* day */
	if(c[i+8] == '0') {
		if(c[i+9] < '1' || c[i+9] > '9') goto fail;
	} else if(c[i+8] == '1' || c[i+8] == '2') {
		if(!isdigit(c[i+9])) goto fail;
	} else if(c[i+8] == '3') {
		if(c[i+9] != '0' && c[i+9] != '1') goto fail;
	} else {
		goto fail;
	}

	/* success, persist */
	*parsed = 10;

ENDParser

/**
 * Parse a Cisco interface spec. A sample for such a spec is:
 *   outside:176.97.252.102/50349
 * right now, we interpret this as
 * - non-whitespace
 * - colon
 * - IP Address
 * - Slash
 * - port
 * Note that this parser does not yet extract the individual parts
 * due to the restrictions in current liblognorm. This is planned for
 * after a general algorithm overhaul.
 * In order to match, this syntax must start on a non-whitespace char
 * other than colon.
 */
BEGINParser(CiscoInterfaceSpec)
	const char *c;
	size_t i;
	size_t localParsed;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	if(c[i] == ':' || isspace(c[i])) goto fail;

	while(i < strLen) {
		if(isspace(c[i])) goto fail;
		if(c[i] == ':')
			break;
		++i;
	}
	if(i == strLen) goto fail;
	++i; /* skip over colon */

	/* we now utilize our other parser helpers */
	if(ln_parseIPv4(str, strLen, &i, node, &localParsed, NULL) != 0) goto fail;
	i += localParsed;
	if(i == strLen || c[i] != '/') goto fail;
	++i; /* skip slash */
	if(ln_parseNumber(str, strLen, &i, node, &localParsed, NULL) != 0) goto fail;
	i += localParsed;
	if(i < strLen && !isspace(c[i])) goto fail;

	/* success, persist */
	*parsed = i - *offs;
ENDParser

/**
 * Parse a duration. A duration is similar to a timestamp, except that
 * it tells about time elapsed. As such, hours can be larger than 23
 * and hours may also be specified by a single digit (this, for example,
 * is commonly done in Cisco software).
 * Note: we do manual loop unrolling -- this is fast AND efficient.
 */
BEGINParser(Duration)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	/* hour is a bit tricky */
	if(!isdigit(c[i])) goto fail;
	++i;
	if(isdigit(c[i]))
		++i;
	if(c[i] == ':')
		++i;
	else
		goto fail;

	if(i+5 > strLen)
		goto fail;/* if it is not 5 chars from here, it can't be us */

	if(c[i] < '0' || c[i] > '5') goto fail;
	if(!isdigit(c[i+1])) goto fail;
	if(c[i+2] != ':') goto fail;
	if(c[i+3] < '0' || c[i+3] > '5') goto fail;
	if(!isdigit(c[i+4])) goto fail;

	/* success, persist */
	*parsed = (i + 5) - *offs;

ENDParser

/**
 * Parse a timestamp in 24hr format (exactly HH:MM:SS).
 * Note: we do manual loop unrolling -- this is fast AND efficient.
 * rgerhards, 2011-01-14
 */
BEGINParser(Time24hr)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	if(*offs+8 > strLen)
		goto fail;	/* if it is not 8 chars, it can't be us */

	/* hour */
	if(c[i] == '0' || c[i] == '1') {
		if(!isdigit(c[i+1])) goto fail;
	} else if(c[i] == '2') {
		if(c[i+1] < '0' || c[i+1] > '3') goto fail;
	} else {
		goto fail;
	}
	/* TODO: the code below is a duplicate of 24hr parser - create common function */
	if(c[i+2] != ':') goto fail;
	if(c[i+3] < '0' || c[i+3] > '5') goto fail;
	if(!isdigit(c[i+4])) goto fail;
	if(c[i+5] != ':') goto fail;
	if(c[i+6] < '0' || c[i+6] > '5') goto fail;
	if(!isdigit(c[i+7])) goto fail;

	/* success, persist */
	*parsed = 8;

ENDParser

/**
 * Parse a timestamp in 12hr format (exactly HH:MM:SS).
 * Note: we do manual loop unrolling -- this is fast AND efficient.
 * TODO: the code below is a duplicate of 24hr parser - create common function?
 * rgerhards, 2011-01-14
 */
BEGINParser(Time12hr)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	if(*offs+8 > strLen)
		goto fail;	/* if it is not 8 chars, it can't be us */

	/* hour */
	if(c[i] == '0') {
		if(!isdigit(c[i+1])) goto fail;
	} else if(c[i] == '1') {
		if(c[i+1] < '0' || c[i+1] > '2') goto fail;
	} else {
		goto fail;
	}
	if(c[i+2] != ':') goto fail;
	if(c[i+3] < '0' || c[i+3] > '5') goto fail;
	if(!isdigit(c[i+4])) goto fail;
	if(c[i+5] != ':') goto fail;
	if(c[i+6] < '0' || c[i+6] > '5') goto fail;
	if(!isdigit(c[i+7])) goto fail;

	/* success, persist */
	*parsed = 8;

ENDParser




/* helper to IPv4 address parser, checks the next set of numbers.
 * Syntax 1 to 3 digits, value together not larger than 255.
 * @param[in] str parse buffer
 * @param[in/out] offs offset into buffer, updated if successful
 * @return 0 if OK, 1 otherwise
 */
static int
chkIPv4AddrByte(const char *str, size_t strLen, size_t *offs)
{
	int val = 0;
	int r = 1;	/* default: fail -- simplifies things */
	const char *c;
	size_t i = *offs;

	c = str;
	if(i == strLen || !isdigit(c[i]))
		goto fail;
	val = c[i++] - '0';
	if(i < strLen && isdigit(c[i])) {
		val = val * 10 + c[i++] - '0';
		if(i < strLen && isdigit(c[i]))
			val = val * 10 + c[i++] - '0';
	}
	if(val > 255)	/* cannot be a valid IP address byte! */
		goto fail;

	*offs = i;
	r = 0;
fail:
	return r;
}

/**
 * Parser for IPv4 addresses.
 */
BEGINParser(IPv4)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	i = *offs;
	if(i + 7 > strLen) {
		/* IPv4 addr requires at least 7 characters */
		goto fail;
	}
	c = str;

	/* byte 1*/
	if(chkIPv4AddrByte(str, strLen, &i) != 0) goto fail;
	if(i == strLen || c[i++] != '.') goto fail;
	/* byte 2*/
	if(chkIPv4AddrByte(str, strLen, &i) != 0) goto fail;
	if(i == strLen || c[i++] != '.') goto fail;
	/* byte 3*/
	if(chkIPv4AddrByte(str, strLen, &i) != 0) goto fail;
	if(i == strLen || c[i++] != '.') goto fail;
	/* byte 4 - we do NOT need any char behind it! */
	if(chkIPv4AddrByte(str, strLen, &i) != 0) goto fail;

	/* if we reach this point, we found a valid IP address */
	*parsed = i - *offs;

ENDParser
