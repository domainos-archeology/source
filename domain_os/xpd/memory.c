/*
 * xpd/memory.c - target memory access
 *
 *   XPD_$COPY_MEMORY      0x00E5B704  394 bytes
 *   XPD_$READ_PROC_ASYNC  0x00E5B88E  198 bytes
 *   XPD_$READ_PROC        0x00E5B954  142 bytes
 *   XPD_$WRITE_PROC       0x00E5B9E2  142 bytes
 *   XPD_$READ             0x00E5BA70   54 bytes
 *   XPD_$WRITE            0x00E5BAA6   54 bytes
 *
 * The four entry points that take an address space by index set A5 to
 * 0x00E81814 and never use it.
 */

#include "xpd/xpd_internal.h"

/* XPD_$COPY_MEMORY's bounce buffer (A6-0x418, 0x400 bytes) */
#define XPD_COPY_CHUNK  0x400

/* The value FIM_$TRACE_STS is left holding for both address spaces
 * (0x00E5B856 `clr.l (0,A0,D0w)` then `ori.w #0x80,(0,A0,D0w)`: the word
 * at the SAME address is the big-endian HIGH word of the longword, so the
 * bit set is bit 23 - the marker bit XPD_$RESTART sets in the fault
 * parameter and XPD_$GET_REGISTERS strips). */
#define XPD_TRACE_STS_DONE  0x00800000

/*
 * XPD_$COPY_MEMORY - copy between address spaces through a 1K buffer
 *
 * A cleanup handler (FIM_$CLEANUP) guards the copy: on the first return it
 * reports status_$fault_cleanup_in_progress and the copy runs; if a fault
 * unwinds to it, it returns that fault's status and the copy is skipped
 * (FIM_$POP_SIGNAL instead of FIM_$RLS_CLEANUP).  Each chunk is read with
 * the source AS selected and written with the destination AS selected;
 * a guard fault in either (FIM_$TRACE_STS == status_$mst_guard_fault)
 * ends the copy with that status.  Both AS's trace status is then set to
 * 0x80 and the caller's AS re-selected if it changed.
 *
 * Frame (link.w A6,-0x42c; A4 A3 A2 D7 D6 D5 D4 D3 D2 saved):
 *   A6-0x42C  4  buf_ptr      the bounce buffer's address
 *   A6-0x428  2  saved_asid   PROC1_$AS_ID at entry
 *   A6-0x426  2  cur_asid     the AS last selected
 *   A6-0x424  4  remaining
 *   A6-0x418 400 buf
 *   A6-0x018     cleanup      FIM_$CLEANUP's record
 *   D4 dst, D5 src (advanced per chunk)
 */
void XPD_$COPY_MEMORY(int16_t dst_asid, void *dst_addr, int16_t src_asid,
                      const void *src_addr, uint32_t len,
                      status_$t *status_ret)
{
    uint16_t saved_asid;                /* A6-0x428 */
    uint16_t cur_asid;                  /* A6-0x426 */
    uint32_t remaining;                 /* A6-0x424 */
    uint8_t buf[XPD_COPY_CHUNK];        /* A6-0x418 */
    uint8_t cleanup[0x18];              /* A6-0x18 */
    uint8_t *dst;                       /* D4 */
    const uint8_t *src;                 /* D5 */
    status_$t st;                       /* D0 */
    uint32_t chunk;                     /* D0 */

    /* 0x00E5B70C-0x00E5B71C */
    remaining = len;
    saved_asid = PROC1_$AS_ID;
    cur_asid = saved_asid;

    /* 0x00E5B722-0x00E5B73A */
    st = FIM_$CLEANUP(cleanup);
    *status_ret = st;
    if (st == status_$fault_cleanup_in_progress) {
        /* 0x00E5B73E-0x00E5B76A */
        *status_ret = status_$ok;
        FIM_$WIRED_DATA.trace_sts[(uint16_t)src_asid] = 0;
        FIM_$WIRED_DATA.trace_sts[(uint16_t)dst_asid] = 0;
        dst = (uint8_t *)dst_addr;
        src = (const uint8_t *)src_addr;

        /* 0x00E5B82A-0x00E5B82E / 0x00E5B76E-0x00E5B828 */
        while (remaining != 0) {
            /* read a chunk from the source AS */
            PROC1_$SET_ASID((uint16_t)src_asid);
            cur_asid = (uint16_t)src_asid;
            chunk = XPD_COPY_CHUNK;
            if (!(chunk <= remaining)) {
                chunk = remaining;
            }
            OS_$DATA_COPY(src, buf, chunk);
            if (FIM_$WIRED_DATA.trace_sts[(uint16_t)src_asid] == status_$mst_guard_fault) {
                *status_ret = status_$mst_guard_fault;      /* 0x00E5B822 */
                break;
            }
            /* write it to the destination AS */
            PROC1_$SET_ASID((uint16_t)dst_asid);
            cur_asid = (uint16_t)dst_asid;
            if (!(remaining <= XPD_COPY_CHUNK)) {
                OS_$DATA_COPY(buf, dst, XPD_COPY_CHUNK);
                remaining -= XPD_COPY_CHUNK;
                dst += XPD_COPY_CHUNK;
                src += XPD_COPY_CHUNK;
            } else {
                OS_$DATA_COPY(buf, dst, remaining);
                remaining = 0;
            }
            if (FIM_$WIRED_DATA.trace_sts[(uint16_t)dst_asid] == status_$mst_guard_fault) {
                *status_ret = status_$mst_guard_fault;      /* 0x00E5B822 */
                break;
            }
        }
        /* 0x00E5B832-0x00E5B83C */
        FIM_$RLS_CLEANUP(cleanup);
    } else {
        /* 0x00E5B83E-0x00E5B842: a fault unwound here */
        FIM_$POP_SIGNAL(cleanup);
    }

    /* 0x00E5B84A-0x00E5B86A */
    FIM_$WIRED_DATA.trace_sts[(uint16_t)src_asid] = XPD_TRACE_STS_DONE;
    FIM_$WIRED_DATA.trace_sts[(uint16_t)dst_asid] = XPD_TRACE_STS_DONE;

    /* 0x00E5B870-0x00E5B87E */
    if (saved_asid != cur_asid) {
        PROC1_$SET_ASID(saved_asid);
    }
}

