/**
 * @file pdag.h
 * @brief The parse DAG object.
 * @class ln_pdag pdag.h
 *//*
 * Copyright 2015 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_PDAG_H_INCLUDED
#define	LIBLOGNORM_PDAG_H_INCLUDED
#include <libestr.h>
#include <stdint.h>
#include <ptree.h> // TODO: remove

#define ORIGINAL_MSG_KEY "originalmsg"
#define UNPARSED_DATA_KEY "unparsed-data"

typedef struct ln_pdag ln_pdag; /**< the parse DAG object */
typedef struct ln_parser_s ln_parser_t;
typedef uint8_t prsid_t;

/** 
 * parser IDs.
 *
 * These identfy a parser. VERY IMPORTANT: they must start at zero
 * and continously increment. They must exactly match the index
 * of the respective parser inside the parser lookup table.
 */
#define PRS_LITERAL			0
#define PRS_DATE_RFC3164		1
#define PRS_DATE_RFC5424		2
#define PRS_NUMBER			3
#define PRS_FLOAT			4
#define PRS_HEXNUMBER			5
#define PRS_KERNEL_TIMESTAMP		6
#define PRS_WHITESPACE			7
#define PRS_IPV4			8
#define PRS_IPV6			9
#define PRS_WORD			10
#define PRS_ALPHA			11
#define PRS_REST			12
#define PRS_OP_QUOTED_STRING		13
#define PRS_QUOTED_STRING		14
#define PRS_DATE_ISO			15
#define PRS_TIME_24HR			16
#define PRS_TIME_12HR			17
#define PRS_DURATION			18
#define PRS_CISCO_INTERFACE_SPEC	19
#define PRS_NAME_VALUE_LIST		20
#define PRS_JSON			21
#define PRS_CEE_SYSLOG			22
#define PRS_MAC48			23
#define PRS_CEF				24
#define PRS_CHECKPOINT_LEA		25
#define PRS_v2_IPTABLES			26
#define PRS_STRING_TO			27
#define PRS_CHAR_TO			28
#define PRS_CHAR_SEP			29

#define PRS_INVALID			255
/* NOTE: current max limit on parser ID is 255, because we use uint8_t
 * for the prsid_t type (which gains cache performance). If more parsers
 * come up, the type must be modified.
 */
/**
 * object describing a specific parser instance.
 */
struct ln_parser_s {
	prsid_t prsid;		/**< parser ID (for lookup table) */
	ln_pdag *node;		/**< node to branch to if parser succeeded */
	const char *name;	/**< field name */
	es_str_t *data;		/**< extra data to be passed to parser */
	es_str_t *raw_data;	/**< extra untouched (unescaping is not done) data available to be used by parser */ /* this is used for some legacy complex parsers like tokenize */
	// TODO: think about moving legacy items out of the core data structure! (during optimizer?)
	uint8_t prio;		/**< assigned priority */
	void *parser_data;	/** opaque data that the field-parser understands */
};

struct ln_parser_info {
	const char *name;	/**< parser name as used in rule base */
	int (*parser)(const char*, size_t, size_t*, const ln_parser_t *,
				  size_t*, struct json_object **); /**< parser to use */
	void (*parser_data_destructor)(void **); /** destroy opaque data that field-parser understands */
};


/* parse DAG object
 */
struct ln_pdag {
	ln_ctx ctx;			/**< our context */ // TODO: why do we need it?
	ln_parser_t *parsers;		/* array of parsers to try */
	prsid_t nparsers;		/**< current table size (prsid_t slighly abused) */
	struct {
		unsigned isTerminal:1;	/**< designates this node a terminal sequence */
	} flags;
	struct json_object *tags;	/**< t	CHKmalloc(pNewBuf = (uchar*) realloc(pThis->pBuf, iNewSize * sizeof(uchar)));
ags to assign to events of this type */

	// TODO: remove these once we do no longer need ptree.[ch] for test builds
	ln_pdag	**parentptr; /**< pointer to *us* *inside* the parent 
				BUT this is NOT a pointer to the parent! */
};


/* Methods */

/**
 * Allocates and initializes a new parse DAG node.
 * @memberof ln_pdag
 *
 * @param[in] ctx current library context. This MUST match the
 * 		context of the parent.
 * @param[in] parent pointer to the new node inside the parent
 *
 * @return pointer to new node or NULL on error
 */
struct ln_pdag* ln_newPDAG(ln_ctx ctx);


/**
 * Free a parse DAG and destruct all members.
 * @memberof ln_pdag
 *
 * @param[in] DAG pointer to pdag to free
 */
void ln_deletePDAG(struct ln_pdag *DAG);

/**
 * Free a parse DAG node and destruct all members.
 * @memberof ln_pdag
 *
 * @param[in] node pointer to free
 */
void ln_deletePDAGNode(ln_parser_t *node);

/**
 * Add parser to dag node.
 * Works on unoptimzed dag.
 *
 * @param[in] pdag pointer to pdag to modify
 * @param[in] parser parser definition
 * @returns 0 on success, something else otherwise
 */
int ln_pdagAddParser(struct ln_pdag **pdag, ln_parser_t *parser);


/**
 * Display the content of a pdag (debug function).
 * This is a debug aid that spits out a textual representation
 * of the provided pdag via multiple calls of the debug callback.
 *
 * @param DAG pdag to display
 * @param level recursion level, must be set to 0 on initial call
 */
void ln_displayPDAG(struct ln_pdag *DAG, int level);


/**
 * Generate a DOT graph.
 * Well, actually it does not generate the graph itself, but a
 * control file that is suitable for the GNU DOT tool. Such a file
 * can be very useful to understand complex sample databases
 * (not to mention that it is probably fun for those creating
 * samples).
 * The dot commands are appended to the provided string.
 *
 * @param[in] DAG pdag to display
 * @param[out] str string which receives the DOT commands.
 */
void ln_genDotPDAGGraph(struct ln_pdag *DAG, es_str_t **str);


/**
 * Build a pdag based on the provided string, but only if necessary.
 * The passed-in DAG is searched and traversed for str. If a node exactly
 * matching str is found, that node is returned. If no exact match is found,
 * a new node is added. Existing nodes may be split, if a so-far common
 * prefix needs to be split in order to add the new node.
 *
 * @param[in] DAG root of the current DAG
 * @param[in] str string to be added
 * @param[in] offs offset into str where match needs to start
 *             (this is required for recursive calls to handle
 *             common prefixes)
 * @return NULL on error, otherwise the pdag leaf that
 *         corresponds to the parameters passed.
 */
struct ln_pdag * ln_buildPDAG(struct ln_pdag *DAG, es_str_t *str, size_t offs);


prsid_t ln_parserName2ID(const char *const __restrict__ name);
#endif /* #ifndef LOGNORM_PDAG_H_INCLUDED */
