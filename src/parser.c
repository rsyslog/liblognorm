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
#include <sys/types.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

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

/* parser _parse interface
 *
 * All parsers receive 
 *
 * @param[in] str the to-be-parsed string
 * @param[in] strLen length of the to-be-parsed string
 * @param[in] offs an offset into the string
 * @param[in] pointer to parser data block
 * @param[out] parsed bytes
 * @param[out] value ptr to json object containing parsed data
 *             (can be unused, but if used *value MUST be NULL on entry)
 *
 * They will try to parse out "their" object from the string. If they
 * succeed, they:
 *
 * return 0 on success and LN_WRONGPARSER if this parser could
 *           not successfully parse (but all went well otherwise) and something
 *           else in case of an error.
 */
#define PARSER_Parse(ParserName) \
int ln_parse##ParserName( \
	__attribute__((unused)) ln_ctx ctx, \
	const char *const str, \
	const size_t strLen, \
	size_t *const offs,       \
	__attribute__((unused)) void *const pdata, \
	size_t *parsed,                                      \
	struct json_object **value) \
{ \
	int r = LN_WRONGPARSER; \
	*parsed = 0;

#define FAILParser \
	goto parserdone; /* suppress warnings */ \
parserdone: \
	r = 0; \
	goto done; /* suppress warnings */ \
done: 

#define ENDFailParser \
	return r; \
}

/* parser constructor
 * @param[in] ed extra data (legacy)
 * @param[in] json config json items
 * @param[out] data parser data block (to be allocated)
 * At minimum, *data must be set to NULL
 * @return error status (0 == OK)
 */
#define PARSER_Construct(ParserName) \
int ln_construct##ParserName( \
	__attribute__((unused)) ln_ctx ctx, \
	__attribute__((unused)) const char *const ed, \
	__attribute__((unused)) json_object *const json, \
	void **pdata)

/* parser destructor
 * @param[data] data parser data block (to be de-allocated)
 */
#define PARSER_Destruct(ParserName) \
void ln_destruct##ParserName(__attribute__((unused)) ln_ctx ctx, void *const pdata)



/**
 * Parse a TIMESTAMP as specified in RFC5424 (subset of RFC3339).
 */
PARSER_Parse(RFC5424Date)
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
	if(len == 0 || *pszTS++ != '-') goto done;
	--len;
	month = hParseInt(&pszTS, &len);
	if(month < 1 || month > 12) goto done;

	if(len == 0 || *pszTS++ != '-')
		goto done;
	--len;
	day = hParseInt(&pszTS, &len);
	if(day < 1 || day > 31) goto done;

	if(len == 0 || *pszTS++ != 'T') goto done;
	--len;

	hour = hParseInt(&pszTS, &len);
	if(hour < 0 || hour > 23) goto done;

	if(len == 0 || *pszTS++ != ':')
		goto done;
	--len;
	minute = hParseInt(&pszTS, &len);
	if(minute < 0 || minute > 59) goto done;

	if(len == 0 || *pszTS++ != ':') goto done;
	--len;
	second = hParseInt(&pszTS, &len);
	if(second < 0 || second > 60) goto done;

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
	if(len == 0) goto done;

	if(*pszTS == 'Z') {
		--len;
		pszTS++; /* eat Z */
	} else if((*pszTS == '+') || (*pszTS == '-')) {
		--len;
		pszTS++;

		OffsetHour = hParseInt(&pszTS, &len);
		if(OffsetHour < 0 || OffsetHour > 23)
			goto done;

		if(len == 0 || *pszTS++ != ':')
			goto done;
		--len;
		OffsetMinute = hParseInt(&pszTS, &len);
		if(OffsetMinute < 0 || OffsetMinute > 59)
			goto done;
	} else {
		/* there MUST be TZ information */
		goto done;
	}

	if(len > 0) {
		if(*pszTS != ' ') /* if it is not a space, it can not be a "good" time */
			goto done;
	}

	/* we had success, so update parse pointer */
	*parsed = orglen - len;

	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}

	r = 0; /* success */
done:
	return r;
}


/**
 * Parse a RFC3164 Date.
 */
PARSER_Parse(RFC3164Date)
	const unsigned char *p;
	size_t len, orglen;
	/* variables to temporarily hold time information while we parse */
	__attribute__((unused)) int month;
	int day;
#if 0 /* TODO: why does this still exist? */
	int year = 0; /* 0 means no year provided */
#endif
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
		goto done;

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
				goto done;
		} else if(*p == 'u' || *p == 'U') {
			++p;
			if(*p == 'n' || *p == 'N') {
				++p;
				month = 6;
			} else if(*p == 'l' || *p == 'L') {
				++p;
				month = 7;
			} else
				goto done;
		} else
			goto done;
		break;
	case 'f':
	case 'F':
		if(*p == 'e' || *p == 'E') {
			++p;
			if(*p == 'b' || *p == 'B') {
				++p;
				month = 2;
			} else
				goto done;
		} else
			goto done;
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
				goto done;
		} else
			goto done;
		break;
	case 'a':
	case 'A':
		if(*p == 'p' || *p == 'P') {
			++p;
			if(*p == 'r' || *p == 'R') {
				++p;
				month = 4;
			} else
				goto done;
		} else if(*p == 'u' || *p == 'U') {
			++p;
			if(*p == 'g' || *p == 'G') {
				++p;
				month = 8;
			} else
				goto done;
		} else
			goto done;
		break;
	case 's':
	case 'S':
		if(*p == 'e' || *p == 'E') {
			++p;
			if(*p == 'p' || *p == 'P') {
				++p;
				month = 9;
			} else
				goto done;
		} else
			goto done;
		break;
	case 'o':
	case 'O':
		if(*p == 'c' || *p == 'C') {
			++p;
			if(*p == 't' || *p == 'T') {
				++p;
				month = 10;
			} else
				goto done;
		} else
			goto done;
		break;
	case 'n':
	case 'N':
		if(*p == 'o' || *p == 'O') {

			++p;
			if(*p == 'v' || *p == 'V') {
				++p;
				month = 11;
			} else
				goto done;
		} else
			goto done;
		break;
	case 'd':
	case 'D':
		if(*p == 'e' || *p == 'E') {
			++p;
			if(*p == 'c' || *p == 'C') {
				++p;
				month = 12;
			} else
				goto done;
		} else
			goto done;
		break;
	default:
		goto done;
	}

	len -= 3;
	
	/* done month */

	if(len == 0 || *p++ != ' ')
		goto done;
	--len;

	/* we accept a slightly malformed timestamp with one-digit days. */
	if(*p == ' ') {
		--len;
		++p;
	}

	day = hParseInt(&p, &len);
	if(day < 1 || day > 31)
		goto done;

	if(len == 0 || *p++ != ' ')
		goto done;
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
			goto done;
		--len;
		hour = hParseInt(&p, &len);
	}

	if(hour < 0 || hour > 23)
		goto done;

	if(len == 0 || *p++ != ':')
		goto done;
	--len;
	minute = hParseInt(&p, &len);
	if(minute < 0 || minute > 59)
		goto done;

	if(len == 0 || *p++ != ':')
		goto done;
	--len;
	second = hParseInt(&p, &len);
	if(second < 0 || second > 60)
		goto done;

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
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}


/**
 * Parse a Number.
 * Note that a number is an abstracted concept. We always represent it
 * as 64 bits (but may later change our mind if performance dictates so).
 */
PARSER_Parse(Number)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;

	for (i = *offs; i < strLen && isdigit(c[i]); i++);
	if (i == *offs)
		goto done;
	
	/* success, persist */
	*parsed = i - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
ln_dbgprintf(ctx, "number parsed '%s'", cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}

/**
 * Parse a Real-number in floating-pt form.
 */
