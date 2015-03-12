/* simple log structure analyzer (slsa)
 *
 * This is a heuristic to mine log structure.
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
/* Learnings (mostly from things that failed)
 * - if we detect IP (and similar things) too early in the process, we
 *   get wrong detections (e.g. for reverse DNS entries)
 * - if we detect IP adresses during value collapsing, we get unacceptable
 *   runtime and memory requirements, as a huge number of different actual
 *   values needs to be stored.
 * ->current solution is to detect them after tokenization but before
 *   adding the the structure tree.
 */

#include "config.h"
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <getopt.h>

#include "liblognorm.h"
#include "internal.h"
#include "parser.h"
#include "syntaxes.h"

#define MAXLINE 32*1024

struct wordflags {
	unsigned
		isSubword : 1,
		isSpecial : 1;	/* indicates this is a special parser */
};

struct wordinfo {
	char *word;
	int occurs;
	struct wordflags flags;
};
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
	struct wordinfo *words;
};
typedef struct logrec_node logrec_node_t;

logrec_node_t *root = NULL;

/* command line options */
static int displayProgress = 0; /* display progress indicators */

/* a stack to keep track of detected (sub)words that
 * need to be processed in the future.
 */
#define SIZE_WORDSTACK 8
static struct wordinfo* wordStack[SIZE_WORDSTACK];
static int wordstackPtr = -1; /* empty */

static void
wordstackPush(struct wordinfo *const wi)
{
	++wordstackPtr;
	if(wordstackPtr >= SIZE_WORDSTACK) {
		fprintf(stderr, "slsa: wordstack too small\n");
		exit(1);
	}
	wordStack[wordstackPtr] = wi;
}

/* returns wordinfo or NULL, if stack is empty */
static struct wordinfo *
wordstackPop(void)
{
	return (wordstackPtr < 0) ? NULL : wordStack[wordstackPtr--];
}

static void
reportProgress(const char *const label)
{
	static unsigned cnt = 0;
	static const char *lastlabel = NULL;
	if(!displayProgress)
		return;
	if(lastlabel == NULL)
		lastlabel = strdup(label);
	if(label == NULL || strcmp(label, lastlabel)) {
		fprintf(stderr, "\r%s: %u - done\n", lastlabel, cnt);
		cnt = 0;
		free((void*)lastlabel);
		lastlabel = (label == NULL) ? NULL : strdup(label);
	} else {
		if(++cnt % 100 == 0)
			fprintf(stderr, "\r%s: %u", label, cnt);
	}
}

#if 0 /* we don't need this at the moment, but want to preserve it
       * in case we can need it again.
       */
static int
qs_compmi(const void *v1, const void *v2)
{
	const struct wordinfo *const w1 =  (struct wordinfo*) v1;
	const struct wordinfo *const w2 =  (struct wordinfo*) v2;
	return strcmp(w1->word, w2->word);
}
static int
bs_compmi(const void *key, const void *mem)
{
	const struct wordinfo *const wi =  (struct wordinfo*) mem;
	return strcmp((char*)key, wi->word);
}
#endif

/* add an additional word to existing node */
void
logrec_addWord(logrec_node_t *const __restrict__ node,
	char *const __restrict__ word)
{
	/* TODO: here we can insert at the right spot, which makes
	 * bsearch() usable [in the caller, later...]
	 */
	int newnwords = node->nwords + 1;
	node->words = realloc(node->words, sizeof(struct wordinfo)*newnwords);
	node->words[node->nwords].word = word;
	node->words[node->nwords].occurs = 1;
	node->nwords = newnwords;
#if 0
	if(node->nwords > 1) {
		qsort(node->words, node->nwords, sizeof(struct wordinfo), qs_compmi); // TODO upgrade
	}
#endif
}

logrec_node_t *
logrec_newNode(char *const word)
{
	logrec_node_t *const node = calloc(1, sizeof(logrec_node_t));
	node->child = NULL;
	node->val.ltext = NULL;
	logrec_addWord(node, word);
	return node;
}

void
logrec_delNode(logrec_node_t *const node)
{
	for(int i = 0 ; i < node->nwords ; ++i)
		free(node->words[i].word);
	free(node->words);
	free(node->val.ltext);
	free(node);
}


/* returns ptr to existing struct wordinfo or NULL, if not found.
 */
static struct wordinfo *
logrec_hasWord(logrec_node_t *const __restrict__ node,
	char *const __restrict__ word)
{
//	return (struct wordinfo*) bsearch(word, node->words, node->nwords, sizeof(struct wordinfo), bs_compmi);
	/* alternative without need to have a binary array -- may make sense... */
	int i;
	for(i = 0 ; i < node->nwords ; ++i) {
		if(!strcmp(node->words[i].word, word))
			break;
	}
	return (i < node->nwords) ? node->words+i : NULL;
}


