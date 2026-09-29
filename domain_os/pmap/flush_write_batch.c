/*
 * pmap_$flush_write_batch - Batch write dirty pages to disk
 *
 * Nested Pascal procedure from PMAP_$FLUSH, flattened into a C function
 * with explicit parameters (originally accessed parent frame via A6 chain).
 *
 * Algorithm:
 *   1. Unlock PMAP lock (allow other processes during I/O)
 *   2. Allocate disk queue blocks via DISK_$GET_QBLKS
 *   3. Fill queue blocks with write descriptors via pmap_$fill_write_qblks
 *   4. Execute batch write via DISK_$WRITE_MULTI
 *   5. If write fails: CRASH_SYSTEM (unrecoverable)
 *   6. Re-acquire PMAP lock
 *   7. For each written page:
 *      a. Call pmap_$write_complete to update page frame state
 *      b. On success (status == 0):
 *         - Increment per-process write counter
 *         - Log via NETLOG_$LOG_IT if enabled
 *         - Call pmap_$update_seg_map to update segment map
 *      c. On error (status != 0 and != -1 sentinel):
 *         - Propagate error to caller's status output
 *   8. Advance AST_$PMAP_IN_TRANS_EC event count
 *   9. Return queue blocks via DISK_$RTN_QBLKS
 *  10. Clear batch count
 *
 * Original m68k parent frame layout (A4 = parent's A6):
 *   A4-0x50: batch_count (int16_t)
 *   A4-0x40: batch_vpns array base
 *   A4+0x0C: segmap pointer (PMAP_$FLUSH parameter)
 *   A4+0x16: status pointer (PMAP_$FLUSH parameter)
 *
 * A4 is also handed on unchanged (in A1) to pmap_$update_seg_map at
 * 0x00E13718, which uses it to read PMAP_$FLUSH's `aste` (A4+0x08) and
 * `flags` (the low byte of the word at A4+0x14).  Those two are therefore
 * extra explicit parameters here.
 *
 * Original address: 0x00E1360C
 * Size: 352 bytes
 */

#include "pmap/pmap_internal.h"
#include "ast/ast.h"
#include "netlog/netlog.h"
#include "misc/misc.h"

/*
 * Disk queue block field offsets (byte offsets within qblk structure).
 *
 * The full qblk structure is defined by the DISK subsystem. These
 * offsets are derived from assembly analysis of pmap_$fill_write_qblks
 * (which populates the blocks) and DISK_$WRITE_MULTI (which writes
 * status results).
 *
 * After DISK_$WRITE_MULTI completes, the result chain is traversed
 * via the QBLK_NEXT pointer (offset 0x08), which differs from the
 * allocation chain pointer at offset 0x00 used by fill_write_qblks.
 */

/*
 * DISK_QBLK_RAW_TO_PTR - Convert int32_t raw qblk handle to pointer.
 *
 * On m68k (32-bit), DISK_$GET_QBLKS returns pointers as int32_t values
 * which fit exactly. On 64-bit test hosts, int32_t truncates the pointer.
 * Tests override this macro to supply the full-width pointer directly.
 */
#ifndef DISK_QBLK_RAW_TO_PTR
#define DISK_QBLK_RAW_TO_PTR(raw) ((uint8_t *)(uintptr_t)(raw))
#endif

#define QBLK_NEXT_OFFSET        0x08    /* Next qblk in result chain (pointer) */
#define QBLK_STATUS_OFFSET      0x0C    /* Write status result (int32_t) */
#define QBLK_VPN_OFFSET         0x14    /* Virtual page number (int32_t) */
#define QBLK_UID_OFFSET         0x20    /* Object UID (8 bytes, for NETLOG) */
#define QBLK_BLKINSEG_OFFSET    0x28    /* Block-in-segment (uint32_t) */

/*
 * QBLK_GET_NEXT - Read next-qblk pointer from result chain.
 *
 * On m68k (32-bit), the 4-byte pointer at offset 0x08 doesn't overlap
 * the status field at 0x0C. On 64-bit test hosts, an 8-byte pointer
 * at 0x08 overlaps 0x0C, corrupting the status field. Tests override
 * this macro to use a side table for chain links.
 */
#ifndef QBLK_GET_NEXT
#define QBLK_GET_NEXT(qblk) (*(uint8_t **)((qblk) + QBLK_NEXT_OFFSET))
#endif

