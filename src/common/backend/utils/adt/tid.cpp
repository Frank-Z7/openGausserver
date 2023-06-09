/* -------------------------------------------------------------------------
 *
 * tid.c
 *	  Functions for the built-in type tuple id
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/tid.c
 *
 * NOTES
 *	  input routine largely stolen from boxin().
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"
#include "knl/knl_variable.h"

#include <math.h>
#include <limits.h>

#include "access/heapam.h"
#include "access/sysattr.h"
#include "catalog/namespace.h"
#include "catalog/pg_type.h"
#include "libpq/pqformat.h"
#include "miscadmin.h"
#include "parser/parsetree.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/rel.h"
#include "utils/rel_gs.h"
#include "utils/snapmgr.h"
#include "access/tableam.h"

#define DatumGetItemPointer(X) ((ItemPointer)DatumGetPointer(X))
#define ItemPointerGetDatum(X) PointerGetDatum(X)
#define PG_GETARG_ITEMPOINTER(n) DatumGetItemPointer(PG_GETARG_DATUM(n))
#define PG_RETURN_ITEMPOINTER(x) return ItemPointerGetDatum(x)

#define LDELIM '('
#define RDELIM ')'
#define DELIM ','
#define NTIDARGS 2

/* ----------------------------------------------------------------
 *		tidin
 * ----------------------------------------------------------------
 */
Datum tidin(PG_FUNCTION_ARGS)
{
    char* str = PG_GETARG_CSTRING(0);
    char* p = NULL;
    char* coord[NTIDARGS];
    int i;
    ItemPointer result;
    BlockNumber blockNumber;
    OffsetNumber offsetNumber;
    char* badp = NULL;
    int hold_offset;

    for (i = 0, p = str; *p && i < NTIDARGS && *p != RDELIM; p++)
        if (*p == DELIM || (*p == LDELIM && !i))
            coord[i++] = p + 1;

    if (i < NTIDARGS)
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for type tid: \"%s\"", str)));

    errno = 0;
    blockNumber = strtoul(coord[0], &badp, 10);
    if (errno || *badp != DELIM)
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for type tid: \"%s\"", str)));

    hold_offset = strtol(coord[1], &badp, 10);
    if (errno || *badp != RDELIM || hold_offset > USHRT_MAX || hold_offset < 0)
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for type tid: \"%s\"", str)));

    offsetNumber = hold_offset;

    result = (ItemPointer)palloc(sizeof(ItemPointerData));

    ItemPointerSet(result, blockNumber, offsetNumber);

    PG_RETURN_ITEMPOINTER(result);
}

/* ----------------------------------------------------------------
 *		tidout
 * ----------------------------------------------------------------
 */
Datum tidout(PG_FUNCTION_ARGS)
{
    ItemPointer itemPtr = PG_GETARG_ITEMPOINTER(0);
    BlockNumber blockNumber;
    OffsetNumber offsetNumber;
    char buf[32] = {0};

    blockNumber = BlockIdGetBlockNumber(&(itemPtr->ip_blkid));
    offsetNumber = itemPtr->ip_posid;

    /* Perhaps someday we should output this as a record. */
    errno_t errorno = snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "(%u,%u)", blockNumber, offsetNumber);
    securec_check_ss(errorno, "\0", "\0");
    PG_RETURN_CSTRING(pstrdup(buf));
}

/*
 *		tidrecv			- converts external binary format to tid
 */
Datum tidrecv(PG_FUNCTION_ARGS)
{
    StringInfo buf = (StringInfo)PG_GETARG_POINTER(0);
    ItemPointer result;
    BlockNumber blockNumber;
    OffsetNumber offsetNumber;

    blockNumber = pq_getmsgint(buf, sizeof(blockNumber));
    offsetNumber = pq_getmsgint(buf, sizeof(offsetNumber));

    result = (ItemPointer)palloc(sizeof(ItemPointerData));

    ItemPointerSet(result, blockNumber, offsetNumber);

    PG_RETURN_ITEMPOINTER(result);
}

/*
 *		tidsend			- converts tid to binary format
 */
