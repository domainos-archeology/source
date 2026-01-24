/*
 * PMAP_$PURIFIER_L - Local page purifier process
 *
 * Background process that writes dirty pages to local disk.
 * Runs continuously, waking when signaled by PMAP_$L_PURIFIER_EC.
 * Uses batch I/O for efficiency.
 *
 * This function never returns - it runs as a kernel daemon.
 *
 * Original address: 0x00e13a9c
 */

#include "pmap/pmap_internal.h"
#include "ast/ast.h"
#include "cal/cal.h"
#include "misc/misc.h"
#include "math/math.h"

/* External working set list high mark array */
extern uint16_t MMAP_$WSL_HI_MARK[];

/* Short wait delay for working set scanning */
extern clock_t PMAP_$SHORT_WAIT_DELAY;

/* Network diskless flag */
extern int8_t NETWORK_$DISKLESS;

/* Disk checksum control flag */
extern int8_t DISK_$DO_CHKSUM;

/* Include mmap.h and mmu.h for MMAPE_BASE and PFT_BASE */
#include "mmap/mmap.h"
#include "mmu/mmu.h"

/* Additional base addresses */
#if defined(M68K)
    #define WSL_BASE            0xE232B0
    #define SEGMAP_BASE_ADDR    0xED5000
    #define AOTE_TABLE_BASE     0xEC53F0
    #define PUR_STATS_BASE      0xE25D18
#else
    extern uint8_t wsl_base[];
    extern uint8_t segmap_base[];
    extern uint8_t aote_table[];
    extern uint8_t pur_stats[];
    #define WSL_BASE            ((uintptr_t)wsl_base)
    #define SEGMAP_BASE_ADDR    ((uintptr_t)segmap_base)
    #define AOTE_TABLE_BASE     ((uintptr_t)aote_table)
    #define PUR_STATS_BASE      ((uintptr_t)pur_stats)
#endif

/* Additional base addresses defined in pmap_internal.h */

