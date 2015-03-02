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
#define _GNU_SOURCE
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
	/* the addtl value structure is dumb, and needs to
	 * be replaced with something much better when the initial
	 * experiments work out. But good enough for now.
	 * (bsearch table?)
	 */
	int nwords;	/* size of table */
	char **words;
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

void
logrec_delNode(logrec_node_t *const node)
{
	for(int i = 0 ; i < node->nwords ; ++i)
		free(node->words[i]);
	free(node->words);
	free(node->val.ltext);
	free(node);
}

/* add an additional value to existing node */
void
treeAddVal(logrec_node_t *const __restrict__ node,
	char *const __restrict__ word)
{
	int i;
	int r = 1;
	for(i = 0 ; i < node->nwords ; ++i) {
		r = strcmp(word, node->words[i]);
		if(r == 0)
			break;
	}
	if(r == 0)
		return; /* duplicate */
	/* TODO: here we can insert at the right spot, which makes
	 * bsearch() usable [in the caller, later...]
	 */
	int newnwords = node->nwords + 1;
	node->words = realloc(node->words, sizeof(char*)*newnwords);
	node->words[node->nwords] = word;
	node->nwords = newnwords;
}

/* squash a tree, that is combine nodes that point to nodes
 * without siblings to a single node.
 */
void
treeSquash(logrec_node_t *node)
{
	if(node == NULL) return;
	const int hasSibling = node->sibling == NULL ? 0 : 1;
//printf("new iter, node %s\n", node->val.ltext);
	while(node != NULL) {
		if(!hasSibling && node->child != NULL && node->nwords == 0
		   && node->child->sibling == NULL && node->child->nwords == 0
		   && node->val.ltext[0] != '%' /* do not combine syntaxes */
		   && node->child->val.ltext[0] != '%') {
			char *newword;
			asprintf(&newword, "%s %s", node->val.ltext,
				node->child->val.ltext);
			printf("squshing: %s\n", newword);
			free(node->val.ltext);
			node->val.ltext = newword;
			node->nterm = node->child->nterm; /* TODO: do not combine terminals! */
			logrec_node_t *toDel = node->child;
			node->child = node->child->child;
			logrec_delNode(toDel);
			continue; /* see if we can squash more */
		}
		treeSquash(node->child);
//printf("moving to next node %p -> %p\n", node, node->sibling);
		node = node->sibling;
	}
}


void
treePrintIndent(const int level, const char indicator)
{
	printf("%2d%c:", level, indicator);
	for(int i = 0 ; i < level ; ++i)
		printf("   ");
}

void
treePrint(logrec_node_t *node, const int level)
{
	while(node != NULL) {
		treePrintIndent(level, 'l');
		printf("%s", node->val.ltext);
		if(node->nterm)
			printf(" [nterm %d]", node->nterm);
		printf("\n");
		for(int i = 0 ; i < node->nwords ; ++i) {
			treePrintIndent(level, 'v');
			printf("%s", node->words[i]);
			printf("\n");
		}
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
treeAddToLevel(logrec_node_t *const level,
	char *const word,
	char *const nextword
	)
{
	logrec_node_t *existing, *prev = NULL;

	for(existing = level->child ; existing != NULL ; existing = existing->sibling) {
		if(!strcmp(existing->val.ltext, word)) {
			break;
		}
		prev = existing;
	}

	if(existing == NULL && nextword != NULL) {
		/* we check if the next word is the same, if so, we can
		 * just add this as a different value.
		 */
		logrec_node_t *child;
		for(child = level->child ; child != NULL ; child = child->sibling) {
			if(child->child != NULL
			   && !strcmp(child->child->val.ltext, nextword))
			   	break;
		}
		if(child != NULL) {
			treeAddVal(child, word);
			printf("val %s combine with %s\n", child->val.ltext, word);
			existing = child;
		}
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
	char *nextword; /* we need one-word lookahead for structure tree */
	logrec_node_t *level = root;
	nextword = getWord(&ln);
	while(1) {
		word = nextword;
		if(word == NULL) {
			++level->nterm;
			break;
		}
		nextword = getWord(&ln);
		//printf("word: '%s'\n", word);
		level = treeAddToLevel(level, word, nextword);
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

	//printf("line %d: %s\n", lnCnt, buf);
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
	//printf("outline %d: %s\n", lnCnt, bufout);
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
	treeSquash(root);
	treePrint(root, 0);
	return 0;
}


int
main(int __attribute((unused)) argc, char __attribute((unused)) *argv[])
{
	int r;
	root = logrec_newNode(strdup("[ROOT]"));
	r = processFile(stdin);
	return r;
}
