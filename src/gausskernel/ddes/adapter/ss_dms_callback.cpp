/*
 * Copyright (c) 2020 Huawei Technologies Co.,Ltd.
 *
 * openGauss is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * ---------------------------------------------------------------------------------------
 *
 * ss_dms_callback.cpp
 *        Provide callback interface for called inside DMS API
 *
 * IDENTIFICATION
 *        src/gausskernel/ddes/adapter/ss_dms_callback.cpp
 *
 * ---------------------------------------------------------------------------------------
 */
#include "ddes/dms/ss_dms_callback.h"
#include "postgres.h"
#include "miscadmin.h"
#include "postmaster/postmaster.h"
#include "utils/memutils.h"
#include "utils/palloc.h"
#include "utils/resowner.h"
#include "utils/postinit.h"
#include "storage/procarray.h"
#include "access/xact.h"
#include "access/transam.h"
#include "access/csnlog.h"
#include "ddes/dms/ss_dms_bufmgr.h"
#include "storage/buf/buf_internals.h"
#include "ddes/dms/ss_transaction.h"
#include "storage/smgr/segment.h"
#include "storage/sinvaladt.h"
#include "replication/walsender_private.h"
#include "replication/walreceiver.h"
#include "ddes/dms/ss_switchover.h"
#include "ddes/dms/ss_reform_common.h"
#include "ddes/dms/ss_dms_bufmgr.h"
#include "storage/file/fio_device.h"
#include "storage/buf/bufmgr.h"

static int CBGetUpdateXid(void *db_handle, unsigned long long xid, unsigned int t_infomask, unsigned int t_infomask2,
    unsigned long long *uxid)
{
    int result = DMS_SUCCESS;
    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;

    PG_TRY();
    {
        *uxid =
            (unsigned long long)MultiXactIdGetUpdateXid((TransactionId)xid, (uint16)t_infomask, (uint16)t_infomask2);
    }
    PG_CATCH();
    {
        result = DMS_ERROR;
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        if (t_thrd.role == DMS_WORKER) {
            FlushErrorState();
        }
    }
    PG_END_TRY();
    return result;
}

static CommitSeqNo TransactionWaitCommittingCSN(dms_opengauss_xid_csn_t *xid_csn_ctx, bool *sync)
{
    bool looped = false;
    bool isCommit = (bool)xid_csn_ctx->is_committed;
    bool isMvcc = (bool)xid_csn_ctx->is_mvcc;
    bool isNest = (bool)xid_csn_ctx->is_nest;
    TransactionId xid = xid_csn_ctx->xid;
    CommitSeqNo snapshotcsn = xid_csn_ctx->snapshotcsn;
    TransactionId parentXid = InvalidTransactionId;
    SnapshotData snapshot = {SNAPSHOT_MVCC};
    snapshot.xmin = xid_csn_ctx->snapshotxmin;
    snapshot.snapshotcsn = snapshotcsn;
    CommitSeqNo csn = TransactionIdGetCommitSeqNo(xid, isCommit, isMvcc, isNest, &snapshot);

    while (COMMITSEQNO_IS_COMMITTING(csn)) {
        if (looped && isCommit) {
            ereport(DEBUG1,
                (errmodule(MOD_DMS), errmsg("committed SS xid %lu's csn %lu"
                    "is changed to FROZEN after lockwait.", xid, csn)));
            CSNLogSetCommitSeqNo(xid, 0, NULL, COMMITSEQNO_FROZEN);
            SetLatestFetchState(xid, COMMITSEQNO_FROZEN);
            /* in this case, SS tuple is visible on standby, as we already compared and waited */
            return COMMITSEQNO_FROZEN;
        } else if (looped && !isCommit) {
            ereport(DEBUG1, (errmodule(MOD_DMS),
                errmsg("SS XID %lu's csn %lu is changed to ABORT after lockwait.", xid, csn)));
            /* recheck if transaction id is finished */
            RecheckXidFinish(xid, csn);
            CSNLogSetCommitSeqNo(xid, 0, NULL, COMMITSEQNO_ABORTED);
            SetLatestFetchState(xid, COMMITSEQNO_ABORTED);
            /* in this case, SS tuple is not visible on standby */
            return COMMITSEQNO_ABORTED;
        } else {
            if (!COMMITSEQNO_IS_SUBTRANS(csn)) {
                /* If snapshotcsn lower than csn stored in csn log, don't need to wait. */
                CommitSeqNo latestCSN = GET_COMMITSEQNO(csn);
                if (latestCSN >= snapshotcsn) {
                    ereport(DEBUG1,
                        (errmodule(MOD_DMS), errmsg(
                            "snapshotcsn %lu < csn %lu stored in CSNLog, TXN invisible, no need to sync wait, XID %lu",
                            snapshotcsn,
                            latestCSN,
                            xid)));
                    /* in this case, SS tuple is not visible; to return ABORT is inappropriate, so let standby judge */
                    return latestCSN;
                }
            } else {
                parentXid = (TransactionId)GET_PARENTXID(csn);
            }

            if (u_sess->attr.attr_common.xc_maintenance_mode || t_thrd.xact_cxt.bInAbortTransaction) {
                return COMMITSEQNO_ABORTED;
            }

            // standby does not need buf lock or validation
            if (TransactionIdIsValid(parentXid)) {
                SyncLocalXidWait(parentXid);
            } else {
                SyncLocalXidWait(xid);
            }

            looped = true;
            *sync = true;
            parentXid = InvalidTransactionId;
            csn = TransactionIdGetCommitSeqNo(xid, isCommit, isMvcc, isNest, &snapshot);
        }
    }
    return csn;
}

static int CBGetTxnCSN(void *db_handle, dms_opengauss_xid_csn_t *csn_req, dms_opengauss_csn_result_t *csn_res)
{
    int ret;
	uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;
    PG_TRY();
    {
        bool sync = false;
        CLogXidStatus clogstatus = CLOG_XID_STATUS_IN_PROGRESS;
        XLogRecPtr lsn = InvalidXLogRecPtr;
        CommitSeqNo csn = TransactionWaitCommittingCSN(csn_req, &sync);
        clogstatus = CLogGetStatus(csn_req->xid, &lsn);
        csn_res->csn = csn;
        csn_res->sync = (unsigned char)sync;
        csn_res->clogstatus = (unsigned int)clogstatus;
        csn_res->lsn = lsn;
        ret = DMS_SUCCESS;
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        if (t_thrd.role == DMS_WORKER) {
            FlushErrorState();
        }
        ret = DMS_ERROR;
    }
    PG_END_TRY();
    return ret;
}

static int CBGetSnapshotData(void *db_handle, dms_opengauss_txn_snapshot_t *txn_snapshot)
{
    if (RecoveryInProgress()) {
        return DMS_ERROR;
    }

    int retCode = DMS_ERROR;
    SnapshotData snapshot = {SNAPSHOT_MVCC};
    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;

    PG_TRY();
    {
        (void)GetSnapshotData(&snapshot, false);
        if (snapshot.xmin != InvalidTransactionId) {
            txn_snapshot->xmin = snapshot.xmin;
            txn_snapshot->xmax = snapshot.xmax;
            txn_snapshot->snapshotcsn = snapshot.snapshotcsn;
            txn_snapshot->localxmin = u_sess->utils_cxt.RecentGlobalXmin;
            retCode = DMS_SUCCESS;
        }
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        if (t_thrd.role == DMS_WORKER) {
            FlushErrorState();
        }
    }
    PG_END_TRY();

    return retCode;
}

static int CBGetTxnStatus(void *db_handle, unsigned long long xid, unsigned char type, unsigned char *result)
{
    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;
    PG_TRY();
    {
        switch (type) {
            case XID_INPROGRESS:
                *result = (unsigned char)TransactionIdIsInProgress(xid);
                break;
            case XID_COMMITTED:
                *result = (unsigned char)TransactionIdDidCommit(xid);
                break;
            default:
                return DMS_ERROR;
        }
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        if (t_thrd.role == DMS_WORKER) {
            FlushErrorState();
        }
    }
    PG_END_TRY();
    return DMS_SUCCESS;
}

