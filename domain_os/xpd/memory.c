/*
 * XPD Memory Access Functions
 *
 * These functions provide memory read/write operations between
 * address spaces for debugging purposes.
 *
 * Original addresses:
 *   XPD_$COPY_MEMORY:      0x00e5b704
 *   XPD_$READ_PROC:        0x00e5b954
 *   XPD_$READ_PROC_ASYNC:  0x00e5b88e
 *   XPD_$WRITE_PROC:       0x00e5b9e2
 *   XPD_$READ:             0x00e5ba70
 *   XPD_$WRITE:            0x00e5baa6
 */

#include "xpd/xpd.h"
#include "fim/fim.h"
#include "acl/acl.h"

/* FIM trace status array base */
#define FIM_TRACE_STS_BASE  0xE223A2

/* Process table offsets */
#define PROC_TABLE_BASE     0xEA551C
#define PROC_ENTRY_SIZE     0xE4

/* Process entry field offset for trace ASID */
#define TRACE_ASID_OFFSET   (-0x4E)

/* Current process index offset */
#define CURRENT_TO_INDEX_OFFSET 0xEA93D2

/* Copy buffer size for inter-address-space transfers */
#define COPY_BUFFER_SIZE    0x400   /* 1KB */

/*
 * XPD_$COPY_MEMORY - Copy memory between address spaces
 *
 * This function copies data between two address spaces using a
 * temporary buffer. It handles guard faults and switches ASIDs
 * as needed to perform the copy.
 *
 * The copy is done in chunks of up to 1KB to limit the amount
 * of time spent in a foreign address space.
 */
void XPD_$COPY_MEMORY(int16_t dst_asid, void *dst_addr, int16_t src_asid,
                      void *src_addr, uint32_t len, status_$t *status_ret)
{
    uint16_t saved_asid;
    uint16_t current_asid;
    uint32_t remaining;
    uint32_t chunk_size;
    char *src_ptr;
    char *dst_ptr;
    char copy_buffer[COPY_BUFFER_SIZE];
    uint8_t cleanup_state[24];
    status_$t cleanup_status;

    /* Save the current ASID */
    saved_asid = PROC1_$AS_ID;
    current_asid = saved_asid;
    remaining = len;

    /* Set up cleanup handler for fault recovery */
    cleanup_status = FIM_$CLEANUP(cleanup_state);
    *status_ret = cleanup_status;

    if (cleanup_status != status_$cleanup_handler_set) {
        /* Fault occurred or couldn't set handler */
        FIM_$POP_SIGNAL(cleanup_state);
        goto cleanup_and_exit;
    }

    *status_ret = status_$ok;

    /* Clear trace status for both ASIDs */
    *(uint32_t *)(FIM_TRACE_STS_BASE + (src_asid << 2)) = 0;
    *(uint32_t *)(FIM_TRACE_STS_BASE + (dst_asid << 2)) = 0;

    src_ptr = (char *)src_addr;
    dst_ptr = (char *)dst_addr;

    while (remaining > 0) {
        /* Switch to source address space */
        PROC1_$SET_ASID(src_asid);
        current_asid = src_asid;

        /* Calculate chunk size */
        chunk_size = COPY_BUFFER_SIZE;
        if (remaining < COPY_BUFFER_SIZE) {
            chunk_size = remaining;
        }

        /* Copy from source to buffer */
        OS_$DATA_COPY(src_ptr, copy_buffer, chunk_size);

        /* Check for guard fault */
        if (*(int32_t *)(FIM_TRACE_STS_BASE + (src_asid << 2)) == status_$mst_guard_fault) {
            *status_ret = status_$mst_guard_fault;
            break;
        }

        /* Switch to destination address space */
        PROC1_$SET_ASID(dst_asid);
        current_asid = dst_asid;

        /* Copy from buffer to destination */
        if (remaining <= COPY_BUFFER_SIZE) {
            OS_$DATA_COPY(copy_buffer, dst_ptr, remaining);
            remaining = 0;
        } else {
            OS_$DATA_COPY(copy_buffer, dst_ptr, COPY_BUFFER_SIZE);
            remaining -= COPY_BUFFER_SIZE;
            src_ptr += COPY_BUFFER_SIZE;
            dst_ptr += COPY_BUFFER_SIZE;
        }

        /* Check for guard fault */
        if (*(int32_t *)(FIM_TRACE_STS_BASE + (dst_asid << 2)) == status_$mst_guard_fault) {
            *status_ret = status_$mst_guard_fault;
            break;
        }
    }

    /* Release cleanup handler */
    FIM_$RLS_CLEANUP(cleanup_state);

cleanup_and_exit:
    /* Clear and mark trace status as handled for both ASIDs */
    *(uint32_t *)(FIM_TRACE_STS_BASE + (src_asid << 2)) = 0;
    *(uint16_t *)(FIM_TRACE_STS_BASE + (src_asid << 2)) |= 0x80;

    *(uint32_t *)(FIM_TRACE_STS_BASE + (dst_asid << 2)) = 0;
    *(uint16_t *)(FIM_TRACE_STS_BASE + (dst_asid << 2)) |= 0x80;

    /* Restore original ASID if we changed it */
    if (saved_asid != current_asid) {
        PROC1_$SET_ASID(saved_asid);
    }
}