PARSER_Parse(Float)
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
		goto done;
		
	/* success, persist */
	*parsed = i - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}


struct data_HexNumber {
	uint64_t maxval;
};
/**
 * Parse a hex Number.
 * A hex number begins with 0x and contains only hex digits until the terminating
 * whitespace. Note that if a non-hex character is deteced inside the number string,
 * this is NOT considered to be a number.
 */
PARSER_Parse(HexNumber)
	const char *c;
	size_t i = *offs;
	struct data_HexNumber *const data = (struct data_HexNumber*) pdata;
	uint64_t maxval = data->maxval;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;

	if(c[i] != '0' || c[i+1] != 'x')
		goto done;

	uint64_t val = 0;
	for (i += 2 ; i < strLen && isxdigit(c[i]); i++) {
		const char digit = tolower(c[i]);
		val *= 16;
		if(digit >= 'a' && digit <= 'f')
			val += digit - 'a' + 10;
		else
			val += digit - '0';
	}
	if (i == *offs || !isspace(c[i]))
		goto done;
	if(maxval > 0 && val > maxval) {
		ln_dbgprintf(ctx, "hexnumber parser: val too large (max %" PRIu64
			     ", actual %" PRIu64 ")",
			     maxval, val);
		goto done;
	}
	
	/* success, persist */
	*parsed = i - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}
PARSER_Construct(HexNumber)
{
	int r = 0;
	struct data_HexNumber *data = (struct data_HexNumber*) calloc(1, sizeof(struct data_HexNumber));

	if(json == NULL)
		goto done;

	json_object_object_foreach(json, key, val) {
		if(!strcmp(key, "maxval")) {
			errno = 0;
			data->maxval = json_object_get_int64(val);
			if(errno != 0) {
				ln_errprintf(ctx, errno, "param 'maxval' must be integer but is: %s",
					 json_object_to_json_string(val));
			}
		} else {
			ln_errprintf(ctx, 0, "invalid param for hexnumber: %s",
				 json_object_to_json_string(val));
		}
	}

done:
	*pdata = data;
	return r;
}
PARSER_Destruct(HexNumber)
{
	free(pdata);
}


/**
 * Parse a kernel timestamp.
 * This is a fixed format, see
 * https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/kernel/printk/printk.c?id=refs/tags/v4.0#n1011
 * This is the code that generates it:
 * sprintf(buf, "[%5lu.%06lu] ",  (unsigned long)ts, rem_nsec / 1000);
 * We accept up to 12 digits for ts, everything above that for sure is
 * no timestamp.
 */
#define LEN_KERNEL_TIMESTAMP 14
PARSER_Parse(KernelTimestamp)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;

	i = *offs;
	if(c[i] != '[' || i+LEN_KERNEL_TIMESTAMP > strLen
	   || !isdigit(c[i+1])
	   || !isdigit(c[i+2])
	   || !isdigit(c[i+3])
	   || !isdigit(c[i+4])
	   || !isdigit(c[i+5])
	   )
		goto done;
	i += 6;
	for(int j = 0 ; j < 7 && i < strLen && isdigit(c[i]) ; )
		++i, ++j;	/* just scan */

	if(i >= strLen || c[i] != '.')
		goto done;

	++i; /* skip over '.' */

	if( i+7 > strLen
	   || !isdigit(c[i+0])
	   || !isdigit(c[i+1])
	   || !isdigit(c[i+2])
	   || !isdigit(c[i+3])
	   || !isdigit(c[i+4])
	   || !isdigit(c[i+5])
	   || c[i+6] != ']'
	   )
		goto done;
	i += 7;

	/* success, persist */
	*parsed = i - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}

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
PARSER_Parse(Whitespace)
	const char *c;
	size_t i = *offs;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;

	if(!isspace(c[i]))
		goto done;

	for (i++ ; i < strLen && isspace(c[i]); i++);
	/* success, persist */
	*parsed = i - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}


/**
 * Parse a word.
 * A word is a SP-delimited entity. The parser always works, except if
 * the offset is position on a space upon entry.
 */
PARSER_Parse(Word)
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

	if(i == *offs)
		goto done;

	/* success, persist */
	*parsed = i - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}


struct data_StringTo {
	const char *toFind;
	size_t len;
};
/**
 * Parse everything up to a specific string.
 * swisskid, 2015-01-21
 */
PARSER_Parse(StringTo)
	const char *c;
	size_t i, j, m;
	int chkstr;
	struct data_StringTo *const data = (struct data_StringTo*) pdata;
	const char *const toFind = data->toFind;
	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
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
		while(m < strLen && j < data->len ) {
		    m++;
		    j++;
		    if(c[m] != toFind[j])
			break;
		    if (j == data->len) 
			chkstr = 1;
		}
	    }
	}
	if(i == *offs || i == strLen || c[i] != toFind[0])
		goto done;

	/* success, persist */
	*parsed = i - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}

PARSER_Construct(StringTo)
{
	int r = 0;
	struct data_StringTo *data = (struct data_StringTo*) calloc(1, sizeof(struct data_StringTo));

	data->toFind = strdup(ed);
	data->len = strlen(data->toFind);

	*pdata = data;
	return r;
}
PARSER_Destruct(StringTo)
{
	struct data_StringTo *data = (struct data_StringTo*) pdata;
	free((void*)data->toFind);
	free(pdata);
}

/**
 * Parse a alphabetic word.
 * A alpha word is composed of characters for which isalpha returns true.
 * The parser dones if there is no alpha character at all.
 */
PARSER_Parse(Alpha)
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
		goto done;
	}

	/* success, persist */
	*parsed = i - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}


struct data_CharTo {
	char *term_chars;
	size_t n_term_chars;
};
/**
 * Parse everything up to a specific character.
 * The character must be the only char inside extra data passed to the parser.
 * It is a program error if strlen(ed) != 1. It is considered a format error if
 * a) the to-be-parsed buffer is already positioned on the terminator character
 * b) there is no terminator until the end of the buffer
 * In those cases, the parsers declares itself as not being successful, in all
 * other cases a string is extracted.
 */
PARSER_Parse(CharTo)
	size_t i;
	struct data_CharTo *const data = (struct data_CharTo*) pdata;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	i = *offs;

	/* search end of word */
	int found = 0;
	while(i < strLen && !found) {
		for(size_t j = 0 ; j < data->n_term_chars ; ++j) {
			if(str[i] == data->term_chars[j]) {
				found = 1;
				break;
			}
		}
		if(!found)
			++i;
	}

	if(i == *offs || i == strLen || !found)
		goto done;

	/* success, persist */
	*parsed = i - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0;
done:
	return r;
}
PARSER_Construct(CharTo)
{
	int r = 0;
	struct data_CharTo *data = (struct data_CharTo*) calloc(1, sizeof(struct data_CharTo));
	data->term_chars = strdup(ed);
	data->n_term_chars = strlen(data->term_chars);
	*pdata = data;
	return r;
}
PARSER_Destruct(CharTo)
{
	struct data_CharTo *data = (struct data_CharTo*) calloc(1, sizeof(struct data_CharTo));
	free(data->term_chars);
	free(pdata);
}



struct data_Literal {
	const char *lit;
};
/**
 * Parse a specific literal.
 */
