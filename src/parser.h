/*
 *
 * liblognorm - a fast samples-based log normalization library
 * Copyright 2010 by Rainer Gerhards and Adiscon GmbH.
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
#ifndef LIBLOGNORM_PARSER_H_INCLUDED
#define	LIBLOGNORM_PARSER_H_INCLUDED
#include "ptree.h"

/**
 * Parser interface.
 * @param[in] str input string
 * @param[in] offs offset where parsing has to start inside str.
 * @param[in] ed string with extra data (if needed)
 * @param[out] parsed int number of characters consumed by the parser.
 * @return 0 on success, something else otherwise
 */


/** 
 * Parser for RFC5424 date.
 */
int ln_parseRFC5424Date(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);

/** 
 * Parser for RFC3164 date.
 */
int ln_parseRFC3164Date(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);

/** 
 * Parser for numbers.
 */
int ln_parseNumber(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);


/** 
 * Parser for Words (SP-terminated strings).
 */
int ln_parseWord(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);


/** 
 * Parser for Alphabetic words (no numbers, punct, ctrl, space).
 */
int ln_parseAlpha(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);


/** 
 * Parse everything up to a specific character.
 */
int ln_parseCharTo(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);

/** 
 * Parse everything up to a specific character (relaxed constraints, suitable for CSV)
 */
int ln_parseCharSeparated(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);


/** 
 * Get everything till the rest of string.
 */
int ln_parseRest(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);


/** 
 * Parse a quoted string.
 */
int ln_parseQuotedString(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);

/** 
 * Parse an ISO date.
 */
int ln_parseISODate(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);


/** 
 * Parse a timestamp in 12hr format.
 */
int ln_parseTime12hr(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);


/** 
 * Parse a timestamp in 24hr format.
 */
int ln_parseTime24hr(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);

/** 
 * Parser for IPv4 addresses.
 */
int ln_parseIPv4(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);

/** 
 * Get all tokens separated by tokenizer-string as array.
 */
int ln_parseTokenized(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);

void* tokenized_parser_data_constructor(ln_fieldList_t *node, ln_ctx ctx);
void tokenized_parser_data_destructor(void** dataPtr);

#ifdef FEATURE_REGEXP
/** 
 * Get field matching regex
 */
int ln_parseRegex(const char *str, size_t strlen, size_t *offs, const ln_fieldList_t *node, size_t *parsed, struct json_object **value);

void* regex_parser_data_constructor(ln_fieldList_t *node, ln_ctx ctx);
void regex_parser_data_destructor(void** dataPtr);
#endif


#endif /* #ifndef LIBLOGNORM_PARSER_H_INCLUDED */
