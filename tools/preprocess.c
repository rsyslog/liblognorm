/* clustering log records
 *
 * Copyright 2015 Rainer Gerhards
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *       http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>

#include "liblognorm.h"
#include "internal.h"
#include "parser.h"
#include "syntaxes.h"
#include "logrecord.h"

#define MAXLINE 32*1024

logrecord_t *
logrec_newNode(logrecord_t ** root, logrecord_t **node) {
	if(*root == NULL) {
		*root = malloc(sizeof(logrecord_t));
		*node = *root;
	} else {
		(*node)->next = malloc(sizeof(logrecord_t));
		*node = (*node)->next;
	}
	(*node)->next = NULL;
	return *node;
}

void
logrecPrint(logrecord_t *__restrict__ logrec)
{
	while(logrec != NULL)  {
		printf("logrec %p, type %d", logrec, logrec->ntype);
		switch(logrec->ntype) {
		case LRN_SYNTAX_LITERAL_TEXT:
			printf(" [%s]", logrec->val.ltext);
			break;
		case LRN_SYNTAX_IPV4:
			printf(" IPv4_Address");
			break;
		case LRN_SYNTAX_INT_POSITIVE:
			printf(" positive_integer");
			break;
		case LRN_SYNTAX_DATE_RFC3164:
			printf(" rfc3164_date");
			break;
		default:
			break;
		}
		printf("\n");
		logrec = logrec->next;
	}
}

static int
addTextNode(logrecord_t *const __restrict__ node,
	const char *const __restrict__ buf,
	const size_t startText,
	const size_t endText)
{
	int r = 0;
	const size_t lenText = endText - startText + 1;
	node->ntype = LRN_SYNTAX_LITERAL_TEXT;
	CHKN(node->val.ltext = malloc(lenText + 1));
	memcpy(node->val.ltext, buf+startText, lenText);
	node->val.ltext[lenText] = '\0';
done:	
	return r;
}

/* the following macro is used to check if a literal text
 * part is terminated and needs to be added as record node.
 * This check occurs frequently and thus is done in a macro.
 */
#define CHECK_TEXTNODE \
printf("strtText %zu, i %zu, buf[%zu...]: %.40s\n", strtText, i, i, buf+i);\
	if(i != 0 && strtText != i) { \
		CHKN(logrec_newNode(logrec, &node)); \
		CHKR(addTextNode(node, buf, strtText, i-1)); \
	}

int
processLine(const char *const __restrict__ buf,
	const size_t buflen,
	logrecord_t **const __restrict__ logrec)
{
	int r = 0;
	static int lnCnt = 1;
	size_t nproc;
	char *tocopy;
	size_t tocopylen;
	size_t iout;
	size_t strtText;
	size_t i;
	char bufout[MAXLINE];
	logrecord_t *node;

	printf("line %d: %s\n", lnCnt, buf);
	*logrec = NULL;
	iout = 0;
	strtText = 0;
	for(i = 0 ; i < buflen ; ) {
		if(ln_parseRFC3164Date(buf, buflen, &i, NULL, &nproc, NULL) == 0) {
			CHECK_TEXTNODE
			CHKN(logrec_newNode(logrec, &node));
			node->ntype = LRN_SYNTAX_DATE_RFC3164;
			tocopy = "%date-rfc3164%";
		} else if(syntax_ipv4(buf+i, buflen-i, NULL, &nproc)) {
			CHECK_TEXTNODE
			CHKN(logrec_newNode(logrec, &node));
			node->ntype = LRN_SYNTAX_IPV4;
			tocopy = "%ipv4%";
		} else if(syntax_posint(buf+i, buflen-i, NULL, &nproc)) {
			CHECK_TEXTNODE
			CHKN(logrec_newNode(logrec, &node));
			node->ntype = LRN_SYNTAX_INT_POSITIVE;
			tocopy = "%posint%";
		} else {
			tocopy = NULL;
			nproc = 1;
		}

		/* copy to output buffer */
		if(tocopy == NULL) {
			bufout[iout++] = buf[i];
		} else {
			tocopylen = strlen(tocopy); // do this in lower lever
			memcpy(bufout+iout, tocopy, tocopylen);
			iout += tocopylen;
			strtText = i + nproc;
		}
		i += nproc;
	}
	if(logrec == NULL)
		CHKN(logrec_newNode(logrec, &node));
	CHECK_TEXTNODE
	bufout[iout] = '\0';
	printf("outline %d: %s\n", lnCnt, bufout);
	++lnCnt;
done:	return r;
}
#undef CHECK_TEXTNODE

int
processFile(FILE *fp)
{
	char lnbuf[MAXLINE];
	logrecord_t * logrec;

	while(!feof(fp)) {
		size_t i;
		for(i = 0 ; i < sizeof(lnbuf)-1 ; ++i) {
			const int c = fgetc(fp);
			if(c == EOF || c == '\n')
				break;
			lnbuf[i] = c;
		}
		lnbuf[i] = '\0';
		if(i > 0) {
			processLine(lnbuf, i, &logrec);
			logrecPrint(logrec);
		}
	}

	return 0;
}


int
main(int __attribute((unused)) argc, char __attribute((unused)) *argv[])
{
	int r;
	r = processFile(stdin);
	return r;
}