static int CBGetCurrModeAndLockBuffer(void *db_handle, int buffer, unsigned char lock_mode,
    unsigned char *curr_mode)
{
    Assert((buffer - 1) >= 0);
    BufferDesc *bufHdr = GetBufferDescriptor(buffer - 1);
    *curr_mode = (unsigned char)GetHeldLWLockMode(bufHdr->content_lock); // LWLockMode
    Assert(*curr_mode == LW_EXCLUSIVE || *curr_mode == LW_SHARED);
    LockBuffer((Buffer)buffer, lock_mode); // BUFFER_LOCK_UNLOCK, BUFFER_LOCK_SHARE or BUFFER_LOCK_EXCLUSIVE
    ereport(LOG, (errmodule(MOD_DMS),
        errmsg("SS lock buf success, buffer=%d, mode=%hhu, curr_mode=%hhu", buffer, lock_mode, *curr_mode)));
    return DMS_SUCCESS;
}

static int CBSwitchoverDemote(void *db_handle)
{
    DemoteMode demote_mode = FastDemote;

    /* borrows walsender lock */
    SpinLockAcquire(&t_thrd.walsender_cxt.WalSndCtl->mutex);
    if (t_thrd.walsender_cxt.WalSndCtl->demotion > NoDemote) {
        SpinLockRelease(&t_thrd.walsender_cxt.WalSndCtl->mutex);
        ereport(LOG, (errmodule(MOD_DMS), errmsg("[SS switchover] master is doing switchover,"
            " probably standby already requested switchover.")));
        return DMS_SUCCESS;
    }
    Assert(g_instance.dms_cxt.SSClusterState == NODESTATE_NORMAL);
    Assert(SS_OFFICIAL_PRIMARY);

    t_thrd.walsender_cxt.WalSndCtl->demotion = demote_mode;
    g_instance.dms_cxt.SSClusterState = NODESTATE_PRIMARY_DEMOTING;
    g_instance.dms_cxt.SSRecoveryInfo.new_primary_reset_walbuf_flag = true;
    SpinLockRelease(&t_thrd.walsender_cxt.WalSndCtl->mutex);

    ereport(LOG,
        (errmodule(MOD_DMS), errmsg("[SS switchover] Recv %s demote request from DMS reformer.",
            DemoteModeDesc(demote_mode))));

    SendPostmasterSignal(PMSIGNAL_DEMOTE_PRIMARY);

    const int WAIT_DEMOTE = 6000;  /* wait up to 10 min in case of too many dirty pages to be flushed */
    for (int ntries = 0;; ntries++) {
        if (pmState == PM_RUN && g_instance.dms_cxt.SSClusterState == NODESTATE_PROMOTE_APPROVE) {
            SpinLockAcquire(&t_thrd.walsender_cxt.WalSndCtl->mutex);
            t_thrd.walsender_cxt.WalSndCtl->demotion = NoDemote;
            SpinLockRelease(&t_thrd.walsender_cxt.WalSndCtl->mutex);
            ereport(LOG,
                (errmodule(MOD_DMS), errmsg("[SS switchover] Success in %s primary demote, running as standby,"
                    " waiting for reformer setting new role.", DemoteModeDesc(demote_mode))));
            return DMS_SUCCESS;
        } else {
            if (ntries >= WAIT_DEMOTE || dms_reform_failed()) {
                ereport(WARNING,
                    (errmodule(MOD_DMS), errmsg("[SS switchover] Failure in %s primary demote, need reform recovery.",
                        DemoteModeDesc(demote_mode))));
                return DMS_ERROR;
            }
            ntries = 0;
        }

        CHECK_FOR_INTERRUPTS();
        pg_usleep(100000L); /* wait 0.1 sec, then retry */
    }
    return DMS_ERROR;
}

static int CBDbIsPrimary(void *db_handle)
{
    return g_instance.dms_cxt.SSReformerControl.primaryInstId == SS_MY_INST_ID ? 1 : 0;
}

static int CBSwitchoverPromote(void *db_handle, unsigned char origPrimaryId)
{
    g_instance.dms_cxt.SSClusterState = NODESTATE_STANDBY_PROMOTING;
    g_instance.dms_cxt.SSRecoveryInfo.new_primary_reset_walbuf_flag = true;
    /* allow recovery in switchover to keep LSN in order */
    t_thrd.shemem_ptr_cxt.XLogCtl->IsRecoveryDone = false;
    t_thrd.shemem_ptr_cxt.XLogCtl->SharedRecoveryInProgress = true;
    t_thrd.shemem_ptr_cxt.ControlFile->state = DB_IN_CRASH_RECOVERY;
    pg_memory_barrier();
    ereport(LOG, (errmodule(MOD_DMS), errmsg("[SS switchover] Starting to promote standby.")));

    SSNotifySwitchoverPromote();

    const int WAIT_PROMOTE = 1200;  /* wait 120 sec */
    for (int ntries = 0;; ntries++) {
        if (pmState == PM_RUN && g_instance.dms_cxt.SSClusterState == NODESTATE_STANDBY_PROMOTED) {
            /* flush control file primary id in advance to save new standby's waiting time */
            SSSavePrimaryInstId(SS_MY_INST_ID);

            SSReadControlFile(REFORM_CTRL_PAGE);
            Assert(SSGetPrimaryInstId() == SS_MY_INST_ID);
            ereport(LOG, (errmodule(MOD_DMS),
                errmsg("[SS switchover] Standby promote: success, set new primary:%d.", SS_MY_INST_ID)));
            return DMS_SUCCESS;
        } else {
            if (ntries >= WAIT_PROMOTE || dms_reform_failed()) {
                ereport(WARNING, (errmodule(MOD_DMS),
                    errmsg("[SS switchover] Standby promote timeout, please try again later.")));
                return DMS_ERROR;
            }
            ntries = 0;
        }

        CHECK_FOR_INTERRUPTS();
        pg_usleep(100000L); /* wait 0.1 sec, then retry */
    }
    return DMS_ERROR;
}

/* only sets switchover errno, everything else set in setPrimaryId */
static void CBSwitchoverResult(void *db_handle, int result)
{
    if (result == DMS_SUCCESS) {
        ereport(LOG, (errmodule(MOD_DMS),
            errmsg("[SS switchover] Switchover success, letting reformer update roles.")));
        return;
    } else {
        /* abort and restore state */
        g_instance.dms_cxt.SSReformInfo.in_reform = false;
        g_instance.dms_cxt.SSClusterState = NODESTATE_NORMAL;
        ereport(WARNING, (errmodule(MOD_DMS), errmsg("[SS switchover] Switchover failed, errno: %d.", result)));
    }
}

static int SetPrimaryIdOnStandby(int primary_id)
{
    g_instance.dms_cxt.SSReformerControl.primaryInstId = primary_id;

    for (int ntries = 0;; ntries++) {
        SSReadControlFile(REFORM_CTRL_PAGE); /* need to double check */
        if (g_instance.dms_cxt.SSReformerControl.primaryInstId == primary_id) {
            ereport(LOG, (errmodule(MOD_DMS),
                errmsg("[SS %s] Reform success, this is a standby:%d confirming new primary:%d.",
                    SS_PERFORMING_SWITCHOVER ? "switchover" : "reform", SS_MY_INST_ID, primary_id)));
            return DMS_SUCCESS;
        } else {
            if (ntries >= WAIT_REFORM_CTRL_REFRESH_TRIES) {
                ereport(ERROR,
                    (errmodule(MOD_DMS), errmsg("[SS %s] Failed to confirm new primary: %d,"
                        " control file indicates primary is %d; wait timeout.",
                        SS_PERFORMING_SWITCHOVER ? "switchover" : "reform", (int)primary_id,
                        g_instance.dms_cxt.SSReformerControl.primaryInstId)));
                return DMS_ERROR;
            }
        }

        CHECK_FOR_INTERRUPTS();
        pg_usleep(REFORM_WAIT_TIME); /* wait 0.01 sec, then retry */
    }

    return DMS_ERROR;
}