PARSER_Parse(Literal)
	struct data_Literal *const data = (struct data_Literal*) pdata;
	const char *const lit = data->lit;
	size_t i = *offs;
	size_t j;

	for(j = 0 ; lit[j] != '\0' && i < strLen ; ++j) {
		if(lit[j] != str[i])
			break;
		++i;
	}

	*parsed = j; /* we must always return how far we parsed! */
	if(lit[j] == '\0') {
		if(value != NULL) {
			char *cstr = strndup(str+ *offs, *parsed);
			*value = json_object_new_string(cstr);
			free(cstr);
		}
		r = 0;
	}
	return r;
}
PARSER_Construct(Literal)
{
	int r = 0;
	struct data_Literal *data = (struct data_Literal*) calloc(1, sizeof(struct data_Literal));

	data->lit = strdup(ed);

	*pdata = data;
	return r;
}
PARSER_Destruct(Literal)
{
	struct data_Literal *data = (struct data_Literal*) pdata;
	free((void*)data->lit);
	free(pdata);
}
/* for path compaction, we need a special handler to combine two
 * literal data elements.
 */
int
ln_combineData_Literal(void *const porg, void *const padd)
{
	struct data_Literal *const __restrict__ org = porg;
	struct data_Literal *const __restrict__ add = padd;
	int r = 0;
	const size_t len = strlen(org->lit);
	const size_t add_len = strlen(add->lit);
	char *const newlit = (char*)realloc((void*)org->lit, len+add_len+1);
	CHKN(newlit);
	org->lit = newlit;
	memcpy((char*)org->lit+len, add->lit, add_len+1);
done:	return r;
}


struct data_CharSeparated {
	char *term_chars;
	size_t n_term_chars;
};
/**
 * Parse everything up to a specific character, or up to the end of string.
 * The character must be the only char inside extra data passed to the parser.
 * It is a program error if strlen(ed) != 1.
 * This parser always returns success.
 * By nature of the parser, it is required that end of string or the separator
 * follows this field in rule.
 */
PARSER_Parse(CharSeparated)
	struct data_CharSeparated *const data = (struct data_CharSeparated*) pdata;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	i = *offs;

	/* search end of word */
	int found = 0;
	while(i < strLen && !found) {
		for(size_t j = 0 ; j < data->n_term_chars ; ++j) {
			if(str[i] == data->term_chars[j]) {
				found = 1;
				break;
			}
		}
		if(!found)
			++i;
	}

	/* success, persist */
	*parsed = i - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
	return r;
}
PARSER_Construct(CharSeparated)
{
	int r = 0;
	struct data_CharSeparated *data = (struct data_CharSeparated*) calloc(1, sizeof(struct data_CharSeparated));

	data->term_chars = strdup(ed);
	data->n_term_chars = strlen(data->term_chars);
	*pdata = data;
	return r;
}
PARSER_Destruct(CharSeparated)
{
	struct data_CharSeparated *data = (struct data_CharSeparated*) calloc(1, sizeof(struct data_CharSeparated));
	free(data->term_chars);
	free(pdata);
}


/**
 * Just get everything till the end of string.
 */
PARSER_Parse(Rest)
	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);

	/* silence the warning about unused variable */
	(void)str;
	/* success, persist */
	*parsed = strLen - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0;
	return r;
}

/**
 * Parse a possibly quoted string. In this initial implementation, escaping of the quote
 * char is not supported. A quoted string is one start starts with a double quote,
 * has some text (not containing double quotes) and ends with the first double
 * quote character seen. The extracted string does NOT include the quote characters.
 * swisskid, 2015-01-21
 */
PARSER_Parse(OpQuotedString)
	const char *c;
	size_t i;
	char *cstr = NULL;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	if(c[i] != '"') {
		while(i < strLen && c[i] != ' ') 
			i++;

		if(i == *offs)
			goto done;

		/* success, persist */
		*parsed = i - *offs;
		/* create JSON value to save quoted string contents */
		CHKN(cstr = strndup((char*)c + *offs, *parsed));
	} else {
	    ++i;

	    /* search end of string */
	    while(i < strLen && c[i] != '"') 
		    i++;

	    if(i == strLen || c[i] != '"')
		    goto done;
	    /* success, persist */
	    *parsed = i + 1 - *offs; /* "eat" terminal double quote */
	    /* create JSON value to save quoted string contents */
	    CHKN(cstr = strndup((char*)c + *offs + 1, *parsed - 2));
	}
	CHKN(*value = json_object_new_string(cstr));

	r = 0; /* success */
done:
	free(cstr);
	return r;
}


/**
 * Parse a quoted string. In this initial implementation, escaping of the quote
 * char is not supported. A quoted string is one start starts with a double quote,
 * has some text (not containing double quotes) and ends with the first double
 * quote character seen. The extracted string does NOT include the quote characters.
 * rgerhards, 2011-01-14
 */
PARSER_Parse(QuotedString)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;
	if(i + 2 > strLen)
		goto done;	/* needs at least 2 characters */

	if(c[i] != '"')
		goto done;
	++i;

	/* search end of string */
	while(i < strLen && c[i] != '"') 
		i++;

	if(i == strLen || c[i] != '"')
		goto done;

	/* success, persist */
	*parsed = i + 1 - *offs; /* "eat" terminal double quote */
	/* create JSON value to save quoted string contents */
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}


/**
 * Parse an ISO date, that is YYYY-MM-DD (exactly this format).
 * Note: we do manual loop unrolling -- this is fast AND efficient.
 * rgerhards, 2011-01-14
 */
PARSER_Parse(ISODate)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	if(*offs+10 > strLen)
		goto done;	/* if it is not 10 chars, it can't be an ISO date */

	/* year */
	if(!isdigit(c[i])) goto done;
	if(!isdigit(c[i+1])) goto done;
	if(!isdigit(c[i+2])) goto done;
	if(!isdigit(c[i+3])) goto done;
	if(c[i+4] != '-') goto done;
	/* month */
	if(c[i+5] == '0') {
		if(c[i+6] < '1' || c[i+6] > '9') goto done;
	} else if(c[i+5] == '1') {
		if(c[i+6] < '0' || c[i+6] > '2') goto done;
	} else {
		goto done;
	}
	if(c[i+7] != '-') goto done;
	/* day */
	if(c[i+8] == '0') {
		if(c[i+9] < '1' || c[i+9] > '9') goto done;
	} else if(c[i+8] == '1' || c[i+8] == '2') {
		if(!isdigit(c[i+9])) goto done;
	} else if(c[i+8] == '3') {
		if(c[i+9] != '0' && c[i+9] != '1') goto done;
	} else {
		goto done;
	}

	/* success, persist */
	*parsed = 10;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}

/**
 * Parse a Cisco interface spec. Sample for such a spec are:
 *   outside:192.168.52.102/50349
 *   inside:192.168.1.15/56543 (192.168.1.112/54543)
 *   outside:192.168.1.13/50179 (192.168.1.13/50179)(LOCAL\some.user)
 *   outside:192.168.1.25/41850(LOCAL\RG-867G8-DEL88D879BBFFC8) 
 *   inside:192.168.1.25/53 (192.168.1.25/53) (some.user)
 *   192.168.1.15/0(LOCAL\RG-867G8-DEL88D879BBFFC8)
 * From this, we conclude the format is:
 *   [interface:]ip/port [SP (ip2/port2)] [[SP](username)]
 * In order to match, this syntax must start on a non-whitespace char
 * other than colon.
 */