/*
 * XPD_$READ_PROC_ASYNC - read a target without it being stopped
 *
 * The target is found with PROC2_$FIND_INDEX under lock 4; the caller must
 * be its debugger or (ACL_$CHECK_DEBUG_RIGHTS on the two PROC1 pids) hold
 * debug rights over it, else proc2_no_debug_rights.
 *
 * Frame (link.w A6,-0x14; A5 A2 D2 saved): A6-0x0C status, A6-0x08 uid copy.
 */
void XPD_$READ_PROC_ASYNC(uid_t *proc_uid, void *addr, int32_t *len,
                          void *buffer, status_$t *status_ret)
{
    status_$t st;                       /* A6-0x0C */
    uid_t uid;                          /* A6-0x08 */
    int16_t idx;                        /* D2 */
    proc2_info_t *entry;                /* A2 */

    /* 0x00E5B89C-0x00E5B8D4 */
    uid = *proc_uid;
    ML_$LOCK(PROC2_LOCK_ID);
    idx = PROC2_$FIND_INDEX(&uid, &st);
    ML_$UNLOCK(PROC2_LOCK_ID);

    if (st == status_$ok) {
        /* 0x00E5B8DC-0x00E5B916 */
        entry = XPD_ENTRY(idx);
        if (entry->debugger_idx == XPD_CURRENT_INDEX() ||
            ACL_$CHECK_DEBUG_RIGHTS((int16_t *)&PROC1_$CURRENT,
                                    (int16_t *)&entry->level1_pid) < 0) {
            /* 0x00E5B918-0x00E5B934 */
            XPD_$COPY_MEMORY((int16_t)PROC1_$AS_ID, buffer,
                             (int16_t)entry->asid, addr, (uint32_t)*len, &st);
        } else {
            st = status_$proc2_permission_denied;         /* 0x00E5B93A */
        }
    }

    /* 0x00E5B942-0x00E5B946 */
    *status_ret = st;
}

/*
 * XPD_$READ_PROC - read a stopped target's memory into the caller's
 */
void XPD_$READ_PROC(uid_t *proc_uid, void *addr, int32_t *len, void *buffer,
                    status_$t *status_ret)
{
    status_$t st;                       /* A6-0x0C */
    uid_t uid;                          /* A6-0x08 */
    int16_t idx;                        /* D2 */
    proc2_info_t *entry;                /* A2 */

    /* 0x00E5B962-0x00E5B998 */
    uid = *proc_uid;
    ML_$LOCK(PROC2_LOCK_ID);
    idx = XPD_$FIND_INDEX(&uid, &st);
    ML_$UNLOCK(PROC2_LOCK_ID);

    if (st == status_$ok) {
        /* 0x00E5B9A0-0x00E5B9CC: (dst AS_ID, buffer) <- (target, addr) */
        entry = XPD_ENTRY(idx);
        XPD_$COPY_MEMORY((int16_t)PROC1_$AS_ID, buffer, (int16_t)entry->asid,
                         addr, (uint32_t)*len, &st);
    }

    /* 0x00E5B9D0-0x00E5B9D4 */
    *status_ret = st;
}

/*
 * XPD_$WRITE_PROC - write the caller's memory into a stopped target's
 */
void XPD_$WRITE_PROC(uid_t *proc_uid, void *addr, const int32_t *len,
                     const void *buffer, status_$t *status_ret)
{
    status_$t st;                       /* A6-0x0C */
    uid_t uid;                          /* A6-0x08 */
    int16_t idx;                        /* D2 */
    proc2_info_t *entry;                /* A2 */

    /* 0x00E5B9F0-0x00E5BA26 */
    uid = *proc_uid;
    ML_$LOCK(PROC2_LOCK_ID);
    idx = XPD_$FIND_INDEX(&uid, &st);
    ML_$UNLOCK(PROC2_LOCK_ID);

    if (st == status_$ok) {
        /* 0x00E5BA2E-0x00E5BA5A: (target, addr) <- (AS_ID, buffer) */
        entry = XPD_ENTRY(idx);
        XPD_$COPY_MEMORY((int16_t)entry->asid, addr, (int16_t)PROC1_$AS_ID,
                         buffer, (uint32_t)*len, &st);
    }

    /* 0x00E5BA5E-0x00E5BA62 */
    *status_ret = st;
}

/*
 * XPD_$READ - read from an address space given by index
 */
void XPD_$READ(uint16_t *asid, void *addr, int32_t *len, void *buffer,
               status_$t *status_ret)
{
    /* 0x00E5BA7C-0x00E5BA9A */
    XPD_$COPY_MEMORY((int16_t)PROC1_$AS_ID, buffer, (int16_t)*asid, addr,
                     (uint32_t)*len, status_ret);
}

/*
 * XPD_$WRITE - write to an address space given by index
 */
void XPD_$WRITE(uint16_t *asid, void *addr, const int32_t *len,
                const void *buffer, status_$t *status_ret)
{
    /* 0x00E5BAB2-0x00E5BAD0 */
    XPD_$COPY_MEMORY((int16_t)*asid, addr, (int16_t)PROC1_$AS_ID, buffer,
                     (uint32_t)*len, status_ret);
}
