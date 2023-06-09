/* -------------------------------------------------------------------------
 *
 * nodeTidscan.h
 *
 *
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeTidscan.h
 *
 * -------------------------------------------------------------------------
 */
#ifndef NODETIDSCAN_H
#define NODETIDSCAN_H

#include "nodes/execnodes.h"

extern TidScanState* ExecInitTidScan(TidScan* node, EState* estate, int eflags);
extern void ExecEndTidScan(TidScanState* node);
extern void ExecTidMarkPos(TidScanState* node);
extern void ExecTidRestrPos(TidScanState* node);
extern void ExecReScanTidScan(TidScanState* node);
extern bool HeapFetchRowVersion(TidScanState* node, Relation relation,
                          ItemPointer tid,
                          Snapshot snapshot,
                          TupleTableSlot *slot);

#endif /* NODETIDSCAN_H */