PARSER_Parse(CiscoInterfaceSpec)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	if(c[i] == ':' || isspace(c[i])) goto done;

	/* first, check if we have an interface. We do this by trying
	 * to detect if we have an IP. If we have, obviously no interface
	 * is present. Otherwise, we check if we have a valid interface.
	 */
	int bHaveInterface = 0;
	size_t idxInterface;
	size_t lenInterface;
	int bHaveIP = 0;
	size_t lenIP;
	size_t idxIP = i;
	if(ln_parseIPv4(ctx, str, strLen, &i, NULL, &lenIP, NULL) == 0) {
		bHaveIP = 1;
		i += lenIP - 1; /* position on delimiter */
	} else {
		idxInterface = i;
		while(i < strLen) {
			if(isspace(c[i])) goto done;
			if(c[i] == ':')
				break;
			++i;
		}
		lenInterface = i - idxInterface;
		bHaveInterface = 1;
	}
	if(i == strLen) goto done;
	++i; /* skip over colon */

	/* we now utilize our other parser helpers */
	if(!bHaveIP) {
		idxIP = i;
		if(ln_parseIPv4(ctx, str, strLen, &i, NULL, &lenIP, NULL) != 0) goto done;
		i += lenIP;
	}
	if(i == strLen || c[i] != '/') goto done;
	++i; /* skip slash */
	const size_t idxPort = i;
	size_t lenPort;
	if(ln_parseNumber(ctx, str, strLen, &i, NULL, &lenPort, NULL) != 0) goto done;
	i += lenPort;
	if(i == strLen) goto success;

	/* check if optional second ip/port is present
	 * We assume we must at least have 5 chars [" (::1)"]
	 */
	int bHaveIP2 = 0;
	size_t idxIP2, lenIP2;
	size_t idxPort2, lenPort2;
	if(i+5 < strLen && c[i] == ' ' && c[i+1] == '(') {
		size_t iTmp = i+2; /* skip over " (" */
		idxIP2 = iTmp;
		if(ln_parseIPv4(ctx, str, strLen, &iTmp, NULL, &lenIP2, NULL) == 0) {
			iTmp += lenIP2;
			if(i < strLen || c[iTmp] == '/') {
				++iTmp; /* skip slash */
				idxPort2 = iTmp;
				if(ln_parseNumber(ctx, str, strLen, &iTmp, NULL, &lenPort2, NULL) == 0) {
					iTmp += lenPort2;
					if(iTmp < strLen && c[iTmp] == ')') {
						i = iTmp + 1; /* match, so use new index */
						bHaveIP2 = 1;
					}
				}
			}
		}
	}

	/* check if optional username is present
	 * We assume we must at least have 3 chars ["(n)"]
	 */
	int bHaveUser = 0;
	size_t idxUser;
	size_t lenUser;
	if(   (i+2 < strLen && c[i] == '(' && !isspace(c[i+1]) )
	   || (i+3 < strLen && c[i] == ' ' && c[i+1] == '(' && !isspace(c[i+2])) ) {
		idxUser = i + ((c[i] == ' ') ? 2 : 1); /* skip [SP]'(' */
		size_t iTmp = idxUser;
		while(iTmp < strLen && !isspace(c[iTmp]) && c[iTmp] != ')')
			++iTmp; /* just scan */
		if(iTmp < strLen && c[iTmp] == ')') {
			i = iTmp + 1; /* we have a match, so use new index */
			bHaveUser = 1;
			lenUser = iTmp - idxUser;
		}
	}

	/* all done, save data */
	if(value == NULL)
		goto success;

	CHKN(*value = json_object_new_object());
	json_object *json;
	if(bHaveInterface) {
		CHKN(json = json_object_new_string_len(c+idxInterface, lenInterface));
		json_object_object_add(*value, "interface", json);
	}
	CHKN(json = json_object_new_string_len(c+idxIP, lenIP));
	json_object_object_add(*value, "ip", json);
	CHKN(json = json_object_new_string_len(c+idxPort, lenPort));
	json_object_object_add(*value, "port", json);
	if(bHaveIP2) {
		CHKN(json = json_object_new_string_len(c+idxIP2, lenIP2));
		json_object_object_add(*value, "ip2", json);
		CHKN(json = json_object_new_string_len(c+idxPort2, lenPort2));
		json_object_object_add(*value, "port2", json);
	}
	if(bHaveUser) {
		CHKN(json = json_object_new_string_len(c+idxUser, lenUser));
		json_object_object_add(*value, "user", json);
	}

success: /* success, persist */
	*parsed = i - *offs;
	r = 0; /* success */
done:
	if(r != 0 && value != NULL && *value != NULL) {
		json_object_put(*value);
		*value = NULL; /* to be on the save side */
	}
	return r;
}

/**
 * Parse a duration. A duration is similar to a timestamp, except that
 * it tells about time elapsed. As such, hours can be larger than 23
 * and hours may also be specified by a single digit (this, for example,
 * is commonly done in Cisco software).
 * Note: we do manual loop unrolling -- this is fast AND efficient.
 */
PARSER_Parse(Duration)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	/* hour is a bit tricky */
	if(!isdigit(c[i])) goto done;
	++i;
	if(isdigit(c[i]))
		++i;
	if(c[i] == ':')
		++i;
	else
		goto done;

	if(i+5 > strLen)
		goto done;/* if it is not 5 chars from here, it can't be us */

	if(c[i] < '0' || c[i] > '5') goto done;
	if(!isdigit(c[i+1])) goto done;
	if(c[i+2] != ':') goto done;
	if(c[i+3] < '0' || c[i+3] > '5') goto done;
	if(!isdigit(c[i+4])) goto done;

	/* success, persist */
	*parsed = (i + 5) - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}

/**
 * Parse a timestamp in 24hr format (exactly HH:MM:SS).
 * Note: we do manual loop unrolling -- this is fast AND efficient.
 * rgerhards, 2011-01-14
 */
PARSER_Parse(Time24hr)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	if(*offs+8 > strLen)
		goto done;	/* if it is not 8 chars, it can't be us */

	/* hour */
	if(c[i] == '0' || c[i] == '1') {
		if(!isdigit(c[i+1])) goto done;
	} else if(c[i] == '2') {
		if(c[i+1] < '0' || c[i+1] > '3') goto done;
	} else {
		goto done;
	}
	/* TODO: the code below is a duplicate of 24hr parser - create common function */
	if(c[i+2] != ':') goto done;
	if(c[i+3] < '0' || c[i+3] > '5') goto done;
	if(!isdigit(c[i+4])) goto done;
	if(c[i+5] != ':') goto done;
	if(c[i+6] < '0' || c[i+6] > '5') goto done;
	if(!isdigit(c[i+7])) goto done;

	/* success, persist */
	*parsed = 8;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}

/**
 * Parse a timestamp in 12hr format (exactly HH:MM:SS).
 * Note: we do manual loop unrolling -- this is fast AND efficient.
 * TODO: the code below is a duplicate of 24hr parser - create common function?
 * rgerhards, 2011-01-14
 */
PARSER_Parse(Time12hr)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = str;
	i = *offs;

	if(*offs+8 > strLen)
		goto done;	/* if it is not 8 chars, it can't be us */

	/* hour */
	if(c[i] == '0') {
		if(!isdigit(c[i+1])) goto done;
	} else if(c[i] == '1') {
		if(c[i+1] < '0' || c[i+1] > '2') goto done;
	} else {
		goto done;
	}
	if(c[i+2] != ':') goto done;
	if(c[i+3] < '0' || c[i+3] > '5') goto done;
	if(!isdigit(c[i+4])) goto done;
	if(c[i+5] != ':') goto done;
	if(c[i+6] < '0' || c[i+6] > '5') goto done;
	if(!isdigit(c[i+7])) goto done;

	/* success, persist */
	*parsed = 8;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}


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
	int r = 1;	/* default: done -- simplifies things */
	const char *c;
	size_t i = *offs;

	c = str;
	if(i == strLen || !isdigit(c[i]))
		goto done;
	val = c[i++] - '0';
	if(i < strLen && isdigit(c[i])) {
		val = val * 10 + c[i++] - '0';
		if(i < strLen && isdigit(c[i]))
			val = val * 10 + c[i++] - '0';
	}
	if(val > 255)	/* cannot be a valid IP address byte! */
		goto done;

	*offs = i;
	r = 0;
done:
	return r;
}