void PMAP_$PURIFIER_L(void)
{
    uint32_t batch_pages[16];
    uint32_t page_counts[5];
    uint16_t page_count;
    uint16_t dummy_status;
    status_$t status;
    int32_t qblk_main;
    int32_t qblk_alt[3];
    int32_t wait_value;
    ec_$eventcount_t *wait_ecs[3];
    uint32_t scan_time;
    uint32_t log_time;
    uint32_t shutdown_time;
    uint32_t carryover;
    uint32_t carryover_delta;
    int32_t steal_count;
    int32_t prev_steal;
    int32_t current_time;
    int8_t did_advance;
    int8_t below_thresh;
    uint32_t total_pages;
    uint32_t slot_pages;
    int pmape_offset;
    int wsl_offset;
    int i;

    /* Brief lock/unlock for initialization synchronization */
    ML_$LOCK(1);
    ML_$UNLOCK(1);

    /* Set process lock for purifier */
    PROC1_$SET_LOCK(0x0D);

    /* Initialize timing */
    scan_time = TIME_$CLOCKH + 0xE4;  /* ~228 ticks */
    wait_value = PMAP_$L_PURIFIER_EC.value + 1;

    /* Set up eventcount array for EC_$WAIT (NULL-terminated) */
    wait_ecs[0] = &PMAP_$L_PURIFIER_EC;
    wait_ecs[1] = NULL;
    wait_ecs[2] = NULL;

    /* Calculate initial thresholds */
    PMAP_$LOW_THRESH = (uint16_t)(MMAP_$PAGEABLE_PAGES_LOWER_LIMIT / 0x32);
    PMAP_$MID_THRESH = (uint16_t)(MMAP_$PAGEABLE_PAGES_LOWER_LIMIT / 0x14);

    carryover = 0;
    carryover_delta = 0;
    steal_count = 0;
    prev_steal = 0;
    log_time = scan_time;
    shutdown_time = scan_time;

    /* Initialize hardware */
    FUN_00e2f880();

    /* Main purifier loop - runs forever */
    for (;;) {
        /* Wait for purifier signal */
        EC_$WAIT(wait_ecs, &wait_value);

        ML_$LOCK(PMAP_LOCK_ID);
        did_advance = 0;

        do {
            /* Calculate total available pages */
            total_pages = DAT_00e232b4 + DAT_00e232fc + DAT_00e232d8;
            below_thresh = -(total_pages < PMAP_$MID_THRESH);

            /* Process impure pages if needed */
            if ((below_thresh < 0 || carryover == 0) && DAT_00e23320 == 0) {
                break;
            }

            if (DAT_00e23320 == 0) break;

            /* Get batch of impure pages */
            MMAP_$GET_IMPURE(3, batch_pages, -(total_pages < PMAP_$MID_THRESH),
                            0x10, page_counts, &page_count);

            if (page_count != 0) {
                /* Process each page in batch */
                for (i = 0; i < page_count; i++) {
                    uint32_t vpn = batch_pages[i];
                    pmape_offset = vpn * 0x10;

                    /* Update segment map to mark page as being written */
                    uint16_t seg_idx = *(uint16_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 2);
                    uint8_t page_idx = *(uint8_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 1);
                    uint8_t *segmap_entry = (uint8_t *)(SEGMAP_BASE_ADDR +
                                                        seg_idx * 0x80 +
                                                        (page_idx << 2));
                    segmap_entry[0] |= 0x80;

                    /* Clear MMU modified bit and update AOTE timestamp */
                    int pte_offset = vpn * 4;
                    if ((*(uint16_t *)((uintptr_t)PFT_BASE + pte_offset + 2) & 0x4000) != 0) {
                        *(uint16_t *)((uintptr_t)PFT_BASE + pte_offset + 2) &= 0xBFFF;

                        /* Update AOTE timestamps if not remote */
                        if (*(int8_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 9) >= 0) {
                            int aote_offset = (int16_t)(seg_idx * 0x14);
                            struct aote_t *aote = *(struct aote_t **)(AOTE_TABLE_BASE + aote_offset);
                            TIME_$ABS_CLOCK((clock_t *)((char *)aote + 0x38));
                            TIME_$CLOCK((clock_t *)((char *)aote + 0x28));
                            *(uint32_t *)((char *)aote + 0x40) = *(uint32_t *)((char *)aote + 0x28);
                            *(uint16_t *)((char *)aote + 0x44) = *(uint16_t *)((char *)aote + 0x2c);
                            *(uint8_t *)((char *)aote + 0xBF) |= 0x20;
                        }
                    }

                    /* Copy transition state if needed */
                    if (*(uint8_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 8) != 0) {
                        *(uint8_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 4) =
                            *(uint8_t *)((uintptr_t)MMAPE_BASE + pmape_offset + 8);
                    }
                }

                ML_$UNLOCK(PMAP_LOCK_ID);

                /* Get disk queue blocks */
                DISK_$GET_QBLKS(page_count, &qblk_main, qblk_alt);

                /* Set up page descriptors for write */
                FUN_00e1327e(batch_pages, qblk_main, page_count);

                /* Write pages to disk */
                DISK_$WRITE_MULTI(0xFF, (void *)(uintptr_t)qblk_main, NULL, &status);
                if (status != 0) {
                    CRASH_SYSTEM(&status);
                }

                /* Update statistics */
                *(uint32_t *)(PUR_STATS_BASE + (int16_t)(PROC1_$CURRENT << 4)) +=
                    (uint32_t)page_count;

                did_advance = 0;
                ML_$LOCK(PMAP_LOCK_ID);

                /* Process completed pages */
                if (page_count != 0) {
                    int qblk_ptr = qblk_main;
                    for (i = 0; i < page_count; i++) {
                        uint32_t vpn = *(uint32_t *)(qblk_ptr + 0x14);

                        FUN_00e12d84((int16_t)vpn, (int16_t)(qblk_ptr + 0x0C));

                        if (*(int32_t *)(qblk_ptr + 0x0C) == 0) {
                            /* Write succeeded */
                            did_advance = -1;
                            MMAP_$AVAIL(vpn);
                        } else if (*(uint8_t *)(vpn * 0x10 + (uintptr_t)MMAPE_BASE + 4) == 0x03) {
                            /* Page was modified during write */
                            MMAP_$UNAVAIL_REMOV(vpn);
                            *(uint8_t *)(vpn * 0x10 + (uintptr_t)MMAPE_BASE + 4) = 5;
                            MMAP_$AVAIL(vpn);
                        }

                        qblk_ptr = *(int32_t *)(qblk_ptr + 8);
                    }
                }

                /* Log if enabled */
                if (NETLOG_$OK_TO_LOG < 0) {
                    uid_t nil_uid = UID_$NIL;
                    NETLOG_$LOG_IT(0x0D, &nil_uid, page_count, dummy_status,
                                  (int16_t)(*(uint32_t *)(qblk_main + 0x3C) >> 16),
                                  (int16_t)(*(uint32_t *)(qblk_main + 0x3C)),
                                  (int16_t)(*(uint32_t *)(qblk_alt[0] + 0x3C) >> 16),
                                  (int16_t)(*(uint32_t *)(qblk_alt[0] + 0x3C)));
                }

                /* Return queue blocks */
                DISK_$RTN_QBLKS(page_count, qblk_main, qblk_alt[0]);

                if (did_advance < 0) {
                    EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC);
                    EC_$ADVANCE(&PMAP_$PAGES_EC);
                }

                PMAP_$PUR_L_CNT += (uint32_t)page_count;
            }

            /* Update carryover */
            if (carryover < page_counts[0]) {
                carryover = 0;
            } else {
                carryover -= page_counts[0];
            }

        } while (0);  /* Single iteration with break logic */

        /* Update wait value for next iteration */
        wait_value = PMAP_$L_PURIFIER_EC.value + 1;

        /* Working set scanning when pages are low */
        total_pages = DAT_00e23320 + total_pages;
        while (total_pages < 0x18) {
            prev_steal++;

            /* Scan all working sets */
            slot_pages = 0;
            if (MMAP_$WSL_HI_MARK[0] > 4) {
                for (uint16_t slot = MMAP_$WSL_HI_MARK[0]; slot > 4; slot--) {
                    wsl_offset = slot * 0x24;
                    uint32_t *wsl_entry = (uint32_t *)(WSL_BASE + wsl_offset);

                    if (wsl_entry[1] != 0) {  /* Page count */
                        if (*(uint16_t *)(WSL_BASE + wsl_offset + 2) > PMAP_$WS_INTERVAL) {
                            /* Overdue for scan */
                            *(uint16_t *)(WSL_BASE + wsl_offset + 2) = 0;
                            wsl_entry[7] = TIME_$CLOCKH;  /* Update last scan time */
                            MMAP_$WS_SCAN(slot, 0, 0x3FFFFF, 0x3FFFFF);
                            goto wait_and_continue;
                        }

                        if ((int32_t)wsl_entry[6] < TIME_$CLOCKH - PMAP_$IDLE_INTERVAL) {
                            /* Slot is idle - purge it */
                            MMAP_$PURGE(slot);
                            goto wait_and_continue;
                        }

                        /* Accumulate page count for selection */
                        if (wsl_entry[8] < wsl_entry[1] || total_pages == 0) {
                            slot_pages += (uint16_t)wsl_entry[1];
                        }
                    }
                }
            }

            if (slot_pages == 0) break;

            /* Random selection of working set to scan */
            DAT_00e254e2 = (DAT_00e254e2 * 0x3039) & 0x3FF;
            uint32_t target = ((uint32_t)slot_pages * (uint32_t)DAT_00e254e2) >> 10;
            uint32_t accumulator = 0;

            if (MMAP_$WSL_HI_MARK[0] > 4) {
                for (uint16_t slot = MMAP_$WSL_HI_MARK[0]; slot > 4; slot--) {
                    wsl_offset = slot * 0x24;
                    uint32_t *wsl_entry = (uint32_t *)(WSL_BASE + wsl_offset);

                    if (wsl_entry[8] < wsl_entry[1] || total_pages == 0) {
                        accumulator += wsl_entry[1];
                    }

                    if (accumulator > target) {
                        MMAP_$WS_SCAN(slot, 0, 1, 0x3FFFFF);
                        break;
                    }
                }
            }

wait_and_continue:
            ML_$UNLOCK(PMAP_LOCK_ID);
            TIME_$WAIT(&PMAP_$SHORT_WAIT_DELAY, &status);
            ML_$LOCK(PMAP_LOCK_ID);

            total_pages = DAT_00e232b4 + DAT_00e232d8 + DAT_00e232fc +
                         DAT_00e23320 + DAT_00e23344;
        }

        /* Ensure we advance pages EC */
        if (did_advance >= 0) {
            EC_$ADVANCE(&PMAP_$PAGES_EC);
            did_advance = -1;
        }

        /* Periodic threshold adjustment */
        if ((int32_t)scan_time <= TIME_$CLOCKH) {
            scan_time += 0x13;  /* ~19 ticks */
            carryover += carryover_delta;

            int32_t total_steal = MMAP_$STEAL_CNT + prev_steal;
            uint32_t steal_delta = total_steal - steal_count;

            if (total_steal == steal_count) {
                PMAP_$WS_INTERVAL += PMAP_$WS_SCAN_DELTA;
                steal_count = total_steal;
                if (PMAP_$MAX_WS_INTERVAL < PMAP_$WS_INTERVAL) {
                    PMAP_$WS_INTERVAL = PMAP_$MAX_WS_INTERVAL;
                }
            } else {
                steal_count = total_steal;
                if (steal_delta > 5) {
                    PMAP_$WS_INTERVAL >>= 1;
                    if (PMAP_$WS_INTERVAL < PMAP_$MIN_WS_INTERVAL) {
                        PMAP_$WS_INTERVAL = PMAP_$MIN_WS_INTERVAL;
                    }
                }
            }
        }

        ML_$UNLOCK(PMAP_LOCK_ID);

        /* Periodic log flush and threshold recalculation */
        if (log_time <= scan_time) {
            carryover_delta = M$DIU$LLW(DAT_00e23320 + 0x0B, 0x0C);
            PMAP_$LOW_THRESH = (uint16_t)((PMAP_$LOW_THRESH +
                M$DIU$LLW(MMAP_$PAGEABLE_PAGES_LOWER_LIMIT, 0x32)) >> 1);
            PMAP_$MID_THRESH = (uint16_t)((PMAP_$MID_THRESH +
                M$DIU$LLW(MMAP_$PAGEABLE_PAGES_LOWER_LIMIT, 0x14)) >> 1);

            int32_t log_vpn = LOG_$UPDATE();
            if (log_vpn != 0) {
                int8_t saved_chksum;
                ML_$LOCK(PMAP_LOCK_ID);

                if (NETWORK_$DISKLESS >= 0) {
                    saved_chksum = DISK_$DO_CHKSUM;
                    DISK_$DO_CHKSUM = 0;
                }

                FUN_00e12e5e(log_vpn, &status, 0);

                if (NETWORK_$DISKLESS >= 0) {
                    DISK_$DO_CHKSUM = saved_chksum;
                }

                if (status != 0) {
                    LOG_$LOGFILE_PTR = 0;
                }

                ML_$UNLOCK(PMAP_LOCK_ID);
            }

            log_time = scan_time + 0xE4;
        }

        /* Periodic shutdown check */
        if (PMAP_$SHUTTING_DOWN_FLAG >= 0 && shutdown_time <= scan_time) {
            if (NETWORK_$DISKLESS >= 0) {
                CAL_$SHUTDOWN(&status);
                if (status != 0) {
                    CRASH_SYSTEM(&status);
                }
            }
            shutdown_time = scan_time + 0x3570;  /* ~13680 ticks */
        }
    }
}
