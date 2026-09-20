/*
 * DISK_$INIT - Initialise the disk module's eventcounts and locks
 *
 * 0x00E3C0EA - 0x00E3C14A (98 bytes, A5 = DISK_$DATA at 0xE7A1CC).
 * Verified against the disassembly on 2026-09-19; the earlier emission
 * was faithful but called the 0x1C-byte slots "volumes" - they are the
 * per-process I/O slots (disk_$per_proc_t in disk/disk.h).
 *
 * EC_$INIT on the module eventcount at A5+0 (0x00E3C0F8), then on the
 * io_ec (+0x378) and err_ec (+0x384) of slots 1..64 (`moveq #0x3f` / dbf,
 * A2 starting at A5+0x1c and stepping 0x1c: 0x00E3C102 - 0x00E3C128),
 * then ML_$EXCLUSION_INIT on the locks at A5+0xa8 and A5+0x90.
 */

#include "disk/disk_internal.h"
#include "ec/ec.h"
#include "ml/ml.h"

void DISK_$INIT(void)
{
    int16_t pid;

    /* 0x00E3C0F8 - 0x00E3C100 */
    EC_$INIT((ec_$eventcount_t *)&DISK_$DATA[DMOD_EVENTCOUNT]);

    /* 0x00E3C102 - 0x00E3C128: slots 1..64 */
    for (pid = 1; pid <= 64; pid++) {
        uint8_t *slot = DISK_$DATA + pid * DMOD_PER_PROC_SIZE;
        EC_$INIT((ec_$eventcount_t *)(slot + DMOD_PER_PROC_IO_EC));
        EC_$INIT((ec_$eventcount_t *)(slot + DMOD_PER_PROC_ERR_EC));
    }

    /* 0x00E3C12C - 0x00E3C13C */
    ML_$EXCLUSION_INIT(&ml_$exclusion_t_00e7a274);
    ML_$EXCLUSION_INIT(&ml_$exclusion_t_00e7a25c);
}