/**
 * Parser for IPv4 addresses.
 */
PARSER_Parse(IPv4)
	const char *c;
	size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	i = *offs;
	if(i + 7 > strLen) {
		/* IPv4 addr requires at least 7 characters */
		goto done;
	}
	c = str;

	/* byte 1*/
	if(chkIPv4AddrByte(str, strLen, &i) != 0) goto done;
	if(i == strLen || c[i++] != '.') goto done;
	/* byte 2*/
	if(chkIPv4AddrByte(str, strLen, &i) != 0) goto done;
	if(i == strLen || c[i++] != '.') goto done;
	/* byte 3*/
	if(chkIPv4AddrByte(str, strLen, &i) != 0) goto done;
	if(i == strLen || c[i++] != '.') goto done;
	/* byte 4 - we do NOT need any char behind it! */
	if(chkIPv4AddrByte(str, strLen, &i) != 0) goto done;

	/* if we reach this point, we found a valid IP address */
	*parsed = i - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}


/* skip past the IPv6 address block, parse pointer is set to 
 * first char after the block. Returns an error if already at end
 * of string.
 * @param[in] str parse buffer
 * @param[in/out] offs offset into buffer, updated if successful
 * @return 0 if OK, 1 otherwise
 */
static int
skipIPv6AddrBlock(const char *const __restrict__ str,
	const size_t strLen,
	size_t *const __restrict__ offs)
{
	int j;
	if(*offs == strLen)
		return 1;

	for(j = 0 ; j < 4  && *offs+j < strLen && isxdigit(str[*offs+j]) ; ++j)
		/*just skip*/ ;

	*offs += j;
	return 0;
}

/**
 * Parser for IPv6 addresses.
 * Bases on RFC4291 Section 2.2. The address must be followed
 * by whitespace or end-of-string, else it is not considered
 * a valid address. This prevents false positives.
 */
PARSER_Parse(IPv6)
	const char *c;
	size_t i;
	size_t beginBlock; /* last block begin in case we need IPv4 parsing */
	int hasIPv4 = 0;
	int nBlocks = 0; /* how many blocks did we already have? */
	int bHad0Abbrev = 0; /* :: already used? */

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	i = *offs;
	if(i + 2 > strLen) {
		/* IPv6 addr requires at least 2 characters ("::") */
		goto done;
	}
	c = str;

	/* check that first block is non-empty */
	if(! ( isxdigit(c[i]) || (c[i] == ':' && c[i+1] == ':') ) )
		goto done;

	/* try for all potential blocks plus one more (so we see errors!) */
	for(int j = 0 ; j < 9 ; ++j) {
		beginBlock = i;
		if(skipIPv6AddrBlock(str, strLen, &i) != 0) goto done;
		nBlocks++;
		if(i == strLen) goto chk_ok;
		if(isspace(c[i])) goto chk_ok;
		if(c[i] == '.'){ /* IPv4 processing! */
			hasIPv4 = 1;
			break;
		}
		if(c[i] != ':') goto done;
		i++; /* "eat" ':' */
		if(i == strLen) goto chk_ok;
		/* check for :: */
		if(bHad0Abbrev) {
			if(c[i] == ':') goto done;
		} else {
			if(c[i] == ':') {
				bHad0Abbrev = 1;
				++i;
				if(i == strLen) goto chk_ok;
			}
		}
	}

	if(hasIPv4) {
		size_t ipv4_parsed;
		--nBlocks;
		/* prevent pure IPv4 address to be recognized */
		if(beginBlock == *offs) goto done;
		i = beginBlock;
		if(ln_parseIPv4(ctx, str, strLen, &i, NULL, &ipv4_parsed, NULL) != 0)
			goto done;
		i += ipv4_parsed;
	}

chk_ok:	/* we are finished parsing, check if things are ok */
	if(nBlocks > 8) goto done;
	if(bHad0Abbrev && nBlocks >= 8) goto done;
	/* now check if trailing block is missing. Note that i is already
	 * on next character, so we need to go two back. Two are always
	 * present, else we would not reach this code here.
	 */
	if(c[i-1] == ':' && c[i-2] != ':') goto done;

	/* if we reach this point, we found a valid IP address */
	*parsed = i - *offs;
	if(value != NULL) {
		char *cstr = strndup(str+ *offs, *parsed);
		*value = json_object_new_string(cstr);
		free(cstr);
	}
	r = 0; /* success */
done:
	return r;
}

/* check if a char is valid inside a name of the iptables motif.
 * We try to keep the set as slim as possible, because the iptables
 * parser may otherwise create a very broad match (especially the
 * inclusion of simple words like "DF" cause grief here).
 * Note: we have taken the permitted set from iptables log samples.
 * Report bugs if we missed some additional rules.
 */
static inline int
isValidIPTablesNameChar(const char c)
{
	/* right now, upper case only is valid */
	return ('A' <= c && c <= 'Z') ? 1 : 0;
}

/* helper to iptables parser, parses out a a single name=value pair 
 */
static int
parseIPTablesNameValue(const char *const __restrict__ str,
	const size_t strLen, 
	size_t *const __restrict__ offs,
	struct json_object *const __restrict__ valroot)
{
	int r = LN_WRONGPARSER;
	size_t i = *offs;

	const size_t iName = i;
	while(i < strLen && isValidIPTablesNameChar(str[i]))
		++i;
	if(i == iName || (i < strLen && str[i] != '=' && str[i] != ' '))
		goto done; /* no name at all! */

	const ssize_t lenName = i - iName;

	ssize_t iVal = -1;
	size_t lenVal = i - iVal;
	if(i < strLen && str[i] != ' ') {
		/* we have a real value (not just a flag name like "DF") */
		++i; /* skip '=' */
		iVal = i;
		while(i < strLen && !isspace(str[i]))
			++i;
		lenVal = i - iVal;
	}

	/* parsing OK */
	*offs = i;
	r = 0;

	if(valroot == NULL)
		goto done;

	char *name;
	CHKN(name = malloc(lenName+1));
	memcpy(name, str+iName, lenName);
	name[lenName] = '\0';
	json_object *json;
	if(iVal == -1) {
		json = NULL;
	} else {
		CHKN(json = json_object_new_string_len(str+iVal, lenVal));
	}
	json_object_object_add(valroot, name, json);
	free(name);
done:
	return r;
}

/**
 * Parser for iptables logs (the structured part).
 * This parser is named "v2-iptables" because of a traditional
 * parser named "iptables", which we do not want to replace, at
 * least right now (we may re-think this before the first release).
 * For performance reasons, this works in two stages. In the first
 * stage, we only detect if the motif is correct. The second stage is
 * only called when we know it is. In it, we go once again over the
 * message again and actually extract the data. This is done because
 * data extraction is relatively expensive and in most cases we will
 * have much more frequent mismatches than matches.
 * Note that this motif must have at least one field, otherwise it
 * could detect things that are not iptables to be it. Further limits
 * may be imposed in the future as we see additional need.
 * added 2015-04-30 rgerhards
 */
PARSER_Parse(v2IPTables)
	size_t i = *offs;
	int nfields = 0;

	/* stage one */
	while(i < strLen) {
		CHKR(parseIPTablesNameValue(str, strLen, &i, NULL));
		++nfields;
		/* exactly one SP is permitted between fields */
		if(i < strLen && str[i] == ' ')
			++i;
	}

	if(nfields < 2) {
		FAIL(LN_WRONGPARSER);
	}

	/* success, persist */
	*parsed = i - *offs;
	r = 0;

	/* stage two */
	if(value == NULL)
		goto done;

	i = *offs;
	CHKN(*value = json_object_new_object());
	while(i < strLen) {
		CHKR(parseIPTablesNameValue(str, strLen, &i, *value));
		while(i < strLen && isspace(str[i]))
			++i;
	}

