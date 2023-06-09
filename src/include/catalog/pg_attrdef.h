/* -------------------------------------------------------------------------
 *
 * pg_attrdef.h
 *	  definition of the system "attribute defaults" relation (pg_attrdef)
 *	  along with the relation's initial contents.
 *
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/catalog/pg_attrdef.h
 *
 * NOTES
 *	  the genbki.pl script reads this file and generates .bki
 *	  information from the DATA() statements.
 *
 * -------------------------------------------------------------------------
 */
#ifndef PG_ATTRDEF_H
#define PG_ATTRDEF_H

#include "catalog/genbki.h"

/* ----------------
 *		pg_attrdef definition.	cpp turns this into
 *		typedef struct FormData_pg_attrdef
 * ----------------
 */
#define AttrDefaultRelationId  2604
#define AttrDefaultRelation_Rowtype_Id 10000

CATALOG(pg_attrdef,2604) BKI_SCHEMA_MACRO
{
	Oid			adrelid;		/* OID of table containing attribute */
	int2		adnum;			/* attnum of attribute */

#ifdef CATALOG_VARLEN			/* variable-length fields start here */
	pg_node_tree adbin;			/* nodeToString representation of default */
	text		adsrc;			/* human-readable representation of default */
#endif
    char adgencol; /* generated column setting */
#ifdef CATALOG_VARLEN			/* variable-length fields start here */
	pg_node_tree adbin_on_update    /* binrary format of on update express syntax */
	text adsrc_on_update;        /* on update express syntax on Mysql Feature */
#endif
} FormData_pg_attrdef;

/* ----------------
 *		Form_pg_attrdef corresponds to a pointer to a tuple with
 *		the format of pg_attrdef relation.
 * ----------------
 */
typedef FormData_pg_attrdef *Form_pg_attrdef;

/* ----------------
 *		compiler constants for pg_attrdef
 * ----------------
 */
#define Natts_pg_attrdef				7
#define Anum_pg_attrdef_adrelid			1
#define Anum_pg_attrdef_adnum			2
#define Anum_pg_attrdef_adbin			3
#define Anum_pg_attrdef_adsrc			4
#define Anum_pg_attrdef_adgencol		5
#define Anum_pg_attrdef_adbin_on_update 6
#define Anum_pg_attrdef_adsrc_on_update	7

#endif   /* PG_ATTRDEF_H */