void
printPrefixes(logrec_node_t *const __restrict__ node,
	const int lenPrefix, const int lenSuffix)
{
	int i;
	const int maxwords = node->nwords > 5 ? 5 : node->nwords;
	printf("prefix %d, suffix %d\n", lenPrefix, lenSuffix);
	for(i = 0 ; i < maxwords ; ++i) {
		const char *const word = node->words[i].word;
		const int lenWord = strlen(word);
		const int strtSuffix = lenWord - lenSuffix;
		int j;
		putchar('"');
		for(j = 0 ; j < lenPrefix ; ++j)
			putchar(word[j]);
		printf("\" \"");
		for( ; j < strtSuffix ; ++j)
			putchar(word[j]);
		printf("\" \"");
		for( ; j < lenWord ; ++j)
			putchar(word[j]);
		printf("\"\n");
	}
}

/* check if there are common prefixes and suffixes and, if so,
 * exteract them.
 */
void
checkPrefixes(logrec_node_t *const __restrict__ node)
{
	if(node->nwords == 1)
		return;

	int i;
	const char *const baseword = node->words[0].word;
	const int lenBaseword = strlen(baseword);
	int lenPrefix = lenBaseword;
	int lenSuffix = lenBaseword;
	for(i = 1 ; i < node->nwords ; ++i) {
		int j;
		/* check prefix */
		if(lenPrefix > 0) {
			for(j = 0 ;
			    j < lenPrefix && node->words[i].word[j] == baseword[j] ;
			    ++j)
				; /* EMPTY - just scan */
			if(j < lenPrefix)
				lenPrefix = j;
		}
		/* check suffix */
		if(lenSuffix > 0) {
			const int lenWord = strlen(node->words[i].word);
			const int jmax = (lenWord < lenSuffix) ? lenWord : lenSuffix;
			for(j = 0 ;
			    j < jmax &&
			      node->words[i].word[lenWord-j-1] == baseword[lenBaseword-j-1] ;
			    ++j)
				; /* EMPTY - just scan */
			if(j < lenSuffix)
				lenSuffix = j;
		}
	}
	if(lenPrefix != 0 || lenSuffix != 0) {
		/* TODO: not only print here, but let user override
		 * (in upcoming "interactive" mode)
		 */
		printPrefixes(node, lenPrefix, lenSuffix);
	}
}

/* squash a tree, that is combine nodes that point to nodes
 * without siblings to a single node.
 */
