/*
 * PMAP_$WS_SCAN_CALLBACK - Per-process timer callback: age its working set
 *
 * 0x00E144F6 - 0x00E145EA (246 bytes, A5 = 0xE24D44).  Re-emitted from the
 * disassembly 2026-09-27; the earlier emission was faithful but addressed
 * the WSL records through raw offsets.  The record is now ws_hdr_t and
 * the argument is the time queue's callback cell.
 *
 * Argument: (0x8,A6) -> a longword holding the address of a record whose
 * WORD at +2 is the process id (`movea.l (A0),A1 / move.w (0x2,A1),D2w`,
 * 0x00E14508).  A pid above 0x40 crashes with status_$t_00e145ec
 * (0x00050010, the cell at 0x00E145EC reached by `pea (0xd6,PC)`).
 *
 * Under the PMAP lock:
 *   - slot = MMAP_PID_TO_WSL[pid] (0xE23CA6 + pid * 2); if non-zero,
 *     ws->owner++ and once it reaches PMAP_$WS_INTERVAL: owner := 0,
 *     scan_pos := page_count, ws_timestamp := now, MMAP_$WS_SCAN(slot, 0,
 *     0x3FFFFF, 0x3FFFFF)
 *   - the wired pool (MMAP_WSL[5]) is aged the same way every 8 ticks:
 *     when ws_timestamp + 8 < now, both stamps := now, owner++, and at
 *     PMAP_$WS_INTERVAL owner := 0, scan_pos := page_count and
 *     MMAP_$WS_SCAN(5, 0, ...) (`move.l #0x50000` pushes the two words)
 */

#include "pmap/pmap_internal.h"
#include "misc/misc.h"
#include "mmap/mmap.h"
#include "time/time.h"

#define PMAP_WSC_MAX_PID        0x40        /* 0x00E1450E */
#define PMAP_WSC_SCAN_ALL       0x3FFFFF
#define PMAP_WSC_WIRED_PERIOD   8           /* 0x00E14598 addq.l #8 */

void PMAP_$WS_SCAN_CALLBACK(void *arg)
{
    time_$callback_arg_t cell = (time_$callback_arg_t)arg;
    const uint16_t *rec;
    uint16_t pid;               /* D2 */
    uint16_t slot;
    ws_hdr_t *ws;
    ws_hdr_t *wired;

    /* 0x00E14504 - 0x00E1451E */
    rec = (const uint16_t *)*cell;                    /* movea.l (A0),A1 */
    pid = rec[1];
    if (pid > PMAP_WSC_MAX_PID) {
        CRASH_SYSTEM(&status_$t_00e145ec);
    }

    /* 0x00E14520 */
    ML_$LOCK(PMAP_LOCK_ID);

    /* 0x00E1452E - 0x00E14584 */
    slot = MMAP_PID_TO_WSL[pid];
    if (slot != 0) {
        ws = &MMAP_WSL[slot];
        ws->owner++;
        if (ws->owner >= PMAP_$WS_INTERVAL) {           /* bcs 0x00E1455E */
            ws->owner = 0;
            ws->scan_pos = ws->page_count;
            ws->ws_timestamp = TIME_$CLOCKH;
            MMAP_$WS_SCAN(slot, 0, PMAP_WSC_SCAN_ALL, PMAP_WSC_SCAN_ALL);
        }
    }

    /* 0x00E14588 - 0x00E145D2: the wired pool's own clock (signed bge) */
    wired = &MMAP_WSL[MMAP_WSL_POOL_WIRED];
    if ((int32_t)(wired->ws_timestamp + PMAP_WSC_WIRED_PERIOD) < (int32_t)TIME_$CLOCKH) {
        wired->pri_timestamp = TIME_$CLOCKH;
        wired->ws_timestamp = TIME_$CLOCKH;
        wired->owner++;
        if (wired->owner >= PMAP_$WS_INTERVAL) {        /* bcs 0x00E145B2 */
            wired->owner = 0;
            wired->scan_pos = wired->page_count;
            MMAP_$WS_SCAN(MMAP_WSL_POOL_WIRED, 0, PMAP_WSC_SCAN_ALL, PMAP_WSC_SCAN_ALL);
        }
    }

    /* 0x00E145D6 */
    ML_$UNLOCK(PMAP_LOCK_ID);
}