Datum tidsend(PG_FUNCTION_ARGS)
{
    ItemPointer itemPtr = PG_GETARG_ITEMPOINTER(0);
    BlockId blockId;
    BlockNumber blockNumber;
    OffsetNumber offsetNumber;
    StringInfoData buf;

    blockId = &(itemPtr->ip_blkid);
    blockNumber = BlockIdGetBlockNumber(blockId);
    offsetNumber = itemPtr->ip_posid;

    pq_begintypsend(&buf);
    pq_sendint32(&buf, blockNumber);
    pq_sendint16(&buf, offsetNumber);
    PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/*****************************************************************************
 *	 PUBLIC ROUTINES														 *
 *****************************************************************************/

Datum tideq(PG_FUNCTION_ARGS)
{
    ItemPointer arg1 = PG_GETARG_ITEMPOINTER(0);
    ItemPointer arg2 = PG_GETARG_ITEMPOINTER(1);

    PG_RETURN_BOOL(ItemPointerCompare(arg1, arg2) == 0);
}

Datum tidne(PG_FUNCTION_ARGS)
{
    ItemPointer arg1 = PG_GETARG_ITEMPOINTER(0);
    ItemPointer arg2 = PG_GETARG_ITEMPOINTER(1);

    PG_RETURN_BOOL(ItemPointerCompare(arg1, arg2) != 0);
}

Datum tidlt(PG_FUNCTION_ARGS)
{
    ItemPointer arg1 = PG_GETARG_ITEMPOINTER(0);
    ItemPointer arg2 = PG_GETARG_ITEMPOINTER(1);

    PG_RETURN_BOOL(ItemPointerCompare(arg1, arg2) < 0);
}

Datum tidle(PG_FUNCTION_ARGS)
{
    ItemPointer arg1 = PG_GETARG_ITEMPOINTER(0);
    ItemPointer arg2 = PG_GETARG_ITEMPOINTER(1);

    PG_RETURN_BOOL(ItemPointerCompare(arg1, arg2) <= 0);
}

Datum tidgt(PG_FUNCTION_ARGS)
{
    ItemPointer arg1 = PG_GETARG_ITEMPOINTER(0);
    ItemPointer arg2 = PG_GETARG_ITEMPOINTER(1);

    PG_RETURN_BOOL(ItemPointerCompare(arg1, arg2) > 0);
}

Datum tidge(PG_FUNCTION_ARGS)
{
    ItemPointer arg1 = PG_GETARG_ITEMPOINTER(0);
    ItemPointer arg2 = PG_GETARG_ITEMPOINTER(1);

    PG_RETURN_BOOL(ItemPointerCompare(arg1, arg2) >= 0);
}

Datum bttidcmp(PG_FUNCTION_ARGS)
{
    ItemPointer arg1 = PG_GETARG_ITEMPOINTER(0);
    ItemPointer arg2 = PG_GETARG_ITEMPOINTER(1);

    PG_RETURN_INT32(ItemPointerCompare(arg1, arg2));
}

Datum tidlarger(PG_FUNCTION_ARGS)
{
    ItemPointer arg1 = PG_GETARG_ITEMPOINTER(0);
    ItemPointer arg2 = PG_GETARG_ITEMPOINTER(1);

    PG_RETURN_ITEMPOINTER((ItemPointerCompare(arg1, arg2) >= 0) ? arg1 : arg2);
}

Datum tidsmaller(PG_FUNCTION_ARGS)
{
    ItemPointer arg1 = PG_GETARG_ITEMPOINTER(0);
    ItemPointer arg2 = PG_GETARG_ITEMPOINTER(1);

    PG_RETURN_ITEMPOINTER((ItemPointerCompare(arg1, arg2) <= 0) ? arg1 : arg2);
}

/*
 * covert bigint to tid
 */
Datum bigint_tid(PG_FUNCTION_ARGS)
{
    int64 val = PG_GETARG_INT64(0);
    ItemPointer item_ptr = (ItemPointer)&val;

    BlockNumber blockNumber = BlockIdGetBlockNumber(&(item_ptr)->ip_blkid);
    OffsetNumber offsetNumber = (item_ptr)->ip_posid;

    ItemPointer result = (ItemPointer)palloc(sizeof(ItemPointerData));
    ItemPointerSet(result, blockNumber, offsetNumber);
    PG_RETURN_ITEMPOINTER(result);
}

/*
 *  output cstore tid
 */
Datum cstore_tid_out(PG_FUNCTION_ARGS)
{
    int64 val = PG_GETARG_INT64(0);
    ItemPointer item_ptr = (ItemPointer)&val;

    BlockNumber blockNumber = BlockIdGetBlockNumber(&(item_ptr)->ip_blkid);
    OffsetNumber offsetNumber = (item_ptr)->ip_posid;

    char buf[32];  // 9 + 9 + '(' + ', '+ ')' + '\0' = 32
    int rc = snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "(%u,%u)", blockNumber, offsetNumber);
    securec_check_ss(rc, "", "");

    PG_RETURN_CSTRING(pstrdup(buf));
}

/*
 *	Functions to get latest tid of a specified tuple.
 *
 *	Maybe these implementations should be moved to another place
 */
void setLastTid(const ItemPointer tid)
{
    *u_sess->utils_cxt.cur_last_tid = *tid;
}

/*
 *	Handle CTIDs of views.
 *		CTID should be defined in the view and it must
 *		correspond to the CTID of a base relation.
 */