void
treeSquash(logrec_node_t *node)
{
	if(node == NULL) return;
	reportProgress("squashing");
	const int hasSibling = node->sibling == NULL ? 0 : 1;
//printf("new iter, node %s\n", node->val.ltext);
	while(node != NULL) {
		if(!hasSibling && node->child != NULL && node->nwords == 0
		   && node->child->sibling == NULL && node->child->nwords == 0
		   && node->words[0].word[0] != '%' /* do not combine syntaxes */
		   && node->child->words[0].word[0] != '%') {
			char *newword;
			if(asprintf(&newword, "%s %s", node->words[0].word,
				node->child->words[0].word)) {}; /* silence cc warning */
			printf("squashing: %s\n", newword);
			free(node->words[0].word);
			node->words[0].word = newword;
			node->nterm = node->child->nterm; /* TODO: do not combine terminals! */
			logrec_node_t *toDel = node->child;
			node->child = node->child->child;
			logrec_delNode(toDel);
			continue; /* see if we can squash more */
		}
		checkPrefixes(node);
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
treePrintWordinfo(struct wordinfo *const __restrict__ wi)
{
	printf("%s", wi->word);
	if(wi->occurs > 1)
		printf(" {%d}", wi->occurs);
}

void
treePrint(logrec_node_t *node, const int level)
{
	reportProgress("print");
	while(node != NULL) {
		treePrintIndent(level, 'l');
		treePrintWordinfo(&(node->words[0]));
		if(node->nterm)
			printf(" [nterm %d]", node->nterm);
		printf("\n");
		for(int i = 1 ; i < node->nwords ; ++i) {
			treePrintIndent(level, 'v');
			treePrintWordinfo(&(node->words[i]));
			printf("\n");
		}
		treePrint(node->child, level + 1);
		node = node->sibling;
	}
}

/* TODO: move wordlen to struct wordinfo? */
void
wordDetectSyntax(struct wordinfo *const __restrict__ wi, const size_t wordlen)
{
	size_t nproc;
	if(syntax_posint(wi->word, wordlen, NULL, &nproc) &&
	   nproc == wordlen) {
		free(wi->word);
		wi->word = strdup("%posint%");
		wi->flags.isSpecial = 1;
		goto done;
	}
	if(syntax_ipv4(wi->word, wordlen, NULL, &nproc)) {
		if(nproc == wordlen) {
			free(wi->word);
			wi->word = strdup("%ipv4%");
			wi->flags.isSpecial = 1;
			goto done;
		}
		if(wi->word[nproc] == '/') {
			size_t strtnxt = nproc + 1;
			if(syntax_posint(wi->word+strtnxt, wordlen-strtnxt,
				NULL, &nproc))
				if(strtnxt+nproc == wordlen) {
					free(wi->word);
					wi->word = strdup("%ipv4%");
					wi->flags.isSubword = 1;
					wi->flags.isSpecial = 1;

					struct wordinfo *wit;

					wit = calloc(1, sizeof(struct wordinfo));
					wit->word = strdup("%posint%");
					wit->flags.isSubword = 1;
					wit->flags.isSpecial = 1;
					wordstackPush(wit);

					wit = calloc(1, sizeof(struct wordinfo));
					wit->word = strdup("/");
					wit->flags.isSubword = 1;
					wit->flags.isSpecial = 0;
					wordstackPush(wit);
					goto done;
				}
		}
	}
done:
	return;
}

struct wordinfo *
getWord(char **const line)
{

	struct wordinfo *wi = wordstackPop();
	if(wi != NULL)
		return wi;
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
	wi = calloc(1, sizeof(struct wordinfo));
	wi->word = malloc(wordlen + 1);
	memcpy(wi->word, ln+begin_word, wordlen);
	wi->word[wordlen] = '\0';
	if(wi->word[0] == '%') /* assume already token [TODO: improve] */
		goto done;
	wordDetectSyntax(wi, wordlen);
done:
	*line = ln+i;
	return wi;
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
	struct wordinfo *const wi,
	struct wordinfo *const nextwi
	)
{
	logrec_node_t *existing, *prev = NULL;

	for(existing = level->child ; existing != NULL ; existing = existing->sibling) {
		struct wordinfo *wi_val;
		if((wi_val = logrec_hasWord(existing, wi->word)) != NULL) {
			wi_val->occurs++;
			break;
		}
		prev = existing;
	}

	if(existing == NULL && nextwi != NULL) {
		/* we check if the next word is the same, if so, we can
		 * just add this as a different value.
		 */
		logrec_node_t *child;
		for(child = level->child ; child != NULL ; child = child->sibling) {
			if(child->child != NULL
			   && !strcmp(child->child->words[0].word, nextwi->word))
			   	break;
		}
		if(child != NULL) {
			logrec_addWord(child, wi->word);
			existing = child;
		}
	}

	if(existing == NULL) {
		existing = logrec_newNode(wi->word);
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
	struct wordinfo *wi;
	struct wordinfo *nextwi; /* we need one-word lookahead for structure tree */
	logrec_node_t *level = root;
	nextwi = getWord(&ln);
	while(1) {
		wi = nextwi;
		if(wi == NULL) {
			++level->nterm;
			break;
		}
		nextwi = getWord(&ln);
		level = treeAddToLevel(level, wi, nextwi);
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

	iout = 0;
	for(size_t i = 0 ; i < buflen ; ) {
		/* in this stage, we must only detect syntaxes that we are
		 * very sure to correctly detect AND that *spawn multiple
		 * words*. Otherwise, it is safer to detect them on a
		 * word basis.
		 */
		if(ln_parseRFC3164Date(buf, buflen, &i, NULL, &nproc, NULL) == 0) {
			tocopy = "%date-rfc3164%";
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
	++lnCnt;
}
int
processFile(FILE *fp)
{
	char lnbuf[MAXLINE];
	char lnpreproc[MAXLINE];

	while(!feof(fp)) {
		reportProgress("reading");
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
	reportProgress(NULL);
	return 0;
}


int
main(int argc, char *argv[])
{
	int r;
	int ch;
	static const struct option longopts[] = {
		{ "report-progress",	no_argument,	  0, 'p' },
		{ NULL,		0, 0, 0 }
	};

	while ((ch = getopt_long(argc, argv, "p", longopts, NULL)) != -1) {
		switch (ch) {
		case 'p':		/* file to log */
			displayProgress = 1;
			break;
		case '?':
		default:
		//	usage(stderr);
			fprintf(stderr, "invalid option");
			break;
		}
	}

	root = logrec_newNode(strdup("[ROOT]"));
	r = processFile(stdin);
	return r;
}
