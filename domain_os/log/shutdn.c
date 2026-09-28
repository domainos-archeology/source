/*
 * LOG_$SHUTDN - Close the system log
 *
 * Re-emitted from the image (0x00E1758C..0x00E17602, 120 bytes; the batch
 * address 0x00E175FC is its epilogue label).  A5 = 0xE2B280 = LOG_$STATE.
 *
 *   00e17598  tst.l (0x14,A5) / beq exit             ; LOG_$LOGFILE_PTR
 *   00e1759e  clr.w ; pea 0x00E17608 ; move.w #4     ; LOG_$ADD(4, &cell, 0)
 *   00e175ae  A6-0x4 = LOG_$LOGFILE_PTR ; clr.l it
 *   00e175b8  WP_$UNWIRE((0x10,A5))                  ; wired_handle, by value
 *   00e175c4  MST_$UNMAP_PRIVI(1, &UID_$NIL, saved ptr, 0x400, 0, &status)
 *             (`pea (0x400).w` pushes the VALUE 0x400)
 *   00e175e6  FILE_$UNLOCK(A5, &0x00E17604, &status)  ; &logfile_uid (+0)
 *   00e175f6  clr.l (0x00e0000c).l
 *
 * Constant cells after the function (`gsk read 0x00e17604 8`):
 *   0x00E17604  00 04        word 4    -- FILE_$UNLOCK's lock mode
 *   0x00E17606  20 48        padding
 *   0x00E17608  00 00 00 00  longword  -- LOG_$ADD's (empty) data
 * The old body passed lock mode 0 and a C "" literal; both were invented.
 *
 * Original address: 0x00e1758c
 */

#include "log/log_internal.h"
#include "file/file.h"
#include "wp/wp.h"
#include "mst/mst.h"

static const uint16_t log_$shutdn_lock_mode_00e17604 = 4;
static const uint32_t log_$shutdn_data_00e17608 = 0;

void LOG_$SHUTDN(void)
{
    status_$t status;            /* A6-0x8 */
    int16_t *saved_ptr;          /* A6-0x4 */

    /* 0x00E17598: tst.l / beq */
    if (LOG_$LOGFILE_PTR == NULL) {
        return;
    }

    /* 0x00E1759E-0x00E175AC */
    LOG_$ADD(LOG_TYPE_SHUTDOWN, (void *)&log_$shutdn_data_00e17608, 0);

    /* 0x00E175AE-0x00E175B4 */
    saved_ptr = LOG_$LOGFILE_PTR;
    LOG_$LOGFILE_PTR = NULL;

    /* 0x00E175B8-0x00E175C2 */
    WP_$UNWIRE(LOG_$STATE.wired_handle);

    /* 0x00E175C4-0x00E175E2 */
    MST_$UNMAP_PRIVI(1, &UID_$NIL, ARCH_PTR_TO_VA(saved_ptr), LOG_BUFFER_SIZE, 0, &status);

    /* 0x00E175E6-0x00E175F0 */
    FILE_$UNLOCK(&LOG_$LOGFILE_UID, (uint16_t *)&log_$shutdn_lock_mode_00e17604, &status);

    /* 0x00E175F6 */
    LOG_$LAST_ENTRY.magic = 0;
}