done:
	if(r != 0 && value != NULL && *value != NULL) {
		json_object_put(*value);
		*value = NULL;
	}
	return r;
}

/**
 * Parse JSON. This parser tries to find JSON data inside a message.
 * If it finds valid JSON, it will extract it. Extra data after the
 * JSON is permitted.
 * Note: the json-c JSON parser treats whitespace after the actual
 * json to be part of the json. So in essence, any whitespace is
 * processed by this parser. We use the same semantics to keep things
 * neatly in sync. If json-c changes for some reason or we switch to
 * an alternate json lib, we probably need to be sure to keep that
 * behaviour, and probably emulate it.
 * added 2015-04-28 by rgerhards, v1.1.2
 */
PARSER_Parse(JSON)
	const size_t i = *offs;
	struct json_tokener *tokener = NULL;

	if(str[i] != '{' && str[i] != ']') {
		/* this can't be json, see RFC4627, Sect. 2
		 * see this bug in json-c:
		 * https://github.com/json-c/json-c/issues/181
		 * In any case, it's better to do this quick check,
		 * even if json-c did not have the bug because this
		 * check here is much faster than calling the parser.
		 */
		goto done;
	}

	if((tokener = json_tokener_new()) == NULL)
		goto done;

	struct json_object *const json
		= json_tokener_parse_ex(tokener, str+i, (int) (strLen - i));

	if(json == NULL)
		goto done;

	/* success, persist */
	*parsed =  (i + tokener->char_offset) - *offs;
	r = 0; /* success */

	if(value == NULL) {
		json_object_put(json);
	} else {
		*value = json;
	}

done:
	if(tokener != NULL)
		json_tokener_free(tokener);
	return r;
}


/* check if a char is valid inside a name of a NameValue list
 * The set of valid characters may be extended if there is good
 * need to do so. We have selected the current set carefully, but
 * may have overlooked some cases.
 */
static inline int
isValidNameChar(const char c)
{
	return (isalnum(c)
		|| c == '.'
		|| c == '_'
		|| c == '-'
		) ? 1 : 0;
}
/* helper to NameValue parser, parses out a a single name=value pair 
 *
 * name must be alphanumeric characters, value must be non-whitespace
 * characters, if quoted than with symmetric quotes. Supported formats
 * - name=value
 * - name="value"
 * - name='value'
 * Note "name=" is valid and means a field with empty value.
 * TODO: so far, quote characters are not permitted WITHIN quoted values.
 */
static int
parseNameValue(const char *const __restrict__ str,
	const size_t strLen, 
	size_t *const __restrict__ offs,
	struct json_object *const __restrict__ valroot)
{
	int r = LN_WRONGPARSER;
	size_t i = *offs;

	const size_t iName = i;
	while(i < strLen && isValidNameChar(str[i]))
		++i;
	if(i == iName || str[i] != '=')
		goto done; /* no name at all! */

	const size_t lenName = i - iName;
	++i; /* skip '=' */

	const size_t iVal = i;
	while(i < strLen && !isspace(str[i]))
		++i;
	const size_t lenVal = i - iVal;

	/* parsing OK */
	*offs = i;
	r = 0;

	if(valroot == NULL)
		goto done;

	char *name;
	CHKN(name = malloc(lenName+1));
	memcpy(name, str+iName, lenName);
	name[lenName] = '\0';
	json_object *json;
	CHKN(json = json_object_new_string_len(str+iVal, lenVal));
	json_object_object_add(valroot, name, json);
	free(name);
done:
	return r;
}

/**
 * Parse CEE syslog.
 * This essentially is a JSON parser, with additional restrictions:
 * The message must start with "@cee:" and json must immediately follow (whitespace permitted).
 * after the JSON, there must be no other non-whitespace characters.
 * In other words: the message must consist of a single JSON object,
 * only.
 * added 2015-04-28 by rgerhards, v1.1.2
 */
PARSER_Parse(CEESyslog)
	size_t i = *offs;
	struct json_tokener *tokener = NULL;
	struct json_object *json = NULL;

	if(strLen < i + 7  || /* "@cee:{}" is minimum text */
	   str[i]   != '@' ||
	   str[i+1] != 'c' ||
	   str[i+2] != 'e' ||
	   str[i+3] != 'e' ||
	   str[i+4] != ':')
	   	goto done;
	
	/* skip whitespace */
	for(i += 5 ; i < strLen && isspace(str[i]) ; ++i)
		/* just skip */;

	if(i == strLen || str[i] != '{')
		goto done;
		/* note: we do not permit arrays in CEE mode */

	if((tokener = json_tokener_new()) == NULL)
		goto done;

	json = json_tokener_parse_ex(tokener, str+i, (int) (strLen - i));

	if(json == NULL)
		goto done;

	if(i + tokener->char_offset != strLen)
		goto done;

	/* success, persist */
	*parsed =  strLen;
	r = 0; /* success */

	if(value != NULL) {
		*value = json;
		json = NULL; /* do NOT free below! */
	}

done:
	if(tokener != NULL)
		json_tokener_free(tokener);
	if(json != NULL)
		json_object_put(json);
	return r;
}

/**
 * Parser for name/value pairs.
 * On entry must point to alnum char. All following chars must be
 * name/value pairs delimited by whitespace up until the end of string.
 * For performance reasons, this works in two stages. In the first
 * stage, we only detect if the motif is correct. The second stage is
 * only called when we know it is. In it, we go once again over the
 * message again and actually extract the data. This is done because
 * data extraction is relatively expensive and in most cases we will
 * have much more frequent mismatches than matches.
 * added 2015-04-25 rgerhards
 */
PARSER_Parse(NameValue)
	size_t i = *offs;

	/* stage one */
	while(i < strLen) {
		CHKR(parseNameValue(str, strLen, &i, NULL));
		while(i < strLen && isspace(str[i]))
			++i;
	}

	/* success, persist */
	*parsed = i - *offs;
	r = 0; /* success */

	/* stage two */
	if(value == NULL)
		goto done;

	i = *offs;
	CHKN(*value = json_object_new_object());
	while(i < strLen) {
		CHKR(parseNameValue(str, strLen, &i, *value));
		while(i < strLen && isspace(str[i]))
			++i;
	}

	/* TODO: fix mem leak if alloc json fails */

done:
	return r;
}

/**
 * Parse a MAC layer address.
 * The standard (IEEE 802) format for printing MAC-48 addresses in
 * human-friendly form is six groups of two hexadecimal digits,
 * separated by hyphens (-) or colons (:), in transmission order
 * (e.g. 01-23-45-67-89-ab or 01:23:45:67:89:ab ).
 * This form is also commonly used for EUI-64.
 * from: http://en.wikipedia.org/wiki/MAC_address
 *
 * This parser must start on a hex digit.
 * added 2015-05-04 by rgerhards, v1.1.2
 */
PARSER_Parse(MAC48)
	size_t i = *offs;
	char delim;

	if(strLen < i + 17 || /* this motif has exactly 17 characters */
	   !isxdigit(str[i]) ||
	   !isxdigit(str[i+1])
	   )
		FAIL(LN_WRONGPARSER);

	if(str[i+2] == ':')
		delim = ':';
	else if(str[i+2] == '-')
		delim = '-';
	else
		FAIL(LN_WRONGPARSER);

	/* first byte ok */
	if(!isxdigit(str[i+3])  ||
	   !isxdigit(str[i+4])  ||
	   str[i+5] != delim    || /* 2nd byte ok */
	   !isxdigit(str[i+6])  ||
	   !isxdigit(str[i+7])  ||
	   str[i+8] != delim    || /* 3rd byte ok */
	   !isxdigit(str[i+9])  ||
	   !isxdigit(str[i+10]) ||
	   str[i+11] != delim   || /* 4th byte ok */
	   !isxdigit(str[i+12]) ||
	   !isxdigit(str[i+13]) ||
	   str[i+14] != delim   || /* 5th byte ok */
	   !isxdigit(str[i+15]) ||
	   !isxdigit(str[i+16])    /* 6th byte ok */
	   )
		FAIL(LN_WRONGPARSER);

	/* success, persist */
	*parsed = 17;
	r = 0; /* success */

	if(value != NULL) {
		CHKN(*value = json_object_new_string_len(str+i, 17));
	}

