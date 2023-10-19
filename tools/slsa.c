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
 * ------------------------------------------
 * - if we detect IP (and similar things) too early in the process, we
 *   get wrong detections (e.g. for reverse DNS entries)
 * - if we detect IP adresses during value collapsing, we get unacceptable
 *   runtime and memory requirements, as a huge number of different actual
 *   values needs to be stored.
 * ->current solution is to detect them after tokenization but before
 *   adding the the structure tree.
 *
 * - if we split some known syntaxes (like cisco ip/port) into it's parts,
 *   they may get improperly detected in regard to subwords. This especially
 *   happens if there are few servers (or few destination ports), where parts
 *   of the IP (or the port) is considered to be a common prefix, which then
 *   may no longer be properly detected. As it looks, it pays to really have a
 *   much richer set of special parsers, and let the common pre/suffix
 *   detection only be used in rare cases where we do not have a parser.
 *   Invocation may even be treated as the need to check for a new parser.
 *
 *   Along the same lines, common "command words" (like "udp", "tcp") may
 *   also be detected by a specific parser, because otherwise they tend to have
 *   common pre/suffixes (e.g. {"ud","tc"},"p"). This could be done via a
 *   dictionary lookup (bsearch, later to become a single state machine?).
 *
 *   Along these lines, we probably need parses who return *multiple* values,
 *   e.g. the IP address and port. This requires that the field name has a
 *   common prefix. A solution is to return JSON where the node element is
 *   named like the field name, and the members have parser-specific names.
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
#include <limits.h>

#include "json_compatibility.h"
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
	struct logrec_node *parent;
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
	struct wordinfo **words;
};
typedef struct logrec_node logrec_node_t;

logrec_node_t *root = NULL;

typedef struct rule_table_etry rule_table_etry_t;
typedef struct rule_table rule_table_t;
struct rule_table {
	int maxEtry; /* max # entries that fit into table */
	int nxtEtry; /* next free entry */
	rule_table_etry_t **entries;
};

struct rule_table_etry {
	int ntimes;
	char *rule;
};
#define RULE_TABLE_GROWTH 512 /* number of entries rule table grows when too small */

/* command line options */
static int displayProgress = 0; /* display progress indicators */
static int optPrintTree = 0; /* disply internal tree for debugging purposes */
static int optPrintDebugOutput = 0;
static int optSortMultivalues = 1;


/* forward definitions */
void wordDetectSyntax(struct wordinfo *const __restrict__ wi, const size_t wordlen, const int);
void treePrint(logrec_node_t *node, const int level);
static void reportProgress(const char *const label);

/* param word may be NULL, if word is not yet known */
static struct wordinfo *
wordinfoNew(char *const word)
{
	struct wordinfo *const wi = calloc(1, sizeof(struct wordinfo));
	if(wi == NULL) {
		perror("slsa: malloc error struct wordinfo");
		exit(1);
	}
	wi->word = word;
	wi->occurs = 1;
	return wi;
}

static void
wordinfoDelete(struct wordinfo *const wi)
{
//printf("free %p: %s\n", wi->word, wi->word);
	free(wi->word);
	free(wi);
}

static rule_table_t *
ruleTableCreate(void)
{
	rule_table_t *const rt = calloc(1, sizeof(rule_table_t));
	if(rt == NULL) {
		perror("slsa: malloc error ruletable_create");
		exit(1);
	}
	rt->nxtEtry = 0;
	rt->maxEtry = 0;
	return rt;
}

static rule_table_etry_t *
ruleTableEtryCreate(rule_table_t *const __restrict__ rt)
{
	if(rt->nxtEtry == rt->maxEtry) {
		const int newMax = rt->maxEtry + RULE_TABLE_GROWTH;
		rt->entries = realloc(rt->entries, newMax * sizeof(rule_table_t));
		if(rt->entries == NULL) {
			perror("slsa: malloc error ruletable_create");
			exit(1);
		}
		rt->maxEtry = newMax;
	}
	const int etry = rt->nxtEtry;
	rt->nxtEtry++;
	rt->entries[etry] = calloc(1, sizeof(rule_table_etry_t));
	if(rt->entries[etry] == NULL) {
		perror("slsa: malloc error entry ruletable_create");
		exit(1);
	}
	return rt->entries[etry];
}