void pmap_$flush_write_batch(int16_t *batch_count_p, uint32_t *batch_vpns,
                              uint32_t *segmap, status_$t *status,
                              aste_t *aste, uint16_t flags)
{
    /*
     * DISK_$GET_QBLKS returns queue block pointers.
     * On m68k (32-bit), pointers fit in int32_t/uint32_t.
     * For portability to wider architectures, we store the raw DISK API
     * return values and immediately convert to proper pointer types.
     */
    uint32_t qblk_head_raw;
    uint32_t qblk_tail_raw;
    uint8_t *qblk_head;
    status_$t write_status;

    /* Step 1: Unlock PMAP lock before disk I/O to allow other processes to run */
    ML_$UNLOCK(PMAP_LOCK_ID);

    /* Step 2: Allocate disk queue blocks for the batch */
    DISK_$GET_QBLKS(*batch_count_p, &qblk_head_raw, &qblk_tail_raw);
    qblk_head = DISK_QBLK_RAW_TO_PTR(qblk_head_raw);

    /* Step 3: Fill queue blocks with page write descriptors */
    pmap_$fill_write_qblks((int32_t *)batch_vpns,
                            (uint32_t *)qblk_head,
                            *batch_count_p);

    /* Step 4: Execute batch write to disk
     * flags = -1 (0xFF): synchronous write to all volumes */
    DISK_$WRITE_MULTI(-1, (void *)qblk_head, &write_status);

    /* Step 5: Crash if write failed - disk write errors during flush
     * are unrecoverable */
    if (write_status != 0) {
        CRASH_SYSTEM(&write_status);
    }

    /* Step 6: Re-acquire PMAP lock before modifying page state */
    ML_$LOCK(PMAP_LOCK_ID);

    /* Step 7: Process results for each written page.
     *
     * The result queue blocks are linked via offset 0x08 (different from
     * the allocation chain at offset 0x00). Each qblk contains the VPN
     * at offset 0x14 and the per-page write status at offset 0x0C.
     *
     * The loop mirrors the m68k assembly: D4 = batch_count-1 (dbf counter),
     * A2 = current qblk pointer traversing the result chain. */
    {
        int16_t remaining = *batch_count_p - 1;
        uint8_t *qblk = qblk_head;

        if (remaining >= 0) {
            do {
                int32_t vpn = *(int32_t *)(qblk + QBLK_VPN_OFFSET);
                int32_t *qblk_status_p = (int32_t *)(qblk + QBLK_STATUS_OFFSET);

                /* Call write completion handler to update page frame state */
                pmap_$write_complete(vpn, (void *)qblk_status_p);

                if (*qblk_status_p == 0) {
                    /* Write succeeded */

                    /* Increment per-process pages-written counter.
                     * PROC1_$DATA.stats[] is one 16-byte proc1_$stats_t per process.
                     * Index 2 (byte offset 8) is the pages-written counter.
                     * Original: addq.l #1, (-8, A3, D0w) where A3 = 0xE25D20,
                     *           D0 = PROC1_$CURRENT << 4 */
                    PROC1_$DATA.stats[PROC1_$CURRENT].stat[2]++;

                    /* Get page index within segment from MMAPE entry.
                     * Original: move.b (-0x1FFF, A1), D2b
                     *   where A1 = 0xEB4800 + vpn*16, so address = 0xEB2801 + vpn*16
                     *   = MMAPE_FOR_VPN(vpn)->seg_offset */
                    uint8_t page_idx = MMAPE_FOR_VPN((uint32_t)vpn)->seg_offset;

                    /* Log the write completion if NETLOG is enabled.
                     * NETLOG_$OK_TO_LOG < 0 means bit 7 is set (logging active).
                     * Original: tst.b NETLOG_$OK_TO_LOG; bpl skip */
                    if (NETLOG_$OK_TO_LOG < 0) {
                        NETLOG_$LOG_IT(
                            3,
                            (uint32_t *)(qblk + QBLK_UID_OFFSET),
                            (uint16_t)(*(uint32_t *)(qblk + QBLK_BLKINSEG_OFFSET) >> 5),
                            (uint16_t)page_idx,
                            (uint16_t)vpn,
                            0, 0, 0);
                    }

                    /*
                     * 0x00E13706-0x00E1371E: release the page or invalidate
                     * the remote mapping.  `segmap + page_idx` is the
                     * `pea (0x0,A1,D0w)` with D0 = page_idx << 2 at
                     * 0x00E13714 (segmap is uint32_t*).  0x00E13718 loads
                     * A1 with A4, this procedure's own static link, which
                     * pmap_$update_seg_map uses to reach PMAP_$FLUSH's
                     * `aste` and `flags` arguments -- forwarded explicitly
                     * here.
                     */
                    pmap_$update_seg_map(aste, flags,
                                         segmap + page_idx,
                                         (uint32_t)vpn,
                                         page_idx);

                } else if (*qblk_status_p != -1) {
                    /* Write returned error (not -1 sentinel) -
                     * propagate to caller's status output.
                     * Original: movea.l (0x16,A4),A0; move.l D0,(A0) */
                    *status = *qblk_status_p;
                }

                /* Advance to next queue block in result chain */
                remaining--;
                qblk = QBLK_GET_NEXT(qblk);
            } while (remaining != -1);
        }
    }

    /* Step 8: Advance in-transit event count to wake any waiters
     * (e.g., pmap_$wait_in_transit callers) */
    EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC);

    /* Step 9: Return queue blocks to disk subsystem */
    DISK_$RTN_QBLKS(*batch_count_p, qblk_head_raw, qblk_tail_raw);

    /* Step 10: Clear batch count.
     * Original: clr.w (-0x50,A4) */
    *batch_count_p = 0;
}