static Datum currtid_for_view(Relation viewrel, ItemPointer tid)
{
    TupleDesc att = RelationGetDescr(viewrel);
    RuleLock* rulelock = NULL;
    RewriteRule* rewrite = NULL;
    int i, natts = att->natts, tididx = -1;

    for (i = 0; i < natts; i++) {
        if (strcmp(NameStr(att->attrs[i].attname), "ctid") == 0) {
            if (att->attrs[i].atttypid != TIDOID)
                ereport(ERROR, (errcode(ERRCODE_DATATYPE_MISMATCH), errmsg("ctid isn't of type TID")));
            tididx = i;
            break;
        }
    }
    if (tididx < 0)
        ereport(ERROR, (errcode(ERRCODE_DATA_EXCEPTION), errmsg("currtid cannot handle views with no CTID")));
    rulelock = viewrel->rd_rules;
    if (rulelock == NULL)
        ereport(ERROR, (errcode(ERRCODE_DATA_EXCEPTION), errmsg("the view has no rules")));
    for (i = 0; i < rulelock->numLocks; i++) {
        rewrite = rulelock->rules[i];
        if (rewrite->event == CMD_SELECT) {
            Query* query = NULL;
            TargetEntry* tle = NULL;

            if (list_length(rewrite->actions) != 1)
                ereport(ERROR,
                    (errcode(ERRCODE_OPTIMIZER_INCONSISTENT_STATE),
                        errmsg("only one select rule is allowed in views")));
            query = (Query*)linitial(rewrite->actions);
            tle = get_tle_by_resno(query->targetList, tididx + 1);
            if ((tle != NULL) && (tle->expr != NULL) && IsA(tle->expr, Var)) {
                Var* var = (Var*)tle->expr;
                RangeTblEntry* rte = NULL;

                if (!IS_SPECIAL_VARNO(var->varno) && var->varattno == SelfItemPointerAttributeNumber) {
                    rte = rt_fetch(var->varno, query->rtable);
                    if (rte != NULL) {
                        heap_close(viewrel, AccessShareLock);
                        return DirectFunctionCall2(
                            currtid_byreloid, ObjectIdGetDatum(rte->relid), PointerGetDatum(tid));
                    }
                }
            }
            break;
        }
    }

    ereport(ERROR, (errcode(ERRCODE_OPTIMIZER_INCONSISTENT_STATE), errmsg("currtid cannot handle this view")));
    return (Datum)0;
}

Datum currtid_byreloid(PG_FUNCTION_ARGS)
{
    Oid reloid = PG_GETARG_OID(0);
    ItemPointer tid = PG_GETARG_ITEMPOINTER(1);
    ItemPointer result;
    Relation rel;
    AclResult aclresult;

    result = (ItemPointer)palloc(sizeof(ItemPointerData));
    if (!reloid) {
        *result = *u_sess->utils_cxt.cur_last_tid;
        PG_RETURN_ITEMPOINTER(result);
    }

    rel = heap_open(reloid, AccessShareLock);

    aclresult = pg_class_aclcheck(RelationGetRelid(rel), GetUserId(), ACL_SELECT);
    if (aclresult != ACLCHECK_OK)
        aclcheck_error(aclresult, ACL_KIND_CLASS, RelationGetRelationName(rel));

    if (rel->rd_rel->relkind == RELKIND_VIEW || rel->rd_rel->relkind == RELKIND_CONTQUERY)
        return currtid_for_view(rel, tid);

    ItemPointerCopy(tid, result);
    tableam_tuple_get_latest_tid(rel, SnapshotNow, result);

    heap_close(rel, AccessShareLock);

    PG_RETURN_ITEMPOINTER(result);
}

Datum currtid_byrelname(PG_FUNCTION_ARGS)
{
    text* relname = PG_GETARG_TEXT_P(0);
    ItemPointer tid = PG_GETARG_ITEMPOINTER(1);
    ItemPointer result;
    RangeVar* relrv = NULL;
    Relation rel;
    AclResult aclresult;
    List* names = NIL;

    names = textToQualifiedNameList(relname);
    relrv = makeRangeVarFromNameList(names);
    rel = heap_openrv(relrv, AccessShareLock);

    aclresult = pg_class_aclcheck(RelationGetRelid(rel), GetUserId(), ACL_SELECT);
    if (aclresult != ACLCHECK_OK)
        aclcheck_error(aclresult, ACL_KIND_CLASS, RelationGetRelationName(rel));

    if (rel->rd_rel->relkind == RELKIND_VIEW || rel->rd_rel->relkind == RELKIND_CONTQUERY)
        return currtid_for_view(rel, tid);

    result = (ItemPointer)palloc(sizeof(ItemPointerData));
    ItemPointerCopy(tid, result);

    tableam_tuple_get_latest_tid(rel, SnapshotNow, result);

    list_free_ext(names);

    heap_close(rel, AccessShareLock);

    PG_RETURN_ITEMPOINTER(result);
}