/* called on both new primary and all standby nodes to refresh status */
static int CBSaveStableList(void *db_handle, unsigned long long list_stable, unsigned char reformer_id,
                            unsigned int save_ctrl)
{
    int primary_id = (int)reformer_id;
    g_instance.dms_cxt.SSReformerControl.primaryInstId = primary_id;
    g_instance.dms_cxt.SSReformerControl.list_stable = list_stable;
    int ret = DMS_ERROR;
    SSLockReleaseAll();
    if ((int)primary_id == SS_MY_INST_ID) {
        if (g_instance.dms_cxt.SSClusterState > NODESTATE_NORMAL) {
            Assert(g_instance.dms_cxt.SSClusterState == NODESTATE_STANDBY_PROMOTED ||
                g_instance.dms_cxt.SSClusterState == NODESTATE_STANDBY_FAILOVER_PROMOTING);
        }
        SSSaveReformerCtrl();
        Assert(g_instance.dms_cxt.SSReformerControl.primaryInstId == (int)primary_id);
        ereport(LOG, (errmodule(MOD_DMS), errmsg("[SS %s] set current instance:%d as primary.",
            SS_PERFORMING_SWITCHOVER ? "switchover" : "reform", primary_id)));
        if (g_instance.dms_cxt.SSClusterState == NODESTATE_NORMAL) {
            /* only send to standby recoveried or new joined */
            SSLockAcquireAll();
        }
        ret = DMS_SUCCESS;
    } else { /* we are on standby */
        ret = SetPrimaryIdOnStandby(primary_id);
    }
    return ret;
}

/* currently not used in switchover, everything set in setPrimaryId */
static void CBSetDbStandby(void *db_handle)
{
    /* nothing to do now, but need to implements callback interface */
}

static void ReleaseResource()
{
    LWLockReleaseAll();
    AbortBufferIO();
    UnlockBuffers();
    /* buffer pins are released here: */
    ResourceOwnerRelease(t_thrd.utils_cxt.CurrentResourceOwner, RESOURCE_RELEASE_BEFORE_LOCKS, false, true);
    FlushErrorState();
}

static unsigned int CBPageHashCode(const char pageid[DMS_PAGEID_SIZE])
{
    BufferTag *tag = (BufferTag *)pageid;
    return BufTableHashCode(tag);
}

static unsigned long long CBGetPageLSN(const dms_buf_ctrl_t *buf_ctrl)
{
    Assert(buf_ctrl->buf_id < TOTAL_BUFFER_NUM);
    if (buf_ctrl->buf_id >= TOTAL_BUFFER_NUM) {
        return 0;
    }
    BufferDesc* buf_desc = GetBufferDescriptor(buf_ctrl->buf_id);
    XLogRecPtr lsn = BufferGetLSN(buf_desc);
    return lsn;
}

static unsigned long long CBGetGlobalLSN(void *db_handle)
{
    return GetInsertRecPtr();
}

static void DmsReleaseBuffer(int buffer, bool is_seg)
{
    if (is_seg) {
        SegReleaseBuffer(buffer);
    } else {
        ReleaseBuffer(buffer);
    }
}

static void tryEnterLocalPage(BufferTag *tag, dms_lock_mode_t mode, dms_buf_ctrl_t **buf_ctrl)
{
    bool is_seg;
    int buf_id = -1;
    uint32 hash;
    LWLock *partition_lock = NULL;
    BufferDesc *buf_desc = NULL;
    RelFileNode relfilenode = tag->rnode;

#ifdef USE_ASSERT_CHECKING
    if (IsSegmentPhysicalRelNode(relfilenode)) {
        SegSpace *spc = spc_open(relfilenode.spcNode, relfilenode.dbNode, false, false);
        BlockNumber spc_nblocks = spc_size(spc, relfilenode.relNode, tag->forkNum);
        if (tag->blockNum >= spc_nblocks) {
            ereport(PANIC, (errmodule(MOD_DMS),
                errmsg("unexpected blocknum %u >= spc nblocks %u", tag->blockNum, spc_nblocks)));
        }
    }
#endif

    *buf_ctrl = NULL;
    hash = BufTableHashCode(tag);
    partition_lock = BufMappingPartitionLock(hash);

    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;
    PG_TRY();
    {
        do {
            (void)LWLockAcquire(partition_lock, LW_SHARED);
            buf_id = BufTableLookup(tag, hash);
            if (buf_id < 0) {
                LWLockRelease(partition_lock);
                break;
            }

            buf_desc = GetBufferDescriptor(buf_id);
            if (IsSegmentBufferID(buf_id)) {
                (void)SegPinBuffer(buf_desc);
                is_seg = true;
            } else {
                ResourceOwnerEnlargeBuffers(t_thrd.utils_cxt.CurrentResourceOwner);
                (void)PinBuffer(buf_desc, NULL);
                is_seg = false;
            }
            LWLockRelease(partition_lock);

            WaitIO(buf_desc);

            if (!(pg_atomic_read_u32(&buf_desc->state) & BM_VALID)) {
                ereport(WARNING, (errmodule(MOD_DMS),
                    errmsg("[%d/%d/%d/%d %d-%d] try enter page failed, buffer is not valid, state = 0x%x",
                    tag->rnode.spcNode, tag->rnode.dbNode, tag->rnode.relNode, tag->rnode.bucketNode,
                    tag->forkNum, tag->blockNum, buf_desc->state)));
                DmsReleaseBuffer(buf_desc->buf_id + 1, is_seg);
                break;
            }

            if (pg_atomic_read_u32(&buf_desc->state) & BM_IO_ERROR) {
                ereport(WARNING, (errmodule(MOD_DMS),
                    errmsg("[%d/%d/%d/%d %d-%d] try enter page failed, buffer is io error, state = 0x%x",
                    tag->rnode.spcNode, tag->rnode.dbNode, tag->rnode.relNode, tag->rnode.bucketNode,
                    tag->forkNum, tag->blockNum, buf_desc->state)));
                DmsReleaseBuffer(buf_desc->buf_id + 1, is_seg);
                break;
            }

            LWLockMode content_mode = (mode == DMS_LOCK_SHARE) ? LW_SHARED : LW_EXCLUSIVE;
            (void)LWLockAcquire(buf_desc->content_lock, content_mode);
            *buf_ctrl = GetDmsBufCtrl(buf_id);
            Assert(buf_id >= 0);
            if ((*buf_ctrl)->lock_mode == DMS_LOCK_NULL) {
                ereport(WARNING, (errmodule(MOD_DMS),
                    errmsg("[%u/%u/%u/%d %d-%u] lock mode is null, still need to transfer page",
                    tag->rnode.spcNode, tag->rnode.dbNode, tag->rnode.relNode, tag->rnode.bucketNode,
                    tag->forkNum, tag->blockNum)));
            }
            (*buf_ctrl)->seg_fileno = buf_desc->extra->seg_fileno;
            (*buf_ctrl)->seg_blockno = buf_desc->extra->seg_blockno;
        } while (0);
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        ReleaseResource();
    }
    PG_END_TRY();
}

static int CBEnterLocalPage(void *db_handle, char pageid[DMS_PAGEID_SIZE], dms_lock_mode_t mode,
    dms_buf_ctrl_t **buf_ctrl)
{
    BufferTag *tag = (BufferTag *)pageid;
    tryEnterLocalPage(tag, mode, buf_ctrl);
    return  DMS_SUCCESS;
}

static unsigned char CBPageDirty(dms_buf_ctrl_t *buf_ctrl)
{
    Assert(buf_ctrl->buf_id < TOTAL_BUFFER_NUM);
    if (buf_ctrl->buf_id >= TOTAL_BUFFER_NUM) {
        return 0;
    }
    BufferDesc *buf_desc = GetBufferDescriptor(buf_ctrl->buf_id);
    bool is_dirty = (pg_atomic_read_u32(&buf_desc->state) & (BM_DIRTY | BM_JUST_DIRTIED)) > 0;
    return (unsigned char)is_dirty;
}

static void CBLeaveLocalPage(void *db_handle, dms_buf_ctrl_t *buf_ctrl)
{
    Assert(buf_ctrl->buf_id < TOTAL_BUFFER_NUM);
    if (buf_ctrl->buf_id >= TOTAL_BUFFER_NUM) {
        return;
    }

    if (IsSegmentBufferID(buf_ctrl->buf_id)) {
        SegUnlockReleaseBuffer(buf_ctrl->buf_id + 1);
    } else {
        UnlockReleaseBuffer(buf_ctrl->buf_id + 1);
    }
}

static char* CBGetPage(dms_buf_ctrl_t *buf_ctrl)
{
    Assert(buf_ctrl->buf_id < TOTAL_BUFFER_NUM);
    if (buf_ctrl->buf_id >= TOTAL_BUFFER_NUM) {
        return NULL;
    }
    BufferDesc *buf_desc = GetBufferDescriptor(buf_ctrl->buf_id);
    return (char *)BufHdrGetBlock(buf_desc);
}

