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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "liblognorm.h"
#include "internal.h"
#include "parser.h"
#include "syntaxes.h"
//#include "logrecord.h"

#define MAXLINE 32*1024

struct logrec_node {
	struct logrec_node *sibling;
	struct logrec_node *child; /* NULL: end of record */
	int nterm; /* number of times this was the terminal node */
	int8_t ntype;
	union {
		char *ltext; /* the literal text */
		int64_t number; /* all integer types */
	} val;
};
typedef struct logrec_node logrec_node_t;

logrec_node_t *root = NULL;

logrec_node_t *
logrec_newNode(char *word)
{
	logrec_node_t *const node = calloc(1, sizeof(logrec_node_t));
	node->child = NULL;
	node->child = NULL;
	node->val.ltext = word;
	return node;
}

#if 0
void
logrec_newChild(logrec_node_t *const parent)
{
	assert(parent->child == NULL);
	parent->child = logrec_newNode();
}

void
logrec_newSibling(logrec_node_t *const curr_sibling)
{
	assert(curr_sibling->sibling == NULL);
	curr_sibling->sibling = logrec_newNode();
}
#endif

#if 0
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
#endif

#if 0
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
#endif

void
treePrint(logrec_node_t *node, const int level)
{
	while(node != NULL) {
		printf("%2d:", level);
		for(int i = 0 ; i < level ; ++i)
			printf("    ");
		printf("%s", node->val.ltext);
		if(node->nterm)
			printf(" [nterm %d]", node->nterm);
		printf("\n");
		treePrint(node->child, level + 1);
		node = node->sibling;
	}
}

char *
getWord(char **const line)
{
	char *ln = *line;
	if(*ln == '\0')
		return NULL;
	size_t i;
	for(i = 0 ; ln[i] && isspace(ln[i]) ; ++i)
		/* EMPTY - skip spaces */;
	const size_t begin_word = i;
	for( ; ln[i] && !isspace(ln[i]) ; ++i)
		/* EMPTY - just count */;
	if(begin_word == i) /* only trailing spaces? */
		return NULL;
	const size_t wordlen = i - begin_word;
	char *const word = malloc(wordlen + 1);
	memcpy(word, ln+begin_word, wordlen);
	word[wordlen] = '\0';
	*line = ln+i;
	return word;
}

void
treeAddChildToParent(logrec_node_t *parent,
	logrec_node_t *child)
{
	if(parent == NULL)
		root = child;
	else
		parent->child = child;
}

logrec_node_t *
treeAddToLevel(logrec_node_t *level,
	char *word
	)
{
	logrec_node_t *existing, *prev = NULL;

	for(existing = level->child ; existing != NULL ; existing = existing->sibling) {
		if(!strcmp(existing->val.ltext, word)) {
			break;
		}
		prev = existing;
	}

	if(existing == NULL) {
		existing = logrec_newNode(word);
		if(prev == NULL) {
			/* first child of parent node */
			//treeAddChildToParent(level, existing);
			level->child = existing;
		} else {
			/* new sibling */
			prev->sibling = existing;
		}
	}

	return existing;
}

void
treeAddLine(char *ln)
{
	char *word;
	logrec_node_t *level = root;
	while(1) {
		if((word = getWord(&ln)) == NULL) {
			++level->nterm;
			break;
		}
		//printf("word: '%s'\n", word);
		level = treeAddToLevel(level, word);
	}
}

void
preprocessLine(const char *const __restrict__ buf,
	const size_t buflen,
	char *const bufout)
{
	static int lnCnt = 1;
	size_t nproc;
	char *tocopy;
	size_t tocopylen;
	size_t iout;

	printf("line %d: %s\n", lnCnt, buf);
	iout = 0;
	for(size_t i = 0 ; i < buflen ; ) {
		if(syntax_ipv4(buf+i, buflen-i, NULL, &nproc)) {
			tocopy = "%ipv4%";
		} else if(ln_parseRFC3164Date(buf, buflen, &i, NULL, &nproc, NULL) == 0) {
			tocopy = "%date-rfc3164%";
		} else if(syntax_posint(buf+i, buflen-i, NULL, &nproc)) {
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
		}
		i += nproc;
	}
	bufout[iout] = '\0';
	printf("outline %d: %s\n", lnCnt, bufout);
	++lnCnt;
}
int
processFile(FILE *fp)
{
	char lnbuf[MAXLINE];
	char lnpreproc[MAXLINE];
	//logrecord_t * logrec;

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
			//processLine(lnbuf, i, &logrec);
			//logrecPrint(logrec);
			preprocessLine(lnbuf, i, lnpreproc);
			treeAddLine(lnpreproc);
		}
	}

	treePrint(root, 0);
	return 0;
}


int
main(int __attribute((unused)) argc, char __attribute((unused)) *argv[])
{
	int r;
	root = logrec_newNode("[ROOT]");
	r = processFile(stdin);
	return r;
}
