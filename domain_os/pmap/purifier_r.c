/*
 * PMAP_$PURIFIER_R - Remote page purifier process
 *
 * Background process that writes dirty pages to remote disk (network storage).
 * Runs continuously, waking when signaled by PMAP_$R_PURIFIER_EC.
 * Handles pages one at a time unlike the batched local purifier.
 *
 * This function never returns - it runs as a kernel daemon.
 *
 * Original address: 0x00e1416c
 */

#include "pmap/pmap_internal.h"
#include "math/math.h"

/* Include mmap.h and mmu.h for MMAPE_BASE and PFT_BASE */
#include "mmap/mmap.h"
#include "mmu/mmu.h"

/* Additional base addresses */
#if defined(M68K)
    #define SEGMAP_BASE_ADDR    0xED5000
#else
    extern uint8_t segmap_base[];
    #define SEGMAP_BASE_ADDR    ((uintptr_t)segmap_base)
#endif

/* Additional data defined in pmap_internal.h */

void PMAP_$PURIFIER_R(void)
{
    uint32_t batch_pages[16];
    uint32_t page_counts[3];
    uint16_t page_count;
    status_$t status[2];
    int32_t wait_value;
    ec_$eventcount_t *wait_ecs[3];
    uint32_t scan_time;
    uint32_t recalc_time;
    uint32_t carryover;
    uint32_t carryover_delta;
    uint32_t total_pages;
    int pmape_offset;
    int i;

    /* Brief lock/unlock for initialization synchronization */
    ML_$LOCK(1);
    ML_$UNLOCK(1);

    /* Set process lock for purifier */
    PROC1_$SET_LOCK(0x0D);

    /* Initialize timing */
    scan_time = TIME_$CLOCKH + 0xE4;
    wait_value = PMAP_$R_PURIFIER_EC.value + 1;

    /* Set up eventcount array for EC_$WAIT (NULL-terminated) */
    wait_ecs[0] = &PMAP_$R_PURIFIER_EC;
    wait_ecs[1] = NULL;
    wait_ecs[2] = NULL;

    carryover = 0;
    carryover_delta = 0;
    recalc_time = scan_time;

    /* Main purifier loop - runs forever */
    for (;;) {
        /* Wait for purifier signal */
        EC_$WAIT(wait_ecs, &wait_value);

        /* Timing-based carryover updates */
        if ((int32_t)scan_time <= TIME_$CLOCKH) {
            /* Periodic recalculation of carryover rate */
            if (recalc_time <= scan_time) {
                recalc_time = scan_time + 0xE4;
                carryover_delta = M$DIU$LLW(DAT_00e23344 + 5, 6);
            }

            scan_time += 0x26;  /* ~38 ticks */
            carryover += carryover_delta;
        }

        ML_$LOCK(PMAP_LOCK_ID);

        /* Process remote pages */
        total_pages = DAT_00e232b4 + DAT_00e232fc + DAT_00e232d8;

        while (DAT_00e23344 != 0 &&
               (carryover != 0 || total_pages < PMAP_$MID_THRESH)) {

            /* Get single impure page (count=1) */
            MMAP_$GET_IMPURE(4, batch_pages, -(total_pages < PMAP_$MID_THRESH),
                            1, page_counts, &page_count);

            if (page_count != 0) {
                /* Process each page (typically just 1) */
                for (i = 0; i < page_count; i++) {
                    uint32_t vpn = batch_pages[i];
                    pmape_offset = vpn * 0x10;

                    /* Mark segment map entry as being written */
                    uint16_t seg_idx = *(uint16_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 2);
                    uint8_t page_idx = *(uint8_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 1);
                    uint8_t *segmap_entry = (uint8_t *)(SEGMAP_BASE_ADDR +
                                                        seg_idx * 0x80 +
                                                        (page_idx << 2));
                    segmap_entry[0] |= 0x80;

                    /* Clear MMU modified bit */
                    *(uint16_t *)((uintptr_t)PFT_BASE + vpn * 4 + 2) &= 0xBFFF;

                    /* Copy transition state if priority > 5 */
                    uint8_t trans_state = *(uint8_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 8);
                    if (trans_state > 5) {
                        *(uint8_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 4) = trans_state;
                    }

                    /* Write page to remote storage */
                    FUN_00e12e5e(vpn, status, 0xFF);

                    if (status[0] == 0) {
                        /* Write succeeded */
                        EC_$ADVANCE(&PMAP_$PAGES_EC);
                        MMAP_$AVAIL(vpn);
                    } else if (*(uint8_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 4) == 0x04 &&
                              status[0] != 0x30001 &&
                              status[0] != 0x30005 &&
                              status[0] != 0xF0001) {
                        /* Recoverable error - retry later */
                        MMAP_$UNAVAIL_REMOV(vpn);
                        *(uint8_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 4) = 5;
                        MMAP_$AVAIL(vpn);
                    }
                    /* Note: other errors leave page in current state for retry */
                }
            }

            PMAP_$PUR_R_CNT += (uint32_t)page_count;

            /* Update carryover */
            if (carryover < page_counts[0]) {
                carryover = 0;
            } else {
                carryover -= page_counts[0];
            }

            /* Recalculate total for next iteration */
            total_pages = DAT_00e232b4 + DAT_00e232fc + DAT_00e232d8;
        }

        /* Update wait value for next iteration */
        wait_value = PMAP_$R_PURIFIER_EC.value + 1;

        ML_$UNLOCK(PMAP_LOCK_ID);
    }
}