static int CBInvalidatePage(void *db_handle, char pageid[DMS_PAGEID_SIZE], unsigned int ver)
{
    int buf_id = -1;
    BufferTag* tag = (BufferTag *)pageid;
    uint32 hash;
    LWLock *partition_lock = NULL;
    int ret = DMS_SUCCESS;

    hash = BufTableHashCode(tag);
    partition_lock = BufMappingPartitionLock(hash);
    (void)LWLockAcquire(partition_lock, LW_SHARED);
    buf_id = BufTableLookup(tag, hash);
    if (buf_id < 0) {
        /* not found in shared buffer */
        LWLockRelease(partition_lock);
        return ret;
    }

    BufferDesc *buf_desc = GetBufferDescriptor(buf_id);
    dms_buf_ctrl_t *buf_ctrl = GetDmsBufCtrl(buf_id);
    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;
    PG_TRY();
    {
        buf_desc = GetBufferDescriptor(buf_id);
        if (IsSegmentBufferID(buf_id)) {
            (void)SegPinBuffer(buf_desc);
        } else {
            ResourceOwnerEnlargeBuffers(t_thrd.utils_cxt.CurrentResourceOwner);
            (void)PinBuffer(buf_desc, NULL);
        }
        LWLockRelease(partition_lock);

        WaitIO(buf_desc);
        if ((!(pg_atomic_read_u32(&buf_desc->state) & BM_VALID)) ||
            (pg_atomic_read_u32(&buf_desc->state) & BM_IO_ERROR)) {
            ereport(WARNING, (errmodule(MOD_DMS),
                errmsg("[%d/%d/%d/%d %d-%d] invalidate page failed, buffer is not valid or io error, state = 0x%x",
                tag->rnode.spcNode, tag->rnode.dbNode, tag->rnode.relNode, tag->rnode.bucketNode,
                tag->forkNum, tag->blockNum, buf_desc->state)));
            DmsReleaseBuffer(buf_desc->buf_id + 1, IsSegmentBufferID(buf_id));
            ret = DMS_ERROR;
            break;
        }

        (void)LWLockAcquire(buf_desc->content_lock, LW_EXCLUSIVE);
        buf_ctrl = GetDmsBufCtrl(buf_id);
        if (ver == buf_ctrl->ver) {
            buf_ctrl->lock_mode = (unsigned char)DMS_LOCK_NULL;
        } else {
            ereport(WARNING, (errmodule(MOD_DMS),
                errmsg("[CBInvalidatePage] invalid ver:%u, buf_ctrl ver:%u", ver, buf_ctrl->ver)));
            ret = DMS_ERROR;
        }
        LWLockRelease(buf_desc->content_lock);

        if (IsSegmentBufferID(buf_id)) {
            SegReleaseBuffer(buf_id + 1);
        } else {
            ReleaseBuffer(buf_id + 1);
        }
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        /* Save error info */
        ErrorData* edata = CopyErrorData();
        FlushErrorState();
        FreeErrorData(edata);
        ereport(WARNING, (errmsg("[CBInvalidatePage] Error happend, spc/db/rel/bucket fork-block: %u/%u/%u/%d %d-%u",
            tag->rnode.spcNode, tag->rnode.dbNode, tag->rnode.relNode, tag->rnode.bucketNode, tag->forkNum,
            tag->blockNum)));
        ReleaseResource();
        ret = DMS_ERROR;
    }
    PG_END_TRY();

    if (ret == DMS_SUCCESS) {
        Assert(buf_ctrl->lock_mode == DMS_LOCK_NULL);
    }

    return ret;
}

static void CBVerifyPage(dms_buf_ctrl_t *buf_ctrl, char *new_page)
{
    Assert(buf_ctrl->buf_id < TOTAL_BUFFER_NUM);
    if (buf_ctrl->buf_id >= TOTAL_BUFFER_NUM) {
        return;
    }

    BufferDesc *buf_desc = GetBufferDescriptor(buf_ctrl->buf_id);

    if (buf_desc->extra->seg_fileno == EXTENT_INVALID) {
        buf_desc->extra->seg_fileno = buf_ctrl->seg_fileno;
        buf_desc->extra->seg_blockno = buf_ctrl->seg_blockno;
    } else if (buf_desc->extra->seg_fileno != buf_ctrl->seg_fileno ||
               buf_desc->extra->seg_blockno != buf_ctrl->seg_blockno) {
        ereport(PANIC, (errmsg("[%u/%u/%u/%d/%d %d-%u] location mismatch, seg_fileno:%d, seg_blockno:%u",
                               buf_desc->tag.rnode.spcNode, buf_desc->tag.rnode.dbNode, buf_desc->tag.rnode.relNode,
                               buf_desc->tag.rnode.bucketNode, buf_desc->tag.rnode.opt, buf_desc->tag.forkNum,
                               buf_desc->tag.blockNum, buf_desc->extra->seg_fileno, buf_desc->extra->seg_blockno)));
    }

    /* page content is not valid */
    if ((pg_atomic_read_u32(&buf_desc->state) & BM_VALID) == 0) {
        return;
    }

    /* we only verify segment-page version */
    if (!(buf_desc->extra->seg_fileno != EXTENT_INVALID || IsSegmentBufferID(buf_desc->buf_id))) {
        return;
    }

    char *page = (char *)BufHdrGetBlock(buf_desc);
    XLogRecPtr lsn_past = PageGetLSN(page);
    XLogRecPtr lsn_now = PageGetLSN(new_page);
    if ((lsn_now != InvalidXLogRecPtr) && (lsn_past > lsn_now)) {
        RelFileNode rnode = buf_desc->tag.rnode;
        ereport(PANIC, (errmsg("[%d/%d/%d/%d/%d %d-%d] now lsn(0x%llx) is less than past lsn(0x%llx)",
            rnode.spcNode, rnode.dbNode, rnode.relNode, rnode.bucketNode, rnode.opt,
            buf_desc->tag.forkNum, buf_desc->tag.blockNum,
            (unsigned long long)lsn_now, (unsigned long long)lsn_past)));
    }
    return;
}

static int CBXLogFlush(void *db_handle, unsigned long long *lsn)
{
    (void)LWLockAcquire(WALWriteLock, LW_EXCLUSIVE);
    (void)XLogBackgroundFlush();
    *lsn = GetFlushRecPtr();
    LWLockRelease(WALWriteLock);
    return GS_SUCCESS;
}

static char *CBDisplayBufferTag(char *displayBuf, unsigned int count, char *pageid)
{
    BufferTag pagetag = *(BufferTag *)pageid;
    int ret = sprintf_s(displayBuf, count, "%u/%u/%u/%d/%d %d-%u",
        pagetag.rnode.spcNode, pagetag.rnode.dbNode, pagetag.rnode.relNode, (int)pagetag.rnode.bucketNode,
        (int)pagetag.rnode.opt, pagetag.forkNum, pagetag.blockNum);
    securec_check_ss(ret, "", "");
    return displayBuf;
}

static int CBRemoveBufLoadStatus(dms_buf_ctrl_t *buf_ctrl, dms_buf_load_status_t dms_buf_load_status)
{
    switch (dms_buf_load_status) {
        case DMS_BUF_NEED_LOAD:
            buf_ctrl->state &= ~BUF_NEED_LOAD;
            break;
        case DMS_BUF_IS_LOADED:
            buf_ctrl->state &= ~BUF_IS_LOADED;
            break;
        case DMS_BUF_LOAD_FAILED:
            buf_ctrl->state &= ~BUF_LOAD_FAILED;
            break;
        case DMS_BUF_NEED_TRANSFER:
            buf_ctrl->state &= ~BUF_NEED_TRANSFER;
            break;
        default:
            Assert(0);
    }
    return DMS_SUCCESS;
}

static int CBSetBufLoadStatus(dms_buf_ctrl_t *buf_ctrl, dms_buf_load_status_t dms_buf_load_status)
{
    switch (dms_buf_load_status) {
        case DMS_BUF_NEED_LOAD:
            buf_ctrl->state |= BUF_NEED_LOAD;
            break;
        case DMS_BUF_IS_LOADED:
            buf_ctrl->state |= BUF_IS_LOADED;
            break;
        case DMS_BUF_LOAD_FAILED:
            buf_ctrl->state |= BUF_LOAD_FAILED;
            break;
        case DMS_BUF_NEED_TRANSFER:
            buf_ctrl->state |= BUF_NEED_TRANSFER;
            break;
        default:
            Assert(0);
    }
    return DMS_SUCCESS;
}

