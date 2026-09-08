/*
 * PMAP_$WS_SCAN_CALLBACK - Working set scan timer callback
 *
 * Called periodically by the timer system to scan working sets
 * and age page references for the clock page replacement algorithm.
 *
 * Parameters:
 *   param - Pointer to callback parameter containing slot index
 *
 * Original address: 0x00e144f6
 */

#include "pmap/pmap_internal.h"
#include "misc/misc.h"

/* MMAP_$WSL_HI_MARK (0xE23CA6) is indexed as an array here; the same
 * array is exported by mmap/mmap.h as MMAP_PID_TO_WSL. */

/* Working set list entry structure offsets */
#define WSL_FLAGS_OFFSET        0x00
#define WSL_INTERVAL_OFFSET     0x02
#define WSL_PAGE_COUNT_OFFSET   0x04    /* At MMAP_$WSL_FREE_CNT (0x00E232B4) + slot*0x24 */
#define WSL_PREV_COUNT_OFFSET   0x08    /* At DAT_00e232b8 + slot*0x24 */
#define WSL_LAST_SCAN_OFFSET    0x18    /* At DAT_00e232cc + slot*0x24 */

#if defined(ARCH_M68K)
    #define WSL_BASE                0xE232B0
    #define WSL_PAGE_COUNT_BASE     0xE232B4
    #define WSL_PREV_COUNT_BASE     0xE232B8
    #define WSL_LAST_SCAN_BASE      0xE232CC
#else
    /* wsl_base: pmap_internal.h */
    #define WSL_BASE                ((uintptr_t)wsl_base)
    #define WSL_PAGE_COUNT_BASE     (WSL_BASE + 4)
    #define WSL_PREV_COUNT_BASE     (WSL_BASE + 8)
    #define WSL_LAST_SCAN_BASE      (WSL_BASE + 0x1C)
#endif

void PMAP_$WS_SCAN_CALLBACK(int *param)
{
    uint16_t slot_index;
    int16_t slot;
    int wsl_offset;

    /* Get slot index from callback parameter */
    slot_index = *(uint16_t *)(*param + 2);

    /* Validate slot index */
    if (slot_index > 0x40) {
        CRASH_SYSTEM(&status_$t_00e145ec);
    }

    ML_$LOCK(PMAP_LOCK_ID);

    /* Get actual slot from high mark table */
    slot = (int16_t)MMAP_PID_TO_WSL[slot_index];

    if (slot != 0) {
        wsl_offset = (int16_t)(slot * 0x24);

        /* Increment scan interval counter */
        int16_t *interval_ptr = (int16_t *)(WSL_BASE + wsl_offset + WSL_INTERVAL_OFFSET);
        (*interval_ptr)++;

        /* Check if we've reached the scan interval */
        if (PMAP_$WS_INTERVAL <= *(uint16_t *)(WSL_BASE + wsl_offset + WSL_INTERVAL_OFFSET)) {
            /* Reset interval counter */
            *(uint16_t *)(WSL_BASE + wsl_offset + WSL_INTERVAL_OFFSET) = 0;

            /* Save previous page count */
            *(uint32_t *)(WSL_PREV_COUNT_BASE + wsl_offset) =
                *(uint32_t *)(WSL_PAGE_COUNT_BASE + wsl_offset);

            /* Record scan time */
            *(uint32_t *)(WSL_LAST_SCAN_BASE + wsl_offset) = TIME_$CLOCKH;

            /* Perform the working set scan */
            MMAP_$WS_SCAN(slot, 0, 0x3FFFFF, 0x3FFFFF);
        }
    }

    /*
     * Check for global scan timing.  The four cells the image touches here
     * are fields of the WIRED pool's ws_hdr_t, MMAP_WSL[MMAP_WSL_POOL_WIRED]
     * at 0xE232B0 + 5 * 0x24 = 0xE23364 (see pmap/pmap_internal.h):
     *
     *   0xE23380  record + 0x1C  ws_timestamp   - the global scan clock
     *   0xE2337C  record + 0x18  pri_timestamp
     *   0xE23366  record + 0x02  owner          - the global scan counter
     *   0xE2336C  record + 0x08  scan_pos
     */
    if (MMAP_WSL[MMAP_WSL_POOL_WIRED].ws_timestamp + 8 < TIME_$CLOCKH) {
        MMAP_WSL[MMAP_WSL_POOL_WIRED].pri_timestamp = TIME_$CLOCKH;
        MMAP_WSL[MMAP_WSL_POOL_WIRED].ws_timestamp  = TIME_$CLOCKH;

        MMAP_WSL[MMAP_WSL_POOL_WIRED].owner++;
        if (PMAP_$WS_INTERVAL <= MMAP_WSL[MMAP_WSL_POOL_WIRED].owner) {
            MMAP_WSL[MMAP_WSL_POOL_WIRED].owner = 0;
            MMAP_WSL[MMAP_WSL_POOL_WIRED].scan_pos = MMAP_$WSL_WIRED_CNT;

            /* Perform global working set scan on slot 5 */
            MMAP_$WS_SCAN(5, 0, 0x3FFFFF, 0x3FFFFF);
        }
    }

    ML_$UNLOCK(PMAP_LOCK_ID);
}
