/*
 * Libee - An Event Expression Library inspired by CEE
 * Copyright 2010 by Rainer Gerhards and Adiscon GmbH.
 *
 * This file is part of libee.
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

#include "liblognorm.h"
#include "parser.h"

/* some helpers */
static inline int
hParseInt(unsigned char **buf, es_size_t *lenBuf)
{
	unsigned char *p = *buf;
	es_size_t len = *lenBuf;
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
 * @param[in] ctx context object
 * @param[in] str the to-be-parsed string
 * @param[in/out] offs an offset into the string
 * @param[in] ed string with extra data for parser use
 * @param[out] newVal newly created value
 *
 * They will try to parse out "their" object from the string. If they
 * succeed, they:
 *
 * create a nw ee_value (newVal) and store the obtained value into it
 * update buf and lenBuf to reflect the parsing carried out
 *
 * returns 0 on success and EE_WRONGPARSER if this parser could
 *           not successfully parse (but all went well otherwise) and something
 *           else in case of an error.
 */
#define BEGINParser(ParserName) \
int ln_parse##ParserName(es_str_t *str, es_size_t *offs, \
                      __attribute__((unused)) es_str_t *ed, es_size_t *parsed) \
{ \
	es_size_t r = LN_WRONGPARSER; \
	*parsed = 0;

#define ENDParser \
	goto done; /* supress warnings */ \
done: \
	r = 0; \
	goto fail; /* supress warnings */ \
fail: \
	return r; \
}


/**
 * Parse a TIMESTAMP as specified in RFC5424 (subset of RFC3339).
 */
BEGINParser(RFC5424Date)
	unsigned char *pszTS;
	/* variables to temporarily hold time information while we parse */
	int year;
	int month;
	int day;
	int hour; /* 24 hour clock */
	int minute;
	int second;
	int secfrac;	/* fractional seconds (must be 32 bit!) */
	int secfracPrecision;
	char OffsetMode;	/* UTC offset + or - */
	char OffsetHour;	/* UTC offset in hours */
	int OffsetMinute;	/* UTC offset in minutes */
	es_size_t len;
	es_size_t orglen;
	/* end variables to temporarily hold time information while we parse */

	assert(*offs < es_strlen(str));

	pszTS = es_getBufAddr(str) + *offs;
	len = orglen = es_strlen(str) - *offs;

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
		unsigned char *pszStart = ++pszTS;
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
		++pszTS; /* just skip past it */
		--len;
	}

	/* we had success, so update parse pointer */
	*parsed = orglen - len;

ENDParser


/**
 * Parse a RFC3164 Date.
 */
BEGINParser(RFC3164Date)
	unsigned char *p;
	es_size_t len, orglen;
	/* variables to temporarily hold time information while we parse */
	int month;
	int day;
	//int year = 0; /* 0 means no year provided */
	int hour; /* 24 hour clock */
	int minute;
	int second;

	assert(*offs < es_strlen(str));

	p = es_getBufAddr(str) + *offs;
	orglen = len = es_strlen(str) - *offs;
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
	unsigned char *c;
	es_size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = es_getBufAddr(str);

	for (i = *offs; i < es_strlen(str) && isdigit(c[i]); i++);
	if (i == *offs)
		goto fail;
	
	/* success, persist */
	*parsed = i - *offs;

ENDParser


/**
 * Parse a word.
 * A word is a SP-delimited entity. The parser always works, except if
 * the offset is position on a space upon entry.
 */
BEGINParser(Word)
	unsigned char *c;
	es_size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = es_getBufAddr(str);
	i = *offs;

	/* search end of word */
	while(i < es_strlen(str) && c[i] != ' ') 
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
	unsigned char *c;
	unsigned char cTerm;
	es_size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	assert(es_strlen(ed) == 1);
	cTerm = *(es_getBufAddr(ed));
	c = es_getBufAddr(str);
	i = *offs;

	/* search end of word */
	while(i < es_strlen(str) && c[i] != cTerm) 
		i++;

	if(i == *offs || i == es_strlen(str) || c[i] != cTerm) {
		r = LN_WRONGPARSER;
		goto fail;
	}

	/* success, persist */
	*parsed = i - *offs;

ENDParser


/**
 * Just get everything till the end of string.
 */
BEGINParser(Rest)

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);

	/* success, persist */
	*parsed = es_strlen(str) - *offs;

ENDParser


/**
 * Parse a quoted string. In this initial implementation, escaping of the quote
 * char is not supported. A quoted string is one start starts with a double quote,
 * has some text (not containing double quotes) and ends with the first double
 * quote character seen. The extracted string does NOT include the quote characters.
 * rgerhards, 2011-01-14
 */
BEGINParser(QuotedString)
	unsigned char *c;
	es_size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = es_getBufAddr(str);
	i = *offs;

	if(c[i] != '"')
		goto fail;
	++i;

	/* search end of string */
	while(i < es_strlen(str) && c[i] != '"') 
		i++;

	if(i == es_strlen(str) || c[i] != '"') {
		r = LN_WRONGPARSER;
		goto fail;
	}

	/* success, persist */
	*parsed = i + 1 - *offs; /* "eat" terminal double quote */

ENDParser


/**
 * Parse an ISO date, that is YYYY-MM-DD (exactly this format).
 * Note: we do manual loop unrolling -- this is fast AND efficient.
 * rgerhards, 2011-01-14
 */
BEGINParser(ISODate)
	unsigned char *c;
	es_size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = es_getBufAddr(str);
	i = *offs;

	if(*offs+10 > es_strlen(str))
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
 * Parse a timestamp in 24hr format (exactly HH:MM:SS).
 * Note: we do manual loop unrolling -- this is fast AND efficient.
 * rgerhards, 2011-01-14
 */
BEGINParser(Time24hr)
	unsigned char *c;
	es_size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = es_getBufAddr(str);
	i = *offs;

	if(*offs+8 > es_strlen(str))
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
	unsigned char *c;
	es_size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	c = es_getBufAddr(str);
	i = *offs;

	if(*offs+8 > es_strlen(str))
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
chkIPv4AddrByte(es_str_t *str, es_size_t *offs)
{
	int val = 0;
	int r = 1;	/* default: fail -- simplifies things */
	unsigned char *c = es_getBufAddr(str);
	es_size_t i = *offs;

	if(i == es_strlen(str) || !isdigit(c[i]))
		goto fail;
	val = c[i++] - '0';
	if(i < es_strlen(str) && isdigit(c[i])) {
		val = val * 10 + c[i++] - '0';
		if(i < es_strlen(str) && isdigit(c[i]))
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
	unsigned char *c;
	es_size_t i;

	assert(str != NULL);
	assert(offs != NULL);
	assert(parsed != NULL);
	i = *offs;
	if(es_strlen(str) - i + 1 < 7) {
		/* IPv4 addr requires at least 7 characters */
		goto fail;
	}
	c = es_getBufAddr(str);

	/* byte 1*/
	if(chkIPv4AddrByte(str, &i) != 0) goto fail;
	if(i == es_strlen(str) || c[i++] != '.') goto fail;
	/* byte 2*/
	if(chkIPv4AddrByte(str, &i) != 0) goto fail;
	if(i == es_strlen(str) || c[i++] != '.') goto fail;
	/* byte 3*/
	if(chkIPv4AddrByte(str, &i) != 0) goto fail;
	if(i == es_strlen(str) || c[i++] != '.') goto fail;
	/* byte 4 - we do NOT need any char behind it! */
	if(chkIPv4AddrByte(str, &i) != 0) goto fail;

	/* if we reach this point, we found a valid IP address */
	*parsed = i - *offs;

ENDParser