/*
 * XPD_$READ_PROC - Read from debug target's memory
 *
 * Reads memory from a suspended debug target. The caller must be
 * the debugger for the target process.
 */
void XPD_$READ_PROC(uid_t *proc_uid, void *addr, int32_t *len, void *buffer, status_$t *status_ret)
{
    int16_t index;
    int32_t proc_offset;
    int16_t target_trace_asid;
    status_$t status;
    uid_t local_uid;

    local_uid = *proc_uid;

    ML_$LOCK(PROC2_LOCK_ID);
    index = XPD_$FIND_INDEX(&local_uid, &status);
    ML_$UNLOCK(PROC2_LOCK_ID);

    if (status == status_$ok) {
        proc_offset = index * PROC_ENTRY_SIZE;

        /* Get target's trace ASID */
        target_trace_asid = *(int16_t *)(PROC_TABLE_BASE + proc_offset + TRACE_ASID_OFFSET);

        /* Copy from target address space to caller's buffer */
        XPD_$COPY_MEMORY(PROC1_$AS_ID, buffer, target_trace_asid, addr, *len, &status);
    }

    *status_ret = status;
}

/*
 * XPD_$READ_PROC_ASYNC - Read from target with permission check
 *
 * Like READ_PROC but checks debug permissions when the caller
 * is not the debugger.
 */
void XPD_$READ_PROC_ASYNC(uid_t *proc_uid, void *addr, int32_t *len,
                          void *buffer, status_$t *status_ret)
{
    int16_t index;
    int32_t proc_offset;
    int16_t target_trace_asid;
    int16_t debugger_idx;
    int16_t current_idx;
    int8_t has_rights;
    status_$t status;
    uid_t local_uid;

    local_uid = *proc_uid;

    ML_$LOCK(PROC2_LOCK_ID);
    index = PROC2_$FIND_INDEX(&local_uid, &status);
    ML_$UNLOCK(PROC2_LOCK_ID);

    if (status == status_$ok) {
        proc_offset = index * PROC_ENTRY_SIZE;

        /* Get target's debugger index */
        debugger_idx = *(int16_t *)(PROC_TABLE_BASE + proc_offset - 0xBE);

        /* Get current process index */
        current_idx = *(int16_t *)(CURRENT_TO_INDEX_OFFSET + (PROC1_$CURRENT * 2));

        /* Check if we're the debugger or have debug rights */
        if (debugger_idx != current_idx) {
            has_rights = ACL_$CHECK_DEBUG_RIGHTS(&PROC1_$CURRENT, proc_offset + PROC_TABLE_BASE - 0x4A);
            if (has_rights >= 0) {
                *status_ret = status_$proc2_permission_denied;
                return;
            }
        }

        /* Get target's trace ASID */
        target_trace_asid = *(int16_t *)(PROC_TABLE_BASE + proc_offset + TRACE_ASID_OFFSET);

        /* Copy from target address space to caller's buffer */
        XPD_$COPY_MEMORY(PROC1_$AS_ID, buffer, target_trace_asid, addr, *len, &status);
    }

    *status_ret = status;
}

/*
 * XPD_$WRITE_PROC - Write to debug target's memory
 *
 * Writes memory to a suspended debug target. The caller must be
 * the debugger for the target process.
 */
void XPD_$WRITE_PROC(uid_t *proc_uid, void *addr, int32_t *data, void *buffer, status_$t *status_ret)
{
    int16_t index;
    int32_t proc_offset;
    int16_t target_trace_asid;
    status_$t status;
    uid_t local_uid;

    local_uid = *proc_uid;

    ML_$LOCK(PROC2_LOCK_ID);
    index = XPD_$FIND_INDEX(&local_uid, &status);
    ML_$UNLOCK(PROC2_LOCK_ID);

    if (status == status_$ok) {
        proc_offset = index * PROC_ENTRY_SIZE;

        /* Get target's trace ASID */
        target_trace_asid = *(int16_t *)(PROC_TABLE_BASE + proc_offset + TRACE_ASID_OFFSET);

        /* Copy from caller's buffer to target address space */
        XPD_$COPY_MEMORY(target_trace_asid, addr, PROC1_$AS_ID, buffer, *data, &status);
    }

    *status_ret = status;
}

/*
 * XPD_$READ - Read from address space by ASID
 *
 * A lower-level interface that reads directly from an address
 * space given its ASID, without process validation.
 */
void XPD_$READ(uint16_t *asid, void *addr, int32_t *len, void *buffer, status_$t *status_ret)
{
    /* Copy from specified ASID to current process's buffer */
    XPD_$COPY_MEMORY(PROC1_$AS_ID, buffer, *asid, addr, *len, status_ret);
}

/*
 * XPD_$WRITE - Write to address space by ASID
 *
 * A lower-level interface that writes directly to an address
 * space given its ASID, without process validation.
 */
void XPD_$WRITE(uint16_t *asid, void *addr, int32_t *len, void *buffer, status_$t *status_ret)
{
    /* Copy from current process's buffer to specified ASID */
    XPD_$COPY_MEMORY(*asid, addr, PROC1_$AS_ID, buffer, *len, status_ret);
}