static void *CBGetHandle(unsigned int *db_handle_index)
{
    void *db_handle = g_instance.proc_base->allProcs[g_instance.dms_cxt.dmsProcSid];
    *db_handle_index = pg_atomic_fetch_add_u32(&g_instance.dms_cxt.dmsProcSid, 1);
    return db_handle;
}

static char *CBMemAlloc(void *context, unsigned int size)
{
    char *ptr = NULL;
    MemoryContext old_cxt = MemoryContextSwitchTo(t_thrd.dms_cxt.msgContext);
    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;

    PG_TRY();
    {
        ptr = (char *)palloc(size);
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        if (t_thrd.role == DMS_WORKER) {
            FlushErrorState();
        }
    }
    PG_END_TRY();

    (void)MemoryContextSwitchTo(old_cxt);
    return ptr;
}

static void CBMemFree(void *context, void *pointer)
{
    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;

    PG_TRY();
    {
        pfree(pointer);
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        if (t_thrd.role == DMS_WORKER) {
            FlushErrorState();
        }
    }
    PG_END_TRY();
}

static void CBMemReset(void *context)
{
    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;
    PG_TRY();
    {
        MemoryContextReset(t_thrd.dms_cxt.msgContext);
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        if (t_thrd.role == DMS_WORKER) {
            FlushErrorState();
        }
    }
    PG_END_TRY();
}

static int32 CBProcessLockAcquire(char *data, uint32 len)
{
    if (unlikely(len != sizeof(SSBroadcastDDLLock))) {
        ereport(DEBUG1, (errmsg("invalid broadcast ddl lock message")));
        return DMS_ERROR;
    }

    SSBroadcastDDLLock *ssmsg = (SSBroadcastDDLLock *)data;
    LockAcquireResult res;
    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;
    PG_TRY();
    {
        res = LockAcquire(&(ssmsg->locktag), ssmsg->lockmode, false, ssmsg->dontWait);
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        res = LOCKACQUIRE_NOT_AVAIL;
        ereport(WARNING, (errmsg("SS Standby process DDLLockAccquire got in PG_CATCH")));
        if (t_thrd.role == DMS_WORKER) {
            FlushErrorState();
        }
    }
    PG_END_TRY();

    if (!(ssmsg->dontWait) && res == LOCKACQUIRE_NOT_AVAIL) {
        ereport(WARNING, (errmsg("SS process DDLLockAccquire request failed!")));
        return DMS_ERROR;
    }
    return DMS_SUCCESS;
}

static int32 CBProcessLockRelease(char *data, uint32 len)
{
    if (unlikely(len != sizeof(SSBroadcastDDLLock))) {
        ereport(DEBUG1, (errmsg("invalid lock release message")));
        return DMS_ERROR;
    }

    SSBroadcastDDLLock *ssmsg = (SSBroadcastDDLLock *)data;
    int res = DMS_SUCCESS;
    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;
    PG_TRY();
    {
        (void)LockRelease(&(ssmsg->locktag), ssmsg->lockmode, ssmsg->sessionlock);
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        res = DMS_ERROR;
        ereport(WARNING, (errmsg("SS process DDLLockRelease request failed!")));
        if (t_thrd.role == DMS_WORKER) {
            FlushErrorState();
        }
    }
    PG_END_TRY();

    return res;
}

static int32 CBProcessReleaseAllLock(uint32 len)
{
    if (unlikely(len != sizeof(SSBroadcastCmdOnly))) {
        return DMS_ERROR;
    }

    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;
    int res = DMS_SUCCESS;
    PG_TRY();
    {
        LockErrorCleanup();
        LockReleaseAll(DEFAULT_LOCKMETHOD, true);
        LockReleaseAll(USER_LOCKMETHOD, true);
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        res = DMS_ERROR;
        ereport(WARNING, (errmsg("SS process DDLLockReleaseAll request failed!")));
        if (t_thrd.role == DMS_WORKER) {
            FlushErrorState();
        }
    }
    PG_END_TRY();

    return res;
}

static int32 CBProcessBroadcast(void *db_handle, char *data, unsigned int len, char *output_msg,
    uint32 *output_msg_len)
{
    int32 ret = DMS_SUCCESS;
    SSBroadcastOp bcast_op = *(SSBroadcastOp *)data;

    *output_msg_len = 0;
    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;

    PG_TRY();
    {
        switch (bcast_op) {
            case BCAST_GET_XMIN:
                ret = SSGetOldestXmin(data, len, output_msg, output_msg_len);
                break;
            case BCAST_SI:
                ret = SSProcessSharedInvalMsg(data, len);
                break;
            case BCAST_SEGDROPTL:
                ret = SSProcessSegDropTimeline(data, len);
                break;
            case BCAST_DROP_REL_ALL_BUFFER:
                ret = SSProcessDropRelAllBuffer(data, len);
                break;
            case BCAST_DROP_REL_RANGE_BUFFER:
                ret = SSProcessDropRelRangeBuffer(data, len);
                break;
            case BCAST_DROP_DB_ALL_BUFFER:
                ret = SSProcessDropDBAllBuffer(data, len);
                break;
            case BCAST_DROP_SEG_SPACE:
                ret = SSProcessDropSegSpace(data, len);
                break;
            case BCAST_DDLLOCK:
                ret = CBProcessLockAcquire(data, len);
                break;
            case BCAST_DDLLOCKRELEASE:
                ret = CBProcessLockRelease(data, len);
                break;
            case BCAST_DDLLOCKRELEASE_ALL:
                ret = CBProcessReleaseAllLock(len);
                break;
            case BCAST_CHECK_DB_BACKENDS:
                ret = SSCheckDbBackends(data, len, output_msg, output_msg_len);
                break;
            default:
                ereport(WARNING, (errmodule(MOD_DMS), errmsg("invalid broadcast operate type")));
                ret = DMS_ERROR;
                break;
        }
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        if (t_thrd.role == DMS_WORKER) {
            FlushErrorState();
        }
    }
    PG_END_TRY();

    return ret;
}

static int32 CBProcessBroadcastAck(void *db_handle, char *data, unsigned int len)
{
    int32 ret = DMS_SUCCESS;
    SSBroadcastOpAck bcast_op = *(SSBroadcastOpAck *)data;

    switch (bcast_op) {
        case BCAST_GET_XMIN_ACK:
            ret = SSGetOldestXminAck((SSBroadcastXminAck *)data);
            break;
        case BCAST_CHECK_DB_BACKENDS_ACK:
            ret = SSCheckDbBackendsAck(data, len);
            break;
        default:
            ereport(WARNING, (errmodule(MOD_DMS), errmsg("invalid broadcast ack type")));
            ret = DMS_ERROR;
    }
    return ret;
}

static int CBGetDmsStatus(void *db_handle)
{
    return (int)g_instance.dms_cxt.dms_status;
}

static void CBSetDmsStatus(void *db_handle, int dms_status)
{
    g_instance.dms_cxt.dms_status = (dms_status_t)dms_status;
}

static int32 CBDrcBufRebuild(void *db_handle)
{
    /* Load Control File */
    int src_id = SSGetPrimaryInstId();
    SSReadControlFile(src_id, true);

    uint32 buf_state;
    for (int i = 0; i < TOTAL_BUFFER_NUM; i++) {
        BufferDesc *buf_desc = GetBufferDescriptor(i);
        buf_state = LockBufHdr(buf_desc);
        if ((buf_state & BM_VALID) || (buf_state & BM_TAG_VALID)) {
            int ret = CheckBuf4Rebuild(buf_desc);
            if (ret != DMS_SUCCESS) {
                UnlockBufHdr(buf_desc, buf_state);
                return ret;
            }
        }
        UnlockBufHdr(buf_desc, buf_state);
    }
    return GS_SUCCESS;
}

static int32 CBDrcBufValidate(void *db_handle)
{
    /* Load Control File */
    int src_id = SSGetPrimaryInstId();
    SSReadControlFile(src_id, true);
    int buf_cnt = 0;

    uint32 buf_state;
    ereport(LOG, (errmodule(MOD_DMS),
        errmsg("[SS reform]CBDrcBufValidate starts before reform done.")));
    for (int i = 0; i < TOTAL_BUFFER_NUM; i++) {
        BufferDesc *buf_desc = GetBufferDescriptor(i);
        buf_state = LockBufHdr(buf_desc);
        if ((buf_state & BM_VALID) || (buf_state & BM_TAG_VALID)) {
            BufValidateDrc(buf_desc);
            buf_cnt++;
        }
        UnlockBufHdr(buf_desc, buf_state);
    }

    ereport(LOG, (errmodule(MOD_DMS),
        errmsg("[SS reform]CBDrcBufValidate %d buffers success.", buf_cnt)));
    return GS_SUCCESS;
}