done:
	return r;
}


/* This parses the extension value and updates the index
 * to point to the end of it.
 */
static int
cefParseExtensionValue(const char *const __restrict__ str,
	const size_t strLen,
	size_t *__restrict__ iEndVal)
{
	int r = 0;
	size_t i = *iEndVal;
	size_t iLastWordBegin;
	/* first find next unquoted equal sign and record begin of
	 * last word in front of it - this is the actual end of the
	 * current name/value pair and the begin of the next one.
	 */
	int hadSP = 0;
	int inEscape = 0;
	for(iLastWordBegin = 0 ; i < strLen ; ++i) {
		if(inEscape) {
			if(str[i] != '=' &&
			   str[i] != '\\' &&
			   str[i] != 'r' &&
			   str[i] != 'n')
			FAIL(LN_WRONGPARSER);
			inEscape = 0;
		} else {
			if(str[i] == '=') {
				break;
			} else if(str[i] == '\\') {
				inEscape = 1;
			} else if(str[i] == ' ') {
				hadSP = 1;
			} else {
				if(hadSP) {
					iLastWordBegin = i;
					hadSP = 0;
				}
			}
		}
	}

	/* Note: iLastWordBegin can never be at offset zero, because
	 * the CEF header starts there!
	 */
	if(i < strLen) {
		*iEndVal = (iLastWordBegin == 0) ? i : iLastWordBegin - 1;
	} else {
		*iEndVal = i;
	}
done:
	return r;
}

/* must be positioned on first char of name, returns index
 * of end of name.
 * Note: ArcSight violates the CEF spec ifself: they generate
 * leading underscores in their extension names, which are
 * definetly not alphanumeric. We still accept them...
 * They also seem to use dots.
 */
static int
cefParseName(const char *const __restrict__ str,
	const size_t strLen,
	size_t *const __restrict__ i)
{
	int r = 0;
	while(*i < strLen && str[*i] != '=') {
		if(!(isalnum(str[*i]) || str[*i] == '_' || str[*i] == '.'))
			FAIL(LN_WRONGPARSER);
		++(*i);
	}
done:
	return r;
}

/* parse CEF extensions. They are basically name=value
 * pairs with the ugly exception that values may contain
 * spaces but need NOT to be quoted. Thankfully, at least
 * names are specified as being alphanumeric without spaces
 * in them. So we must add a lookahead parser to check if
 * a word is a name (and thus the begin of a new pair) or
 * not. This is done by subroutines.
 */
static int
cefParseExtensions(const char *const __restrict__ str,
	const size_t strLen,
	size_t *const __restrict__ offs,
	json_object *const __restrict__ jroot)
{
	int r = 0;
	size_t i = *offs;
	size_t iName, lenName;
	size_t iValue, lenValue;
	char *name = NULL;
	char *value = NULL;

	while(i < strLen) {
		while(i < strLen && str[i] == ' ')
			++i;
		iName = i;
		CHKR(cefParseName(str, strLen, &i));
		if(i+1 >= strLen || str[i] != '=')
			FAIL(LN_WRONGPARSER);
		lenName = i - iName;
		++i; /* skip '=' */

		iValue = i;
		CHKR(cefParseExtensionValue(str, strLen, &i));
		lenValue = i - iValue;

		++i; /* skip past value */

		if(jroot != NULL) {
			CHKN(name = malloc(sizeof(char) * (lenName + 1)));
			memcpy(name, str+iName, lenName);
			name[lenName] = '\0';
			CHKN(value = malloc(sizeof(char) * (lenValue + 1)));
			/* copy value but escape it */
			size_t iDst = 0;
			for(size_t iSrc = 0 ; iSrc < lenValue ; ++iSrc) {
				if(str[iValue+iSrc] == '\\') {
					++iSrc; /* we know the next char must exist! */
					switch(str[iValue+iSrc]) {
					case '=':	value[iDst] = '=';
							break;
					case 'n':	value[iDst] = '\n';
							break;
					case 'r':	value[iDst] = '\r';
							break;
					case '\\':	value[iDst] = '\\';
							break;
					}
				} else {
					value[iDst] = str[iValue+iSrc];
				}
				++iDst;
			}
			value[iDst] = '\0';
			json_object *json;
			CHKN(json = json_object_new_string(value));
			json_object_object_add(jroot, name, json);
			free(name); name = NULL;
			free(value); value = NULL;
		}
	}

	*offs = strLen; /* this parser consume everything or fails */

done:
	free(name);
	free(value);
	return r;
}

/* gets a CEF header field. Must be positioned on the
 * first char after the '|' in front of field.
 * Note that '|' may be escaped as "\|", which also means
 * we need to supprot "\\" (see CEF spec for details).
 * We return the string in *val, if val is non-null. In
 * that case we allocate memory that the caller must free.
 * This is necessary because there are potentially escape
 * sequences inside the string.
 */
static int
cefGetHdrField(const char *const __restrict__ str,
	const size_t strLen,
	size_t *const __restrict__ offs,
	char **val)
{
	int r = 0;
	size_t i = *offs;
	assert(str[i] != '|');
	while(i < strLen && str[i] != '|') {
		if(str[i] == '\\') {
			++i; /* skip esc char */
			if(str[i] != '\\' && str[i] != '|')
				FAIL(LN_WRONGPARSER);
		}
		++i; /* scan to next delimiter */
	}

	if(str[i] != '|')
		FAIL(LN_WRONGPARSER);

	const size_t iBegin = *offs;
	/* success, persist */
	*offs = i + 1;

	if(val == NULL) {
		r = 0;
		goto done;
	}
	
	const size_t len = i - iBegin;
	CHKN(*val = malloc(len + 1));
	size_t iDst = 0;
	for(size_t iSrc = 0 ; iSrc < len ; ++iSrc) {
		if(str[iBegin+iSrc] == '\\')
			++iSrc; /* we already checked above that this is OK! */
		(*val)[iDst++] = str[iBegin+iSrc];
	}
	(*val)[iDst] = 0;
	r = 0;
done:
	return r;
}

/**
 * Parser for ArcSight Common Event Format (CEF) version 0.
 * added 2015-05-05 by rgerhards, v1.1.2
 */
