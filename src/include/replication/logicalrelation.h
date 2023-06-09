/*-------------------------------------------------------------------------
 *
 * logicalrelation.h
 *	  Relation definitions for logical replication relation mapping.
 *
 * Portions Copyright (c) 2010-2016, PostgreSQL Global Development Group
 *
 * src/include/replication/logicalrelation.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef LOGICALRELATION_H
#define LOGICALRELATION_H

#include "replication/logicalproto.h"

typedef struct LogicalRepRelMapEntry
{
    LogicalRepRelation remoterel; /* key is remoterel.remoteid */

    /*
     * Validity flag -- when false, revalidate all derived info at next
     * logicalrep_rel_open.  (While the localrel is open, we assume our lock
     * on that rel ensures the info remains good.)
     */
    bool localrelvalid;

    /* Mapping to local relation, filled as needed. */
    Oid localreloid; /* local relation id */
    Relation localrel; /* relcache entry (NULL when closed) */
    AttrNumber *attrmap; /* map of local attributes to remote ones */
    bool updatable; /* Can apply updates/detetes? */

    /* Sync state. */
    char state;
    XLogRecPtr statelsn;
} LogicalRepRelMapEntry;

extern void logicalrep_relmap_update(LogicalRepRelation *remoterel);
extern LogicalRepRelMapEntry *logicalrep_rel_open(LogicalRepRelId remoteid, LOCKMODE lockmode);
extern void logicalrep_rel_close(LogicalRepRelMapEntry *rel, LOCKMODE lockmode);

#endif   /* LOGICALRELATION_H */