// used for find bufferdesc in dms
static void SSGetBufferDesc(char *pageid, bool *is_valid, BufferDesc** ret_buf_desc)
{
    int buf_id;
    uint32 hash;
    LWLock *partition_lock = NULL;
    BufferTag *tag = (BufferTag *)pageid;
    BufferDesc *buf_desc;

    RelFileNode relfilenode = tag->rnode;

#ifdef USE_ASSERT_CHECKING
    if (IsSegmentPhysicalRelNode(relfilenode)) {
        SegSpace *spc = spc_open(relfilenode.spcNode, relfilenode.dbNode, false, false);
        BlockNumber spc_nblocks = spc_size(spc, relfilenode.relNode, tag->forkNum);
        if (tag->blockNum >= spc_nblocks) {
            ereport(PANIC, (errmodule(MOD_DMS),
                errmsg("unexpected blocknum %u >= spc nblocks %u", tag->blockNum, spc_nblocks)));
        }
    }
#endif

    hash = BufTableHashCode(tag);
    partition_lock = BufMappingPartitionLock(hash);

    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;
    PG_TRY();
    {
        (void)LWLockAcquire(partition_lock, LW_SHARED);
        buf_id = BufTableLookup(tag, hash);
        if (buf_id >= 0) {
            buf_desc = GetBufferDescriptor(buf_id);
            if (IsSegmentBufferID(buf_id)) {
                (void)SegPinBuffer(buf_desc);
            } else {
                ResourceOwnerEnlargeBuffers(t_thrd.utils_cxt.CurrentResourceOwner);
                (void)PinBuffer(buf_desc, NULL);
            }
            LWLockRelease(partition_lock);

            WaitIO(buf_desc);
            Assert(!(pg_atomic_read_u32(&buf_desc->state) & BM_IO_ERROR));
            *is_valid = (pg_atomic_read_u32(&buf_desc->state) & BM_VALID) != 0;
            *ret_buf_desc = buf_desc;
        } else {
            LWLockRelease(partition_lock);
            *ret_buf_desc = NULL;
        }
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        ReleaseResource();
    }
    PG_END_TRY();
}

void SSUnPinBuffer(BufferDesc* buf_desc)
{
    if (IsSegmentBufferID(buf_desc->buf_id)) {
        SegUnpinBuffer(buf_desc);
    } else {
        UnpinBuffer(buf_desc, true);
    }
}

static int CBConfirmOwner(void *db_handle, char *pageid, unsigned char *lock_mode, unsigned char *is_edp,
    unsigned long long *edp_lsn)
{
    BufferDesc *buf_desc = NULL;
    bool valid;
    dms_buf_ctrl_t *buf_ctrl = NULL;

    SSGetBufferDesc(pageid, &valid, &buf_desc);
    if (buf_desc == NULL) {
        return DMS_ERROR;
    }

    if (!valid) {
        *lock_mode = (uint8)DMS_LOCK_NULL;
        *is_edp = (unsigned char)false;
        SSUnPinBuffer(buf_desc);
        return GS_SUCCESS;
    }

    /*
     * not acquire buf_desc->io_in_progress_lock
     * consistency guaranteed by reform phase
     */
    buf_ctrl = GetDmsBufCtrl(buf_desc->buf_id);
    *lock_mode = buf_ctrl->lock_mode;
    // opengauss currently no edp
    Assert(buf_ctrl->is_edp == 0);
    *is_edp = (unsigned char)false;
    SSUnPinBuffer(buf_desc);

    return GS_SUCCESS;
}

static int CBConfirmConverting(void *db_handle, char *pageid, unsigned char smon_chk,
    unsigned char *lock_mode, unsigned long long *edp_map, unsigned long long *lsn, unsigned int *ver)
{
    BufferDesc *buf_desc = NULL;
    bool valid;
    dms_buf_ctrl_t *buf_ctrl = NULL;
    bool timeout = false;

    *lsn = 0;
    *edp_map = 0;
    
    SSGetBufferDesc(pageid, &valid, &buf_desc);
    if (buf_desc == NULL) {
        return DMS_ERROR;
    }

    if (!valid) {
        *lock_mode = (uint8)DMS_LOCK_NULL;
        SSUnPinBuffer(buf_desc);
        return GS_SUCCESS;
    }

    struct timeval begin_tv;
    struct timeval now_tv;
    (void)gettimeofday(&begin_tv, NULL);
    long begin = GET_US(begin_tv);
    long now;

    while (true) {
        bool is_locked = LWLockConditionalAcquire(buf_desc->io_in_progress_lock, LW_EXCLUSIVE);
        if (is_locked) {
            buf_ctrl = GetDmsBufCtrl(buf_desc->buf_id);
            *ver = buf_ctrl->ver;
            *lock_mode = buf_ctrl->lock_mode;
            LWLockRelease(buf_desc->io_in_progress_lock);
            break;
        }

        (void)gettimeofday(&now_tv, NULL);
        now = GET_US(now_tv);
        if (now - begin > REFORM_CONFIRM_TIMEOUT) {
            timeout = true;
            break;
        }
        pg_usleep(REFORM_CONFIRM_INTERVAL); /* sleep 5ms */
    }

    if (!timeout) {
        SSUnPinBuffer(buf_desc);
        return GS_SUCCESS;
    }

    if (smon_chk) {
        SSUnPinBuffer(buf_desc);
        return GS_TIMEDOUT;
    }

    // without lock
    buf_ctrl = GetDmsBufCtrl(buf_desc->buf_id);
    *ver = buf_ctrl->ver;
    *lock_mode = buf_ctrl->lock_mode;

    SSUnPinBuffer(buf_desc);
    return GS_SUCCESS;
}

static int CBGetStableList(void *db_handle, unsigned long long *list_stable, unsigned char *reformer_id)
{
    *list_stable = g_instance.dms_cxt.SSReformerControl.list_stable;
    *reformer_id = (uint8)g_instance.dms_cxt.SSReformerControl.primaryInstId;
    return GS_SUCCESS;
}

static int CBStartup(void *db_handle)
{
    g_instance.dms_cxt.SSRecoveryInfo.ready_to_startup = true;
    return GS_SUCCESS;
}

static int CBRecoveryStandby(void *db_handle, int inst_id)
{
    Assert(inst_id == g_instance.attr.attr_storage.dms_attr.instance_id);
    ereport(LOG, (errmsg("[SS reform] Recovery as standby")));

    g_instance.dms_cxt.SSRecoveryInfo.skip_redo_replay = true;
    if (!SSRecoveryNodes()) {
        ereport(WARNING, (errmodule(MOD_DMS), errmsg("Recovery failed in startup first")));
        return GS_ERROR;
    }
    g_instance.dms_cxt.SSRecoveryInfo.skip_redo_replay = false;

    return GS_SUCCESS;
}

static int CBRecoveryPrimary(void *db_handle, int inst_id)
{
    Assert(g_instance.dms_cxt.SSReformerControl.primaryInstId == inst_id ||
        g_instance.dms_cxt.SSReformerControl.primaryInstId == -1);
    g_instance.dms_cxt.SSRecoveryInfo.skip_redo_replay = false;
    g_instance.dms_cxt.SSRecoveryInfo.in_flushcopy = false;
    ereport(LOG, (errmsg("[SS reform] Recovery as primary, will replay xlog from inst:%d",
                         g_instance.dms_cxt.SSReformerControl.primaryInstId)));

    if (!SSRecoveryNodes()) {
        ereport(WARNING, (errmodule(MOD_DMS), errmsg("Recovery failed in startup first")));
        return GS_ERROR;
    }

    return GS_SUCCESS;
}