static void
ruleTablePrint(rule_table_t *const __restrict__ rt)
{
	for(int i = 0 ; i < rt->nxtEtry ; ++i) {
		reportProgress("rule table print");
		printf("%6d times: %s\n", rt->entries[i]->ntimes,
			rt->entries[i]->rule);
	}
}

static void
ruleTableDestroy(rule_table_t *const __restrict__ rt)
{
	for(int i = 0 ; i < rt->nxtEtry ; ++i) {
		free((void*)rt->entries[i]->rule);
		free((void*)rt->entries[i]);
	}
	free((void*) rt->entries);
	free((void*) rt);
}

/* function to quicksort rule table */
static int
qs_comp_rt_etry(const void *v1, const void *v2)
{
	const rule_table_etry_t *const e1 =  *((rule_table_etry_t **) v1);
	const rule_table_etry_t *const e2 =  *((rule_table_etry_t **) v2);
	return -(e1->ntimes - e2->ntimes); /* sort descending! */
}

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

static int
qs_compmi(const void *v1, const void *v2)
{
	const struct wordinfo *const w1 =  *((struct wordinfo**) v1);
	const struct wordinfo *const w2 =  *((struct wordinfo**) v2);
	return strcmp(w1->word, w2->word);
}
#if 0 /* we don't need this at the moment, but want to preserve it
	* in case we can need it again.
	*/
static int
bs_compmi(const void *key, const void *mem)
{
	const struct wordinfo *const wi =  *((struct wordinfo**) mem);
	return strcmp((char*)key, wi->word);
}
#endif

/* add an additional word to existing node */
void
logrec_addWord(logrec_node_t *const __restrict__ node,
	struct wordinfo *const __restrict__ wi)
{
	/* TODO: here we can insert at the right spot, which makes
	 * bsearch() usable [in the caller, later...]
	 */
	/* TODO: alloc in larger increments */
	int newnwords = node->nwords + 1;
	node->words = realloc(node->words, sizeof(struct wordinfo *)*newnwords);
	wi->occurs = 1; /* TODO: layer violation, do in other function */
	node->words[node->nwords] = wi;
	node->nwords = newnwords;
#if 0
	if(node->nwords > 1) {
		qsort(node->words, node->nwords, sizeof(struct wordinfo), qs_compmi); // TODO upgrade
	}
#endif
}

logrec_node_t *
logrec_newNode(struct wordinfo *const wi, struct logrec_node *const parent)
{
	logrec_node_t *const node = calloc(1, sizeof(logrec_node_t));
	node->parent = parent;
	node->child = NULL;
	node->val.ltext = NULL;
	if(wi != NULL)
		logrec_addWord(node, wi);
	return node;
}

void
logrec_delNode(logrec_node_t *const node)
{
	for(int i = 0 ; i < node->nwords ; ++i)
		wordinfoDelete(node->words[i]);
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
		if(!strcmp(node->words[i]->word, word))
			break;
	}
	return (i < node->nwords) ? node->words[i] : NULL;
}


