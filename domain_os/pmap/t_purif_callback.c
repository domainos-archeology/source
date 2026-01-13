/*
 * PMAP_$T_PURIF_CALLBACK - Timer-based purifier callback
 *
 * Periodic callback that scans working sets and purges idle pages.
 * This function cycles through working set slots 5-69 and performs
 * scans to age pages and identify candidates for purging.
 *
 * Original address: 0x00e143cc
 */

#include "pmap.h"

/* External data */
extern uint16_t DAT_00e254e4;   /* Current scan slot (5-69) */
extern uint32_t PMAP_$T_PUR_SCANS;  /* Total timer purifier scans */
extern uint32_t MMAP_$PAGEABLE_PAGES_LOWER_LIMIT;

/* Working set list base and offsets */
#if defined(M68K)
    #define WSL_BASE            0xE232B0
#else
    extern uint8_t wsl_base[];
    #define WSL_BASE            ((uintptr_t)wsl_base)
#endif

/* WSL entry structure offsets within each 0x24-byte entry */
#define WSL_FLAGS_OFFSET        0x00    /* Flags word */
#define WSL_INTERVAL_OFFSET     0x02    /* Scan interval counter */
#define WSL_PAGE_COUNT_OFFSET   0x04    /* Page count */
#define WSL_PREV_COUNT_OFFSET   0x08    /* Previous page count */
#define WSL_WS_LIMIT_OFFSET     0x10    /* Working set limit */
#define WSL_PREV_SCAN_OFFSET    0x18    /* Previous scan time */
#define WSL_LAST_SCAN_OFFSET    0x1C    /* Last scan time */

void PMAP_$T_PURIF_CALLBACK(void)
{
    int32_t current_time;
    int wsl_offset;

    current_time = TIME_$CLOCKH;

    /* Cycle through slots 5-69 (wraps from 0x45=69 back to 5) */
    if (DAT_00e254e4 == 0x45) {
        DAT_00e254e4 = 5;
    } else {
        DAT_00e254e4++;
    }

    PMAP_$T_PUR_SCANS++;

    /* Calculate WSL entry offset: slot * 0x24 bytes */
    wsl_offset = (int16_t)(DAT_00e254e4 * 0x24);

    /* Check WSL flags - if bit 13 not set, update working set limit */
    if ((*(uint16_t *)(WSL_BASE + wsl_offset) & 0x2000) == 0) {
        uint32_t limit = MMAP_$PAGEABLE_PAGES_LOWER_LIMIT >> 2;
        if (limit > 0x800) {
            limit = 0x800;
        }
        *(uint32_t *)(WSL_BASE + wsl_offset + WSL_WS_LIMIT_OFFSET) =
            MMAP_$PAGEABLE_PAGES_LOWER_LIMIT - limit;
    }

    /* Check if slot has pages and needs scanning */
    if (*(int32_t *)(WSL_BASE + wsl_offset + WSL_PAGE_COUNT_OFFSET) != 0 &&
        *(int32_t *)(WSL_BASE + wsl_offset + WSL_LAST_SCAN_OFFSET) <= current_time - 0x1CA) {

        ML_$LOCK(PMAP_LOCK_ID);

        /* Check if last scan time is 0 and prev scan is old enough */
        if (*(int32_t *)(WSL_BASE + wsl_offset + WSL_LAST_SCAN_OFFSET) == 0 &&
            *(int32_t *)(WSL_BASE + wsl_offset + WSL_PREV_SCAN_OFFSET) < current_time - 0x1CA) {
            /* Slot is completely idle - purge it */
            MMAP_$PURGE(DAT_00e254e4);
        } else {
            /* Save current page count as previous */
            *(uint32_t *)(WSL_BASE + wsl_offset + WSL_PREV_COUNT_OFFSET) =
                *(uint32_t *)(WSL_BASE + wsl_offset + WSL_PAGE_COUNT_OFFSET);

            /* Decide whether to do a full or partial scan */
            uint16_t do_full_scan;

            if ((*(uint16_t *)(WSL_BASE + wsl_offset) & 0x4000) == 0 &&
                *(uint16_t *)(WSL_BASE + wsl_offset + WSL_INTERVAL_OFFSET) <= PMAP_$WS_INTERVAL &&
                (*(int32_t *)(WSL_BASE + wsl_offset + WSL_PREV_SCAN_OFFSET) <=
                     *(int32_t *)(WSL_BASE + wsl_offset + WSL_LAST_SCAN_OFFSET) ||
                 current_time - 0x26 <= *(int32_t *)(WSL_BASE + wsl_offset + WSL_PREV_SCAN_OFFSET))) {
                /* Partial scan - only age without full scan */
                do_full_scan = 0xFF;  /* -1 in signed context */
            } else {
                /* Full scan - reset interval and update timestamps */
                *(uint16_t *)(WSL_BASE + wsl_offset + WSL_INTERVAL_OFFSET) = 0;
                *(int32_t *)(WSL_BASE + wsl_offset + WSL_LAST_SCAN_OFFSET) = current_time;
                do_full_scan = 0;
            }

            /* Perform the working set scan */
            MMAP_$WS_SCAN(DAT_00e254e4, do_full_scan, 0x3FFFFF, 0x3FFFFF);
        }

        ML_$UNLOCK(PMAP_LOCK_ID);

        /* Signal that pages may be available */
        EC_$ADVANCE(&PMAP_$PAGES_EC);
    }
}