PARSER_Parse(CEF)
	size_t i = *offs;
	char *vendor = NULL;
	char *product = NULL;
	char *version = NULL;
	char *sigID = NULL;
	char *name = NULL;
	char *severity = NULL;

	/* minumum header: "CEF:0|x|x|x|x|x|x|" -->  17 chars */
	if(strLen < i + 17 ||
	   str[i]   != 'C' ||
	   str[i+1] != 'E' ||
	   str[i+2] != 'F' ||
	   str[i+3] != ':' ||
	   str[i+4] != '0' ||
	   str[i+5] != '|'
	   )	FAIL(LN_WRONGPARSER);
	
	i += 6; /* position on '|' */

	CHKR(cefGetHdrField(str, strLen, &i, (value == NULL) ? NULL : &vendor));
	CHKR(cefGetHdrField(str, strLen, &i, (value == NULL) ? NULL : &product));
	CHKR(cefGetHdrField(str, strLen, &i, (value == NULL) ? NULL : &version));
	CHKR(cefGetHdrField(str, strLen, &i, (value == NULL) ? NULL : &sigID));
	CHKR(cefGetHdrField(str, strLen, &i, (value == NULL) ? NULL : &name));
	CHKR(cefGetHdrField(str, strLen, &i, (value == NULL) ? NULL : &severity));
	++i; /* skip over terminal '|' */

	/* OK, we now know we have a good header. Now, we need
	 * to process extensions.
	 * This time, we do NOT pre-process the extension, but rather
	 * persist them directly to JSON. This is contrary to other
	 * parsers, but as the CEF header is pretty unique, this time
	 * it is exteremely unlike we will get a no-match during
	 * extension processing. Even if so, nothing bad happens, as
	 * the extracted data is discarded. But the regular case saves
	 * us processing time and complexity. The only time when we
	 * cannot directly process it is when the caller asks us not
	 * to persist the data. So this must be handled differently.
	 */
	 size_t iBeginExtensions = i;
	 CHKR(cefParseExtensions(str, strLen, &i, NULL));

	/* success, persist */
	*parsed = i - *offs;
	r = 0; /* success */

	if(value != NULL) {
		CHKN(*value = json_object_new_object());
		json_object *json;
		CHKN(json = json_object_new_string(vendor));
		json_object_object_add(*value, "DeviceVendor", json);
		CHKN(json = json_object_new_string(product));
		json_object_object_add(*value, "DeviceProduct", json);
		CHKN(json = json_object_new_string(version));
		json_object_object_add(*value, "DeviceVersion", json);
		CHKN(json = json_object_new_string(sigID));
		json_object_object_add(*value, "SignatureID", json);
		CHKN(json = json_object_new_string(name));
		json_object_object_add(*value, "Name", json);
		CHKN(json = json_object_new_string(severity));
		json_object_object_add(*value, "Severity", json);

		json_object *jext;
		CHKN(jext = json_object_new_object());
		json_object_object_add(*value, "Extensions", jext);

		i = iBeginExtensions;
		cefParseExtensions(str, strLen, &i, jext);
	}

done:
	if(r != 0 && value != NULL && *value != NULL) {
		json_object_put(*value);
		value = NULL;
	}
	free(vendor);
	free(product);
	free(version);
	free(sigID);
	free(name);
	free(severity);
	return r;
}

/**
 * Parser for Checkpoint LEA on-disk format.
 * added 2015-06-18 by rgerhards, v1.1.2
 */
PARSER_Parse(CheckpointLEA)
	size_t i = *offs;
	size_t iName, lenName;
	size_t iValue, lenValue;
	int foundFields = 0;
	char *name = NULL;
	char *val = NULL;

	while(i < strLen) {
		while(i < strLen && str[i] == ' ') /* skip leading SP */
			++i;
		if(i == strLen) { /* OK if just trailing space */
			if(foundFields == 0)
				FAIL(LN_WRONGPARSER);
			break; /* we are done with the loop, all processed */
		} else {
			++foundFields;
		}
		iName = i;
		/* TODO: do a stricter check? ... but we don't have a spec */
		while(i < strLen && str[i] != ':') {
			++i;
		}
		if(i+1 >= strLen || str[i] != ':')
			FAIL(LN_WRONGPARSER);
		lenName = i - iName;
		++i; /* skip ':' */

		while(i < strLen && str[i] == ' ') /* skip leading SP */
			++i;
		iValue = i;
		while(i < strLen && str[i] != ';') {
			++i;
		}
		if(i+1 > strLen || str[i] != ';')
			FAIL(LN_WRONGPARSER);
		lenValue = i - iValue;
		++i; /* skip ';' */

		if(value != NULL) {
			CHKN(name = malloc(sizeof(char) * (lenName + 1)));
			memcpy(name, str+iName, lenName);
			name[lenName] = '\0';
			CHKN(val = malloc(sizeof(char) * (lenValue + 1)));
			memcpy(val, str+iValue, lenValue);
			val[lenValue] = '\0';
			if(*value == NULL)
				CHKN(*value = json_object_new_object());
			json_object *json;
			CHKN(json = json_object_new_string(val));
			json_object_object_add(*value, name, json);
			free(name); name = NULL;
			free(val); val = NULL;
		}
	}

	/* success, persist */
	*parsed =  i - *offs;
	r = 0; /* success */

done:
	free(name);
	free(val);
	if(r != 0 && value != NULL && *value != NULL) {
		json_object_put(*value);
		value = NULL;
	}
	return r;
}


struct data_Repeat {
	ln_pdag *parser;
	ln_pdag *while_cond;
};
/**
 * "repeat" special parser.
 */
PARSER_Parse(Repeat)
	struct data_Repeat *const data = (struct data_Repeat*) pdata;
	struct ln_pdag *endNode = NULL;
	size_t longest_path = 0;
	size_t strtoffs = *offs;
	struct json_object *json_arr = NULL;

	do {
		struct json_object *parsed_value = json_object_new_object();
		r = ln_normalizeRec(data->parser, str, strLen, strtoffs, 1,
				    &longest_path, parsed_value, &endNode);
		strtoffs = longest_path;
		ln_dbgprintf(ctx, "repeat parser returns %d, parsed %zu, json: %s",
			r, longest_path, json_object_to_json_string(parsed_value));

		if(json_arr == NULL) {
			json_arr = json_object_new_array();
		}
		json_object_array_add(json_arr, parsed_value);
		ln_dbgprintf(ctx, "arr: %s", json_object_to_json_string(json_arr));

		/* now check if we shall continue */
		longest_path = 0;
		r = ln_normalizeRec(data->while_cond, str, strLen, strtoffs, 1,
				    &longest_path, NULL, &endNode);
		ln_dbgprintf(ctx, "repeat while returns %d, parsed %zu",
			r, longest_path);
		if(r == 0)
			strtoffs = longest_path;
	} while(r == 0);

	/* success, persist */
	*parsed = strtoffs - *offs;
	if(value == NULL) {
		json_object_put(json_arr);
	} else {
		*value = json_arr;
	}
	r = 0; /* success */
	return r;
}
PARSER_Construct(Repeat)
{
	int r = 0;
	struct data_Repeat *data = (struct data_Repeat*) calloc(1, sizeof(struct data_Repeat));
	struct ln_pdag *endnode; /* we need this fo ln_pdagAddParser, which updates its param! */

	if(json == NULL)
		goto done;

	json_object_object_foreach(json, key, val) {
		if(!strcmp(key, "parser")) {
			endnode = data->parser = ln_newPDAG(ctx); 
			CHKR(ln_pdagAddParser(ctx, &endnode, val));
			endnode->flags.isTerminal = 1;
		} else if(!strcmp(key, "while")) {
			endnode = data->while_cond = ln_newPDAG(ctx); 
			CHKR(ln_pdagAddParser(ctx, &endnode, val));
			endnode->flags.isTerminal = 1;
		} else {
			ln_errprintf(ctx, 0, "invalid param for hexnumber: %s",
				 json_object_to_json_string(val));
		}
	}

done:
	if(data->parser == NULL || data->while_cond == NULL) {
		ln_errprintf(ctx, 0, "repeat parser needs 'parser','while' parameters");
		ln_destructRepeat(ctx, data);
		r = LN_BADCONFIG;
	} else {
		*pdata = data;
	}
	return r;
}
PARSER_Destruct(Repeat)
{
	struct data_Repeat *data = (struct data_Repeat*) calloc(1, sizeof(struct data_Repeat));
	if(data->parser != NULL)
		ln_pdagDelete(data->parser);
	if(data->while_cond != NULL)
		ln_pdagDelete(data->while_cond);
	free(pdata);
}