static int CBFlushCopy(void *db_handle, char *pageid)
{
    if (SS_REFORM_REFORMER && !g_instance.dms_cxt.SSRecoveryInfo.in_flushcopy) {
        g_instance.dms_cxt.SSRecoveryInfo.in_flushcopy = true;
        smgrcloseall();
    }

    // only 1) primary restart 2) failover need flush_copy
    if (SS_REFORM_REFORMER && g_instance.dms_cxt.dms_status == DMS_STATUS_IN && !SS_STANDBY_FAILOVER) {
        return GS_SUCCESS;
    }

    BufferTag* tag = (BufferTag*)pageid;
    Buffer buffer;
    SegSpace *spc = NULL;

    uint32 saveInterruptHoldoffCount = t_thrd.int_cxt.InterruptHoldoffCount;
    PG_TRY();
    {
        if (IsSegmentPhysicalRelNode(tag->rnode)) {
            spc = spc_open(tag->rnode.spcNode, tag->rnode.dbNode, false, false);
            buffer = ReadBufferFast(spc, tag->rnode, tag->forkNum, tag->blockNum, RBM_NORMAL);
        } else {
            buffer = ReadBufferWithoutRelcache(tag->rnode, tag->forkNum, tag->blockNum, RBM_NORMAL, NULL, NULL);
        }
    }
    PG_CATCH();
    {
        t_thrd.int_cxt.InterruptHoldoffCount = saveInterruptHoldoffCount;
        /* Save error info */
        ErrorData* edata = CopyErrorData();
        FlushErrorState();
        FreeErrorData(edata);
        ereport(PANIC, (errmsg("[SS flush copy] Error happend, spc/db/rel/bucket fork-block: %u/%u/%u/%d %d-%u",
                        tag->rnode.spcNode, tag->rnode.dbNode, tag->rnode.relNode, tag->rnode.bucketNode,
                        tag->forkNum, tag->blockNum)));
    }
    PG_END_TRY();

    /**
     *  when remote DB instance reboot, this round reform fail
     *  primary node may fail to get page from remote node which reboot, this phase should return fail
     */
    if (BufferIsInvalid(buffer)) {
        if (dms_reform_failed()) {
            return GS_ERROR;
        } else {
            Assert(0);
        }
    }

    Assert(XLogRecPtrIsValid(g_instance.dms_cxt.ckptRedo));
    LockBuffer(buffer, BUFFER_LOCK_SHARE);
    BufferDesc* buf_desc = GetBufferDescriptor(buffer - 1);
    XLogRecPtr pagelsn = BufferGetLSN(buf_desc);
    if (XLByteLT(g_instance.dms_cxt.ckptRedo, pagelsn)) {
        dms_buf_ctrl_t *buf_ctrl = GetDmsBufCtrl(buffer - 1);
        buf_ctrl->state |= BUF_DIRTY_NEED_FLUSH;
        ereport(LOG, (errmsg("[SS] Mark need flush in flush copy, spc/db/rel/bucket fork-block: %u/%u/%u/%d %d-%u, page lsn (0x%llx)",
                            tag->rnode.spcNode, tag->rnode.dbNode, tag->rnode.relNode, tag->rnode.bucketNode,
                            tag->forkNum, tag->blockNum, (unsigned long long)pagelsn)));
    } else {
        ereport(LOG, (errmsg("[SS] ready to flush copy, spc/db/rel/bucket fork-block: %u/%u/%u/%d %d-%u, page lsn (0x%llx)",
                        tag->rnode.spcNode, tag->rnode.dbNode, tag->rnode.relNode, tag->rnode.bucketNode,
                        tag->forkNum, tag->blockNum, (unsigned long long)pagelsn)));
    }
    LockBuffer(buffer, BUFFER_LOCK_UNLOCK);
    ReleaseBuffer(buffer);
    return GS_SUCCESS;
}

static int CBFailoverPromote(void *db_handle)
{
    g_instance.dms_cxt.SSRecoveryInfo.no_backend_left = false;
    SSTriggerFailover();
    /**
     * for alive failover: wait for backend threads to exit, at most 30s
     * why wait code write this
     *      step 1, sned PMSIGNAL_DMS_TRIGGERFAILOVER to tell thread to exit
     *      step 2, PM detected backend exit
     *      step 3, reform proc wait
     */
    if (!g_instance.dms_cxt.SSRecoveryInfo.startup_reform) {
        long max_wait_time = 30000000L;
        long wait_time = 0;
        while (true) {
            if (g_instance.dms_cxt.SSRecoveryInfo.no_backend_left) {
                ereport(LOG, (errmodule(MOD_DMS), errmsg("[SS failover] backends exit successfully")));
                break;
            }
            if (wait_time > max_wait_time) {
                ereport(WARNING, (errmodule(MOD_DMS), errmsg("[SS failover] failover failed, backends can not exit")));
                _exit(0);
            }
            pg_usleep(REFORM_WAIT_TIME);
            wait_time += REFORM_WAIT_TIME;
        }
        SendPostmasterSignal(PMSIGNAL_DMS_FAILOVER_STARTUP);
    }

    while (true) {
        if (SSFAILOVER_TRIGGER && g_instance.pid_cxt.StartupPID != 0) {
            ereport(LOG, (errmodule(MOD_DMS), errmsg("startup thread success.")));
            return GS_SUCCESS;
        }
        pg_usleep(REFORM_WAIT_TIME);
    }
}

static int CBGetDBPrimaryId(void *db_handle, unsigned int *primary_id)
{
    *primary_id = (unsigned int)g_instance.dms_cxt.SSReformerControl.primaryInstId;
    return GS_SUCCESS;
}

/* Currently only used in SS switchover */
static void CBReformSetDmsRole(void *db_handle, unsigned int reformer_id)
{
    ss_reform_info_t *reform_info = &g_instance.dms_cxt.SSReformInfo;
    reform_info->dms_role = reformer_id == (unsigned int)SS_MY_INST_ID ? DMS_ROLE_REFORMER : DMS_ROLE_PARTNER;
    if (reform_info->dms_role == DMS_ROLE_REFORMER) {
        /* since original primary must have demoted, it is safe to allow promting standby write */
        if (dss_set_server_status_wrapper() != GS_SUCCESS) {
            ereport(PANIC, (errmodule(MOD_DMS),
                errmsg("Could not set dssserver flag, vgname: \"%s\", socketpath: \"%s\"",
                g_instance.attr.attr_storage.dss_attr.ss_dss_vg_name,
                g_instance.attr.attr_storage.dss_attr.ss_dss_conn_path),
                errhint("Check vgname and socketpath and restart later.")));
        }
    }
    ereport(LOG, (errmodule(MOD_DMS),
        errmsg("[SS switchover]switching, updated inst:%d with role:%d success",
            SS_MY_INST_ID, reform_info->dms_role)));
}

static void CBReformStartNotify(void *db_handle, dms_role_t role, unsigned char reform_type)
{
    SSReformType ss_reform_type = (SSReformType)reform_type;
    ss_reform_info_t *reform_info = &g_instance.dms_cxt.SSReformInfo;
    g_instance.dms_cxt.SSClusterState = NODESTATE_NORMAL;
    g_instance.dms_cxt.SSRecoveryInfo.reform_ready = false;
    g_instance.dms_cxt.resetSyscache = true;
    if (ss_reform_type == DMS_REFORM_TYPE_FOR_FAILOVER_OPENGAUSS) {
        g_instance.dms_cxt.SSRecoveryInfo.in_failover = true;
        if (role == DMS_ROLE_REFORMER) {
            g_instance.dms_cxt.dw_init = false;
            // variable set order: SharedRecoveryInProgress -> failover_triggered -> dms_role
            volatile XLogCtlData *xlogctl = t_thrd.shemem_ptr_cxt.XLogCtl;
            SpinLockAcquire(&xlogctl->info_lck);
            xlogctl->IsRecoveryDone = false;
            xlogctl->SharedRecoveryInProgress = true;
            SpinLockRelease(&xlogctl->info_lck);
            t_thrd.shemem_ptr_cxt.ControlFile->state = DB_IN_CRASH_RECOVERY;
            pg_memory_barrier();
            g_instance.dms_cxt.SSRecoveryInfo.failover_triggered = true;
            g_instance.dms_cxt.SSClusterState = NODESTATE_STANDBY_FAILOVER_PROMOTING;
            ereport(LOG, (errmodule(MOD_DMS), errmsg("[SS failover] failover trigger.")));
        }
    }
    reform_info->dms_role = role;
    reform_info->in_reform = true;

    ereport(LOG, (errmodule(MOD_DMS),
        errmsg("[SS reform] dms reform start, role:%d, reform type:%d", role, (int)ss_reform_type)));
    if (reform_info->dms_role == DMS_ROLE_REFORMER) {
        if (dss_set_server_status_wrapper() != GS_SUCCESS) {
            ereport(PANIC, (errmodule(MOD_DMS), errmsg("[SS reform] Could not set dssserver flag=read_write")));
        }
    }

    /* cluster has no transactions during startup reform */
    if (!g_instance.dms_cxt.SSRecoveryInfo.startup_reform) {
        SendPostmasterSignal(PMSIGNAL_DMS_REFORM);
    }

    while (true) {
        if (dms_reform_failed()) {
            ereport(WARNING, (errmodule(MOD_DMS), errmsg("[SS reform]reform failed during caneling backends")));
            return;
        }
        if (g_instance.dms_cxt.SSRecoveryInfo.reform_ready || g_instance.dms_cxt.SSRecoveryInfo.startup_reform) {
            ereport(LOG, (errmodule(MOD_DMS), errmsg("[SS reform]reform ready, backends have been terminated")));
            return;
        }
        pg_usleep(REFORM_WAIT_TIME);
    }
}

