/* -------------------------------------------------------------------------
 *
 * nodeBitmapHeapscan.h
 *
 *
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeBitmapHeapscan.h
 *
 * -------------------------------------------------------------------------
 */
#ifndef NODEBITMAPHEAPSCAN_H
#define NODEBITMAPHEAPSCAN_H

#include "nodes/execnodes.h"

extern BitmapHeapScanState* ExecInitBitmapHeapScan(BitmapHeapScan* node, EState* estate, int eflags);
extern void ExecEndBitmapHeapScan(BitmapHeapScanState* node);
extern void ExecReScanBitmapHeapScan(BitmapHeapScanState* node);

#endif /* NODEBITMAPHEAPSCAN_H */