void
printPrefixes(logrec_node_t *const __restrict__ node,
	const int lenPrefix, const int lenSuffix)
{
	if(!optPrintDebugOutput)
		return;
	int i;
	const int maxwords = node->nwords > 5 ? 5 : node->nwords;
	printf("prefix %d, suffix %d\n", lenPrefix, lenSuffix);
	for(i = 0 ; i < maxwords ; ++i) {
		const char *const word = node->words[i]->word;
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

/* Squash duplicate values inside a tree node. This must only be run
 * after tree node values have been modified.
 */
static void
squashDuplicateValues(logrec_node_t *node)
{
	if(node->nwords == 1)
		return;

	/* sort and the remove the easy to find dupes */
	qsort(node->words, node->nwords, sizeof(struct wordinfo*), qs_compmi);
	int iPrev = 0;
	int nsquashed = 0;
	for(int iNext = 1 ; iNext < node->nwords ; ++iNext) {
		if(!strcmp(node->words[iPrev]->word, node->words[iNext]->word)) {
			wordinfoDelete(node->words[iNext]);
			++nsquashed;
		} else { /* OK, new word */
			if(iPrev+1 == iNext)
				iPrev = iNext;
			else {
				node->words[iPrev]->occurs += iNext - iPrev - 1;
				++iPrev;
				node->words[iPrev] = node->words[iNext];
			}
		}
	}
	if(nsquashed) {
		node->nwords -= nsquashed;
		node->words = realloc(node->words,
				sizeof(struct wordinfo *)*node->nwords);
	}
}

/* Disjoin subwords based on a Delimiter.
 * When calling this function, it must be known that the Delimiter
 * is present in *all* words.
 * TODO: think about empty initial subwords (Delimiter in start position!).
 */
static void
disjoinDelimiter(logrec_node_t *node,
	const char Delimiter)
{
	/* first, create node pointers:
	 * we need to update our original node in-place, because otherwise
	 * we change the structure of the tree with a couple of
	 * side effects. As we do not want this, the first subword must
	 * be placed into the current node, and new nodes be
	 * created for the other subwords.
	 */
	char delimword[2];
	delimword[0] = Delimiter;
	delimword[1] = '\0';
	struct wordinfo *const delim_wi = wordinfoNew(strdup(delimword));
	delim_wi->flags.isSubword = 1;
	logrec_node_t *const delim_node = logrec_newNode(delim_wi, node);
	logrec_node_t *const tail_node = logrec_newNode(NULL, delim_node);
	delim_node->child = tail_node;
	tail_node->child = node->child;
	node->child = delim_node;
	delim_node->parent = node;
	if(tail_node->child != NULL)
		tail_node->child->parent = tail_node;

	/* now, do the actual split */
//printf("nodes setup, now doing actual split\n");fflush(stdout);
	char *prevword = NULL;
	for(int i = 0 ; i < node->nwords ; ++i) {
		struct wordinfo *wi;
		char *const delimptr = strchr(node->words[i]->word, Delimiter);
//printf("splitting off tail %d of %d [%p]:'%s' [full: '%s']\n"
//, i, node->nwords, delimptr+1, delimptr+1, node->words[i]->word);fflush(stdout);
		wi = wordinfoNew(strdup(delimptr+1));
		wi->flags.isSubword = 1;
		wordDetectSyntax(wi, strlen(wi->word), 0);
		/* add new word only if not duplicate of previous (reduces dupes) */
		if(prevword == NULL || strcmp(prevword, wi->word)) {
			logrec_addWord(tail_node, wi);
			prevword = wi->word;
		} else {
			wordinfoDelete(wi);
		}
		/* we can now do an in-place update of the old word ;) */
		*delimptr = '\0';
		node->words[i]->flags.isSubword = 1;
		wordDetectSyntax(node->words[i], strlen(node->words[i]->word), 0);
#if 0
		/* but we trim the memory */
		const char *delword = node->words[i]->word;
		node->words[i]->word = strdup(delword);
		free((void*)delword);
#endif
	}

	if(node->nwords > 1) {
		squashDuplicateValues(node);
		squashDuplicateValues(tail_node);
	}
//printf("done disjonDelimiter\n");fflush(stdout);
}


/* Disjoin common prefixes and suffixes. This also triggers a
 * new syntax detection on the remaining variable part.
 */
static void
disjoinCommon(logrec_node_t *node,
	const size_t lenPrefix,
	const size_t lenSuffix)
{
	logrec_node_t *newnode;
	struct wordinfo *newwi;
	char *newword;
	char *word = node->words[0]->word;
	if(lenPrefix > 0) {
		/* we need to update our node in-place, because otherwise
		 * we change the structure of the tree with a couple of
		 * side effects. As we do not want this, the prefix must
		 * be placed into the current node, and new nodes be
		 * created for the other words.
		 */
		newword = malloc(lenPrefix+1);
		memcpy(newword, word, lenPrefix);
		newword[lenPrefix] = '\0';
		newwi = wordinfoNew(newword);
		newwi->flags.isSubword = 1;

		newnode = logrec_newNode(newwi, node);
		newnode->words = node->words;
		newnode->nwords = node->nwords;
		node->nwords = 0;
		node->words = NULL;
		logrec_addWord(node, newwi);
		newnode->child = node->child;
		node->child = newnode;
		node->parent = newnode;

		/* switch node */
		node = newnode;

		for(int i = 0 ; i < node->nwords ; ++i)
			memmove(node->words[i]->word, /* move includes \0 */
				node->words[i]->word+lenPrefix,
				strlen(node->words[i]->word)-lenPrefix+1);
	}
	if(lenSuffix > 0) {
		const size_t lenword = strlen(word);
		size_t iSuffix = lenword-lenSuffix;
		newword = malloc(lenSuffix+1);
		memcpy(newword, word+iSuffix, lenSuffix+1); /* includes \0 */
		newwi = wordinfoNew(newword);
		newwi->flags.isSubword = 1;

		newnode = logrec_newNode(newwi, node);
		newnode->child = node->child;
		if(newnode->child != NULL)
			newnode->child->parent = newnode;
		node->child = newnode;

		for(int i = 0 ; i < node->nwords ; ++i) {
			iSuffix = strlen(node->words[i]->word)-lenSuffix;
			node->words[i]->word[iSuffix] = '\0';
		}
	}

	for(int i = 0 ; i < node->nwords ; ++i) {
		node->words[i]->flags.isSubword = 1;
		wordDetectSyntax(node->words[i], strlen(node->words[i]->word), 0);
	}
	// TODO: squash duplicates only if syntaxes were detected!
	squashDuplicateValues(node);
}

/* find a matching terminator inside a suffix, searchs only
 * within the suffix area. If found, lenPrefix and lenSuffix
 * are update and 1 is returned. Returns 0 if not found.
 * Helper to checkPrefixes.
 */
static int
findMatchingTerm(const char *const __restrict__ word,
	const size_t lenWord,
	size_t potentialNewPrefix,
	int *const __restrict lenPrefix,
	int *const __restrict lenSuffix,
	const char term)
{
	int newSuffix = -1;
	for(int i = 0 ; i < *lenSuffix ; ++i)
		if(word[lenWord-i-1] == term) {
			newSuffix = i+1;
			break;
		}
	if(newSuffix >= 0) {
		*lenSuffix = newSuffix;
		*lenPrefix = potentialNewPrefix;
		return 1;
	}
	return 0;
}

/* returns 1 if Delimiter is found in all words, 0 otherwise */
int
checkCommonDelimiter(logrec_node_t *const __restrict__ node,
	const char Delimiter)
{
	for(int i = 0 ; i < node->nwords ; ++i) {
		if(strlen(node->words[i]->word) < 2 || strchr(node->words[i]->word+1, Delimiter) == NULL)
			return 0;
	}
	return 1;
}

/* returns 1 if braces are found in all words, 0 otherwise */
int
checkCommonBraces(logrec_node_t *const __restrict__ node,
	const char braceOpen,
	const char braceClose)
{
	char *op;
	for(int i = 0 ; i < node->nwords ; ++i) {
		if(strlen(node->words[i]->word) < 2)
			return 0;
		if((op = strchr(node->words[i]->word+1, braceOpen)) == NULL)
			return 0;
		else if(strchr(op+1, braceClose) == NULL)
			return 0;
	}
	return 1;
}

/* check if there are common subword delimiters inside the values. If so,
 * use them to create subwords. Revalute the syntax if done so.
 */
void
checkSubwords(logrec_node_t *const __restrict__ node)
{
//printf("checkSubwords checking node %p: %s\n", node, node->words[0]->word);
	if(checkCommonDelimiter(node, '/')) {
		disjoinDelimiter(node, '/');
	}
	if(checkCommonDelimiter(node, ':')) {
		disjoinDelimiter(node, ':');
	}
	if(checkCommonDelimiter(node, '=')) {
		disjoinDelimiter(node, '=');
	}
#if 0 // this does not work, requies a seperate disjoin operation (4-parts)
	if(checkCommonBraces(node, '[', ']')) {
		disjoinDelimiter(node, '(');
		disjoinDelimiter(node->child->child, ')');
	}
#endif
}

/* check if there are common prefixes and suffixes and, if so,
 * extract them.
 */
void
checkPrefixes(logrec_node_t *const __restrict__ node)
{
	if(node->nwords == 1 || node->words[0]->flags.isSubword)
		return;

	int i;
	const char *const baseword = node->words[0]->word;
	const size_t lenBaseword = strlen(baseword);
	int lenPrefix = lenBaseword;
	int lenSuffix = lenBaseword;
	int shortestWord = INT_MAX;
	for(i = 1 ; i < node->nwords ; ++i) {
		int j;
		/* check prefix */
		if(lenPrefix > 0) {
			for(j = 0 ;
			    j < lenPrefix && node->words[i]->word[j] == baseword[j] ;
			    ++j)
				; /* EMPTY - just scan */
			if(j < lenPrefix)
				lenPrefix = j;
		}
		/* check suffix */
		if(lenSuffix > 0) {
			const int lenWord = strlen(node->words[i]->word);
			if(lenWord < shortestWord)
				shortestWord = lenWord;
			const int jmax = (lenWord < lenSuffix) ? lenWord : lenSuffix;
			for(j = 0 ;
			    j < jmax &&
			      node->words[i]->word[lenWord-j-1] == baseword[lenBaseword-j-1] ;
			    ++j)
				; /* EMPTY - just scan */
			if(j < lenSuffix)
				lenSuffix = j;
		}
	}
	if(lenPrefix+lenSuffix > shortestWord) /* can happen, e.g. if {"aaa","aaa"} */
		lenSuffix = shortestWord - lenPrefix;
	/* to avoid false positives, we check for some common
	 * field="xxx" syntaxes here.
	 */
	for(int j = lenPrefix-1 ; j >= 0 ; --j) {
		switch(baseword[j]) {
		case '"':
			if(findMatchingTerm(baseword, lenBaseword, j+1,
						&lenPrefix, &lenSuffix,'"'))
				goto done_prefixes;
			break;
		case '\'':
			if(findMatchingTerm(baseword, lenBaseword, j+1,
						&lenPrefix, &lenSuffix,'\''))
				goto done_prefixes;
			break;
		case '[':
			if(findMatchingTerm(baseword, lenBaseword, j+1,
						&lenPrefix, &lenSuffix,']'))
				goto done_prefixes;
			break;
		case '(':
			if(findMatchingTerm(baseword, lenBaseword, j+1,
						&lenPrefix, &lenSuffix,')'))
				goto done_prefixes;
			break;
		case '<':
			if(findMatchingTerm(baseword, lenBaseword, j+1,
						&lenPrefix, &lenSuffix,'>'))
				goto done_prefixes;
			break;
		case '=':
		case ':':
			lenPrefix = j+1;
			break;
		default:
			break;
		}
	}
done_prefixes:
	if(lenPrefix != 0 || lenSuffix != 0) {
		/* TODO: not only print here, but let user override
		 * (in upcoming "interactive" mode)
		 */
		printPrefixes(node, lenPrefix, lenSuffix);
		disjoinCommon(node, lenPrefix, lenSuffix);
	}
}

/* if all terminals, squash siblings. It is too dangerous to
 * do this while creating the tree, but after it has been created
 * such siblings are really just different values.
 */
void
squashTerminalSiblings(logrec_node_t *const __restrict__ node)
{
	if(!node->sibling)
		return;
	int nSiblings = 0;
	for(logrec_node_t *n = node ; n ; n = n->sibling) {
		if(n->child || n->nwords > 1)
			return;
		nSiblings++;
	}
	
	node->words = realloc(node->words, sizeof(struct wordinfo *)
				* (node->nwords + nSiblings));
	for(logrec_node_t *n = node->sibling ; n ; n = n->sibling) {
if(optPrintDebugOutput) printf("add to idx %d: '%s'\n", node->nwords, n->words[0]->word);fflush(stdout);
		node->words[node->nwords++] = n->words[0];
		n->words[0] = NULL;
	}
	node->sibling = NULL; // TODO: fix memory leak
}

/* reprocess tree to check subword creation */
void
treeDetectSubwords(logrec_node_t *node)
{
	if(node == NULL) return;
	reportProgress("subword detection");
	squashTerminalSiblings(node);
	while(node != NULL) {
		checkSubwords(node);
		//checkPrefixes(node);
		treeDetectSubwords(node->child);
		node = node->sibling;
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
	squashTerminalSiblings(node);
	const int hasSibling = node->sibling == NULL ? 0 : 1;
	while(node != NULL) {
		if(!hasSibling && node->child != NULL && node->nwords == 1
		   && node->child->sibling == NULL && node->child->nwords == 1
		   && node->words[0]->word[0] != '%' /* do not combine syntaxes */
		   && node->child->words[0]->word[0] != '%') {
			char *newword;
			if(asprintf(&newword, "%s %s", node->words[0]->word,
				node->child->words[0]->word)) {}; /* silence cc warning */
			if(optPrintDebugOutput)
				printf("squashing: %s\n", newword);
			free(node->words[0]->word);
			node->words[0]->word = newword;
			node->nterm = node->child->nterm; /* TODO: do not combine terminals! */
			logrec_node_t *toDel = node->child;
			node->child = node->child->child;
			logrec_delNode(toDel);
			continue; /* see if we can squash more */
		}
		//checkPrefixes(node);
		treeSquash(node->child);
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
	if(wi->flags.isSubword)
		printf(" {subword}");
	if(wi->occurs > 1)
		printf(" {%d}", wi->occurs);
}

void
treePrint(logrec_node_t *node, const int level)
{
	if(!optPrintTree)
		return;
	reportProgress("print");
	while(node != NULL) {
		if(optSortMultivalues)
			qsort(node->words, node->nwords, sizeof(struct wordinfo*), qs_compmi);
		treePrintIndent(level, 'l');
		treePrintWordinfo(node->words[0]);
		if(node->nterm)
			printf(" [nterm %d]", node->nterm);
		printf("\n");
		for(int i = 1 ; i < node->nwords ; ++i) {
			treePrintIndent(level, 'v');
			treePrintWordinfo(node->words[i]);
			printf("\n");
		}
		treePrint(node->child, level + 1);
		node = node->sibling;
	}
}

#if 0
void
treeToJSON(logrec_node_t *node, json_object *json)
{
	json_object *newobj;
	int isArray;
	reportProgress("convert tree to json");
	if(node == NULL)
		return;
	if(node->sibling == NULL) {
		isArray = 0;
		newobj = json_object_new_object();
	} else {
		isArray = 1;
		newobj = json_object_new_array();
	}
	while(node != NULL) {
		treePrintWordinfo(node->words[0]);
		if(node->nterm)
			printf(" [nterm %d]", node->nterm);
		printf("\n");
		for(int i = 1 ; i < node->nwords ; ++i) {
			treePrintIndent(level, 'v');
			treePrintWordinfo(node->words[i]);
			printf("\n");
		}
		treePrint(node->child, level + 1);
		node = node->sibling;
	}
}
#endif

#if 0
void
treePrintTerminalsNonRoot(logrec_node_t *__restrict__ node,
	const char *const __restrict__ beginOfMsg)
{
	const char *msg = NULL;
	while(node != NULL) {
		const char *tail;
		if(node == root) {
			msg = "";
		} else {
			if(node->nwords > 1) {
				tail = "%word%";
			} else {
				tail = node->words[0]->word;
			}
			free((void*)msg);
			asprintf((char**)&msg, "%s%s%s", beginOfMsg,
				tail,
				(node->words[0]->flags.isSubword) ? "" : " ");
			if(node->nterm)
				printf("%6d times:%s\n", node->nterm, msg);
		}
		treePrintTerminalsNonRoot(node->child, msg);
		node = node->sibling;
	}

}

void
treePrintTerminals(logrec_node_t *__restrict__ node)
{
	/* we need to strip "[ROOT]" from the node value. Note that it may
	 * have been combined with some other value during tree squash.
	 */
	const char *beginOfMsg = node->words[0]->word + 6 /* "[ROOT]"! */;
	while(node != NULL) {
		treePrintTerminalsNonRoot(node->child, "");
		node = node->sibling;
	}

}
#endif

void
treeCreateRuleTableNonRoot(logrec_node_t *__restrict__ node,
	rule_table_t *const __restrict__ rt,
	const char *const __restrict__ beginOfMsg)
{
	const char *msg = NULL;
	while(node != NULL) {
		const char *tail;
		if(node->nwords > 1) {
			tail = "%MULTIVALUE%";
		} else {
			tail = node->words[0]->word;
		}
		free((void*)msg);
		if(asprintf((char**)&msg, "%s%s%s", beginOfMsg,
			tail,
			(node->words[0]->flags.isSubword) ? "" : " ") == -1)
			{}; /* silence cc warning */
		if(node->nterm) {
			reportProgress("rule table create");
			rule_table_etry_t *const rt_etry = ruleTableEtryCreate(rt);
			rt_etry->ntimes = node->nterm;
			rt_etry->rule = strdup(msg);
		}
		treeCreateRuleTableNonRoot(node->child, rt, msg);
		node = node->sibling;
	}

}

rule_table_t *
treeCreateRuleTable(logrec_node_t *__restrict__ node)
{
	/* we need to strip "[ROOT]" from the node value. Note that it may
	 * have been combined with some other value during tree squash.
	 */
	const char *beginOfMsg = node->words[0]->word + 6 /* "[ROOT]"! */;
	rule_table_t *const __restrict__ rt = ruleTableCreate();
	while(node != NULL) {
		treeCreateRuleTableNonRoot(node->child, rt, beginOfMsg);
		node = node->sibling;
	}
	return rt;
}
/* TODO: move wordlen to struct wordinfo? */
void
/* NOTE: bDetectStacked is just a development aid, it permits us to write
 * a first version which does not detect multi-node items that would
 * go to the stack and require more elaborate handling. TODO: remove that
 * restriction.
 * TODO: check: we may remove stacked mode due to new subword algo (if it stays!)
 */
wordDetectSyntax(struct wordinfo *const __restrict__ wi, const size_t wordlen,
	const int bDetectStacked)
{
	size_t nproc;
	size_t constzero = 0; /* the default lognorm parsers need this */
	if(syntax_posint(wi->word, wordlen, NULL, &nproc) &&
	   nproc == wordlen) {
		free(wi->word);
		wi->word = strdup("%posint%");
		wi->flags.isSpecial = 1;
		goto done;
	}
	if(ln_parseTime24hr(wi->word, wordlen, &constzero, NULL, &nproc, NULL) == 0 &&
	   nproc == wordlen) {
		free(wi->word);
		wi->word = strdup("%time-24hr%");
		wi->flags.isSpecial = 1;
		goto done;
	}
	/* duration needs to go after Time24hr, as duration would accept
	 * Time24hr format, whereas duration usually starts with a single
	 * digit and so Tim24hr will not pick it. Still we may get false
	 * detection for durations > 10hrs, but so is it...
	 */
	if(ln_parseDuration(wi->word, wordlen, &constzero, NULL, &nproc, NULL) == 0 &&
	   nproc == wordlen) {
		free(wi->word);
		wi->word = strdup("%duration%");
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
		if(bDetectStacked && wi->word[nproc] == '/') {
			size_t strtnxt = nproc + 1;
			if(syntax_posint(wi->word+strtnxt, wordlen-strtnxt,
				NULL, &nproc))
				if(strtnxt+nproc == wordlen) {
					free(wi->word);
					wi->word = strdup("%ipv4%");
					wi->flags.isSubword = 1;
					wi->flags.isSpecial = 1;

					struct wordinfo *wit;

					wit = wordinfoNew("%posint%");
					wit->flags.isSubword = 1;
					wit->flags.isSpecial = 1;
					wordstackPush(wit);

					wit = wordinfoNew("/");
					wit->flags.isSubword = 1;
					wordstackPush(wit);
					goto done;
				}
		}
	}
	if(ln_parseKernelTimestamp(wi->word, wordlen, &constzero, NULL, &nproc, NULL) == 0 &&
	   nproc == wordlen) {
		free(wi->word);
		wi->word = strdup("%kernel-timestamp%");
		wi->flags.isSpecial = 1;
		goto done;
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
	for( ; ln[i] && !isspace(ln[i]) ; ++i) {
#if 0 /* turn on for subword detection experiment */
		if(ln[i] == ':' || ln[i] == '=' || ln[i] == '/'
		   || ln[i] == '[' || ln[i] == ']'
		   || ln[i] == '(' || ln[i] == ')') {
			wi = wordinfoNew(NULL);
			wi->word = malloc(2);
			wi->word[0] = ln[i];
			wi->word[1] = '\0';
			wordstackPush(wi);
			// TODO: if we continue with this approach, we must indicate that
			// this is a subword.
			/* mimic word delimiter, will be skipped over in next run: */
			ln[i] = ' ';
			break;
		}
#endif
	}
	if(begin_word == i) /* only trailing spaces? */
		return NULL;
	const size_t wordlen = i - begin_word;
	wi = wordinfoNew(NULL);
	wi->word = malloc(wordlen + 1);
	memcpy(wi->word, ln+begin_word, wordlen);
	wi->word[wordlen] = '\0';
	if(wi->word[0] == '%') /* assume already token [TODO: improve] */
		goto done;
	wordDetectSyntax(wi, wordlen, 1);
done:
	*line = ln+i;
	return wi;
}

logrec_node_t *
treeAddToLevel(logrec_node_t *const level, /* current node */
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
			   && !strcmp(child->child->words[0]->word, nextwi->word))
			   	break;
		}
		if(child != NULL) {
			logrec_addWord(child, wi);
			existing = child;
		}
	}

	if(existing == NULL) {
		existing = logrec_newNode(wi, level);
		if(prev == NULL) {
			/* first child of parent node */
			level->child = existing;
		} else {
			/* potential new sibling */
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
		} else if(ln_parseRFC5424Date(buf, buflen, &i, NULL, &nproc, NULL) == 0) {
			tocopy = "%date-rfc5424%";
		} else if(ln_parseISODate(buf, buflen, &i, NULL, &nproc, NULL) == 0) {
			tocopy = "%date-iso%";
		} else if(ln_parsev2IPTables(buf, buflen, &i, NULL, &nproc, NULL) == 0) {
			tocopy = "%v2-iptables%";
		} else if(ln_parseNameValue(buf, buflen, &i, NULL, &nproc, NULL) == 0) {
			tocopy = "%name-value-list%";
		} else if(ln_parseCiscoInterfaceSpec(buf, buflen, &i, NULL, &nproc, NULL) == 0) {
			tocopy = "%cisco-interface-spec%";
		} else if(ln_parseCEESyslog(buf, buflen, &i, NULL, &nproc, NULL) == 0) {
			tocopy = "%cee-syslog%";
		} else if(ln_parseJSON(buf, buflen, &i, NULL, &nproc, NULL) == 0) {
			tocopy = "%json%";
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
	treeDetectSubwords(root);
	treeSquash(root);
	treePrint(root, 0);
	rule_table_t *const rt = treeCreateRuleTable(root);
	reportProgress("sorting rule table");
	qsort(rt->entries, (size_t) rt->nxtEtry, sizeof(rule_table_etry_t*), qs_comp_rt_etry);
	ruleTablePrint(rt);
	ruleTableDestroy(rt);
	//treePrintTerminals(root);
	reportProgress(NULL);
	return 0;
}


#define OPT_PRINT_TREE 1000
#define OPT_PRINT_DEBUG_OUTPUT 1001
#define OPT_SORT_MULTIVALUES 1002
int
main(int argc, char *argv[])
{
	int r;
	int ch;
	static const struct option longopts[] = {
		{ "report-progress",	no_argument,	  0, 'p' },
		{ "print-tree", 	no_argument,	  0, OPT_PRINT_TREE },
		{ "print-debug-output",	no_argument,	  0, OPT_PRINT_DEBUG_OUTPUT },
		{ "sort-multivalues",	required_argument,0, OPT_SORT_MULTIVALUES },
		{ NULL,		0, 0, 0 }
	};

	setvbuf(stdout, NULL, _IONBF, 0);

	while ((ch = getopt_long(argc, argv, "p", longopts, NULL)) != -1) {
		switch (ch) {
		case 'p':		/* file to log */
			displayProgress = 1;
			break;
		case OPT_PRINT_TREE:
			optPrintTree = 1;
			break;
		case OPT_PRINT_DEBUG_OUTPUT:
			optPrintDebugOutput = 1;
			break;
		case OPT_SORT_MULTIVALUES:
			if(!strcmp(optarg, "enabled"))
				optSortMultivalues = 1;
			else if(!strcmp(optarg, "disabled"))
				optSortMultivalues = 0;
			else {
				fprintf(stderr, "invalid value '%s' for --sort-multivalues."
					"Valid: \"enabled\", \"disabled\"\n", optarg);
				exit(1);
			}
			break;
		case '?':
		default:
		//	usage(stderr);
			fprintf(stderr, "invalid option");
			break;
		}
	}

	root = logrec_newNode(wordinfoNew(strdup("[ROOT]")), NULL);
	r = processFile(stdin);
	return r;
}