static int CBReformDoneNotify(void *db_handle)
{
    if (g_instance.dms_cxt.SSRecoveryInfo.in_failover) {
        g_instance.dms_cxt.SSRecoveryInfo.in_failover = false;
        if (SS_REFORM_REFORMER) {
            ereport(LOG, (errmodule(MOD_DMS), errmsg("[SS failover] failover success, instance:%d become primary.",
                g_instance.attr.attr_storage.dms_attr.instance_id)));
        }
    }
    /* SSClusterState and in_reform must be set atomically */
    g_instance.dms_cxt.SSClusterState = NODESTATE_NORMAL;
    g_instance.dms_cxt.SSReformInfo.in_reform = false;
    g_instance.dms_cxt.SSRecoveryInfo.startup_reform = false;
    g_instance.dms_cxt.SSRecoveryInfo.restart_failover_flag = false;
    ereport(LOG,
            (errmodule(MOD_DMS),
                errmsg("[SS reform/SS switchover/SS failover] Reform success, instance:%d is running.",
                       g_instance.attr.attr_storage.dms_attr.instance_id)));

    /* reform success indicates that reform of primary and standby all complete, then update gaussdb.state */
    SendPostmasterSignal(PMSIGNAL_DMS_REFORM_DONE);
    return GS_SUCCESS;
}

static int CBXLogWaitFlush(void *db_handle, unsigned long long lsn)
{
    XLogWaitFlush(lsn);
    return GS_SUCCESS;
}

void DmsCallbackThreadShmemInit(unsigned char need_startup, char **reg_data)
{
    IsUnderPostmaster = true;
    // to add cnt, avoid postmain execute proc_exit to free shmem now
    (void)pg_atomic_add_fetch_u32(&g_instance.dms_cxt.inDmsThreShmemInitCnt, 1);

    // postmain execute proc_exit now, share mem maybe shdmt, exit this thread now.
    if (pg_atomic_read_u32(&g_instance.dms_cxt.inProcExitCnt) > 0) {
        (void)pg_atomic_sub_fetch_u32(&g_instance.dms_cxt.inDmsThreShmemInitCnt, 1);
        ThreadExitCXX(0);
    }
    EarlyBindingTLSVariables();
    MemoryContextInit();
    knl_thread_init(DMS_WORKER);
    *reg_data = (char *)&t_thrd;
    t_thrd.fake_session = create_session_context(t_thrd.top_mem_cxt, 0);
    t_thrd.fake_session->status = KNL_SESS_FAKE;
    u_sess = t_thrd.fake_session;
    t_thrd.proc_cxt.MyProcPid = gs_thread_self();
    if (!need_startup) {
        t_thrd.proc_cxt.MyProgName = "DMS WORKER";
        t_thrd.dms_cxt.is_reform_proc = false;
    } else {
        t_thrd.proc_cxt.MyProgName = "DMS REFORM PROC";
        t_thrd.dms_cxt.is_reform_proc = true;
    }
    t_thrd.proc_cxt.MyStartTime = time(NULL);

    SelfMemoryContext = THREAD_GET_MEM_CXT_GROUP(MEMORY_CONTEXT_DEFAULT);
    /* memory context will be used by DMS message process functions */
    t_thrd.dms_cxt.msgContext = AllocSetContextCreate(TopMemoryContext,
        "DMSWorkerContext",
        ALLOCSET_DEFAULT_MINSIZE,
        ALLOCSET_DEFAULT_INITSIZE,
        ALLOCSET_DEFAULT_MAXSIZE);
    /* create timer with thread safe */
    if (gs_signal_createtimer() < 0) {
        ereport(FATAL, (errmsg("create timer fail at thread : %lu", t_thrd.proc_cxt.MyProcPid)));
    }
    CreateLocalSysDBCache();
    InitShmemForDmsCallBack();
    Assert(t_thrd.utils_cxt.CurrentResourceOwner == NULL);
    t_thrd.utils_cxt.CurrentResourceOwner =
        ResourceOwnerCreate(NULL, "dms worker", THREAD_GET_MEM_CXT_GROUP(MEMORY_CONTEXT_STORAGE));
    SharedInvalBackendInit(false, false);
    pgstat_initialize();
    u_sess->attr.attr_common.Log_line_prefix = "\%m \%u \%d \%h \%p \%S ";
    log_timezone = g_instance.dms_cxt.log_timezone;
    (void)pg_atomic_sub_fetch_u32(&g_instance.dms_cxt.inDmsThreShmemInitCnt, 1);
    t_thrd.postgres_cxt.whereToSendOutput = (int)DestNone;
}

void DmsInitCallback(dms_callback_t *callback)
{
    // used in reform
    callback->get_list_stable = CBGetStableList;
    callback->save_list_stable = CBSaveStableList;
    callback->opengauss_startup = CBStartup;
    callback->opengauss_recovery_standby = CBRecoveryStandby;
    callback->opengauss_recovery_primary = CBRecoveryPrimary;
    callback->get_dms_status = CBGetDmsStatus;
    callback->set_dms_status = CBSetDmsStatus;
    callback->dms_reform_rebuild_buf_res = CBDrcBufRebuild;
    callback->dms_thread_init = DmsCallbackThreadShmemInit;
    callback->confirm_owner = CBConfirmOwner;
    callback->confirm_converting = CBConfirmConverting;
    callback->flush_copy = CBFlushCopy;
    callback->get_db_primary_id = CBGetDBPrimaryId;
    callback->failover_promote_opengauss = CBFailoverPromote;
    callback->reform_start_notify = CBReformStartNotify;
    callback->reform_set_dms_role = CBReformSetDmsRole;

    callback->get_page_hash_val = CBPageHashCode;
    callback->read_local_page4transfer = CBEnterLocalPage;
    callback->leave_local_page = CBLeaveLocalPage;
    callback->page_is_dirty = CBPageDirty;
    callback->get_page = CBGetPage;
    callback->set_buf_load_status = CBSetBufLoadStatus;
    callback->remove_buf_load_status = CBRemoveBufLoadStatus;
    callback->invld_share_copy = CBInvalidatePage;
    callback->get_db_handle = CBGetHandle;
    callback->display_pageid = CBDisplayBufferTag;
    callback->verify_page = CBVerifyPage;

    callback->mem_alloc = CBMemAlloc;
    callback->mem_free = CBMemFree;
    callback->mem_reset = CBMemReset;

    callback->get_page_lsn = CBGetPageLSN;
    callback->get_global_lsn = CBGetGlobalLSN;
    callback->log_flush = CBXLogFlush;
    callback->process_broadcast = CBProcessBroadcast;
    callback->process_broadcast_ack = CBProcessBroadcastAck;

    callback->get_opengauss_xid_csn = CBGetTxnCSN;
    callback->get_opengauss_update_xid = CBGetUpdateXid;
    callback->get_opengauss_txn_status = CBGetTxnStatus;
    callback->opengauss_lock_buffer = CBGetCurrModeAndLockBuffer;
    callback->get_opengauss_txn_snapshot = CBGetSnapshotData;

    callback->log_output = NULL;

    callback->switchover_demote = CBSwitchoverDemote;
    callback->switchover_promote_opengauss = CBSwitchoverPromote;
    callback->set_switchover_result = CBSwitchoverResult;
    callback->set_db_standby = CBSetDbStandby;
    callback->db_is_primary = CBDbIsPrimary;
    callback->reform_done_notify = CBReformDoneNotify;
    callback->log_wait_flush = CBXLogWaitFlush;
    callback->drc_validate = CBDrcBufValidate;
}
