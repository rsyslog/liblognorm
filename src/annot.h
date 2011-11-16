/**
 * @file annot.h
 * @brief The annotation set object
 * @class ln_annot annot.h
 *//*
 * Copyright 2011 by Rainer Gerhards and Adiscon GmbH.
 *
 * This file is meant to be included by applications using liblognorm.
 * For lognorm library files themselves, include "lognorm.h".
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
#ifndef LIBLOGNORM_ANNOT_H_INCLUDED
#define	LIBLOGNORM_ANNOT_H_INCLUDED
#include <libestr.h>
#include <libee/libee.h>

typedef struct ln_annot_s ln_annot; /**< the parse tree object */
typedef struct ln_annot_op_s ln_annot_op; /**< the parse tree object */
typedef enum {ln_annot_ADD=0, ln_annot_RM=1} ln_annot_opcode;

/**
 * List of annotation operations.
 */
struct ln_annot_op_s {
	ln_annot_opcode opc; /**< opcode */
	es_str_t *name;
	es_str_t *value;
	ln_annot_op *next;
};

/* annotation set object
 */
struct ln_annot_s {
	es_str_t *tag;	/**< tag associated for this annotation */
	ln_annot_op *oproot;
};

/* Methods */

/**
 * Allocates and initializes a new annotation set.
 * @memberof ln_annot
 *
 * @param[in] ctx current library context. This MUST match the
 * 		context of the parent.
 *
 * @return pointer to new node or NULL on error
 */
ln_annot* ln_newAnnot(ln_ctx ctx);


/**
 * Free annotation set and destruct all members.
 * @memberof ln_annot
 *
 * @param[in] tree pointer to annot to free
 */
void ln_deleteAnnot(ln_annot *annot);


/**
 * Add an operation to the annotation set.
 * The operation description will be added as entry.
 * @memberof ln_annot
 *
 * @param[in] annot pointer to annot to modify
 * @param[in] op operation
 * @param[in] name name of field, must NOT be re-used by caller
 * @param[in] value value of field, may be NULL (e.g. in remove operation),
 * 		    must NOT be re-used by caller
 * @returns 0 on success, something else otherwise
 */
int ln_addAnnotOp(ln_annot *anot, ln_annot_opcode opc, es_str_t *name, es_str_t *value);

#endif /* #ifndef LOGNORM_ANNOT_H_INCLUDED */
