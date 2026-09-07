/*
 * PMAP_$PURIFIER_L - Local page purifier process
 *
 * Background process that writes dirty pages to local disk.  It sleeps on
 * PMAP_$L_PURIFIER_EC (with a clock-based timeout), drains the "dirty,
 * local" page pool through DISK_$WRITE_MULTI in batches of up to 16 pages,
 * scans working sets when free pages get low, and periodically retunes the
 * page thresholds, flushes the log file and runs the shutdown check.
 *
 * This function never returns - it runs as a kernel daemon.
 *
 * Structure recovered from the disassembly (branch targets in brackets):
 *
 *   [E13B28] forever:
 *              EC_$WAIT(...); ML_$LOCK(20); batch_advanced = false
 *              do {
 *   [E13B62]      for (;;) {                       inner drain loop
 *                     total = free + impure + pure
 *                     below_thresh = total < MID_THRESH
 *                     if ((!below_thresh && carryover == 0)
 *                         || dirty_local == 0) break;      [E13B8C/E13B96]
 *                     MMAP_$GET_IMPURE(...)
 *                     if (page_count != 0) { ...write batch... }
 *   [E13E0A]          carryover -= scanned (saturating)
 *                 }                                        [E13E14/E13E1C]
 *   [E13E20]      wait_value = ec.value + 1; total += dirty_local
 *   [E13FA2]      while (total < 0x18) { ws scan pass; unlock; wait; lock }
 *   [E13FAC]      if (!did_advance) { EC_$ADVANCE(PAGES_EC); did_advance = 1 }
 *   [E13FC2]  } while (below_thresh && dirty_local != 0);
 *   [E13FD2]  threshold retune; ML_$UNLOCK(20)
 *   [E14042]  periodic log flush
 *   [E14122]  periodic shutdown check
 *
 * Original address: 0x00e13a9c
 * Size: 1742 bytes
 * A5 (module base) = 0x00E24D44
 */

#include "pmap/pmap_internal.h"
#include "ast/ast.h"
#include "cal/cal.h"
#include "misc/misc.h"
#include "math/math.h"
#include "netlog/netlog.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"

/*
 * Constant cells in the code region that the original passes by reference.
 *
 * TIME_$WAIT's delay-type parameter is `pea (0x200,PC)` at 0x00E13F68,
 * which resolves to 0x00E1416A - the two zero bytes between the end of
 * PMAP_$PURIFIER_L and the entry of PMAP_$PURIFIER_R.  Delay type 0 is a
 * relative wait.
 */
static const uint16_t pmap_l_relative_delay_type = 0;   /* 0x00E1416A */

/*
 * Tuning constants, all immediates in the original.
 */
#define PMAP_L_SCAN_PERIOD      0x13    /* 0x00E13FDE: scan_time increment */
#define PMAP_L_LOG_PERIOD       0xE4    /* 0x00E14118: log flush period */
#define PMAP_L_SHUTDOWN_PERIOD  0x3570  /* 0x00E1415C: shutdown check period */
#define PMAP_L_INITIAL_DELAY    0xE4    /* 0x00E13ADA: first deadline */
#define PMAP_L_BATCH_MAX        0x10    /* 0x00E13BA4: pages per DISK write */
#define PMAP_L_LOW_WATER        0x18    /* 0x00E13FA2: ws scan trigger */
#define PMAP_L_RAND_MULT        0x3039  /* 0x00E13ED4 */
#define PMAP_L_RAND_MASK        0x03FF  /* 0x00E13ED8 */
#define PMAP_L_WS_SCAN_ALL      0x3FFFFF /* 0x00E13E8E / 0x00E13F2E */
#define PMAP_L_STEAL_HYSTERESIS 5       /* 0x00E14016 */

/*
 * pmap_$aote_for_segment - AOTE that owns a segment.
 *
 * 0x00E13C0C-0x00E13C1E computes (seg * 0x14) in a 16-bit register and uses
 * it as a signed word displacement from 0xEC5400-0x10.  0xEC5400 is
 * ASTE_BASE and an aste_t is 0x14 bytes, so the effective address is
 * &ASTE_BASE[seg - 1].aote: the segment index held in mmape_t.segment is a
 * 1-based index into the ASTE table.  The 16-bit truncation of the product
 * is preserved here because it is what the hardware does.
 */
static aote_t *pmap_$aote_for_segment(uint16_t seg)
{
    int16_t disp = (int16_t)(seg * 0x14);

    return *(aote_t **)((uint8_t *)ASTE_BASE - 0x10 + disp);
}

/*
 * pmap_$purifier_ws_scan_pass - one pass of the working-set scan/steal loop.
 *
 * This is the body of the `while (total_pages < 0x18)` loop at
 * 0x00E13E34-0x00E13F4E, up to but not including the unlock / TIME_$WAIT /
 * lock at 0x00E13F52.  It walks the user working sets from the high-water
 * mark down to slot 5 and takes the first action that applies:
 *
 *   - a working set whose age counter (ws_hdr_t.owner, 0x02) has passed
 *     PMAP_$WS_INTERVAL is scanned in full and its age reset  [E13E82]
 *   - a working set untouched for longer than PMAP_$IDLE_INTERVAL is
 *     purged outright                                        [E13EA0]
 *   - otherwise the pages of every working set above its floor are summed
 *     and one working set is picked in proportion to its size by a
 *     multiplicative-congruential draw, then scanned for one page [E13F2E]
 *
 * Returns true (0xFF) when the caller should fall through to the unlock /
 * wait / re-lock at 0x00E13F52, and false when the loop must be abandoned
 * because no working set has a stealable page (0x00E13ECC -> 0x00E13FAC).
 *
 * prev_steal is the caller's local at (-0x60,A6), incremented once per pass
 * at 0x00E13E3E.
 */
static boolean pmap_$purifier_ws_scan_pass(uint32_t total_pages,
                                           int32_t *prev_steal)
{
    /* 0x00E13E34: idle cutoff, computed once per pass */
    int32_t idle_cutoff = (int32_t)(TIME_$CLOCKH - PMAP_$IDLE_INTERVAL);
    uint16_t slot;
    uint16_t slot_pages;        /* D4w - a 16-bit accumulator (0x00E13E42) */
    uint32_t accumulator;       /* D1  - 32 bits (0x00E13EE2) */
    uint16_t target;            /* D5w - 16 bits (0x00E13F02) */

    (*prev_steal)++;            /* 0x00E13E3E */

    slot_pages = 0;

    /* 0x00E13E44: skip the walk entirely when there are no user slots */
    if (MMAP_WSL_HI_MARK >= 5) {
        /* 0x00E13E4E-0x00E13EC6: slot = HI_MARK down to 5 inclusive */
        for (slot = MMAP_WSL_HI_MARK; slot >= 5; slot--) {
            ws_hdr_t *ws = &MMAP_WSL[slot];

            if (ws->page_count != 0) {                      /* 0x00E13E72 */
                if (ws->owner > PMAP_$WS_INTERVAL) {        /* 0x00E13E78 */
                    /* Overdue: reset the age and rescan the whole set. */
                    ws->owner = 0;                          /* 0x00E13E82 */
                    ws->ws_timestamp = TIME_$CLOCKH;        /* 0x00E13E86 */
                    MMAP_$WS_SCAN(slot, 0, PMAP_L_WS_SCAN_ALL,
                                  PMAP_L_WS_SCAN_ALL);      /* 0x00E13E8E */
                    return true;
                }

                if ((int32_t)ws->pri_timestamp < idle_cutoff) {  /* 0x00E13E9A */
                    MMAP_$PURGE(slot);                      /* 0x00E13EA0 */
                    return true;
                }

                /*
                 * 0x00E13EB0-0x00E13EBE: only pages above the working-set
                 * floor are candidates, unless nothing at all is free.
                 * The add is `add.w D7w,D4w` - 16 bits wide.
                 */
                if (ws->ws_floor < ws->page_count || total_pages == 0) {
                    slot_pages = (uint16_t)(slot_pages + (uint16_t)ws->page_count);
                }
            }
        }
    }

    /* 0x00E13ECA: nothing stealable - leave the low-memory loop */
    if (slot_pages == 0) {
        return false;
    }

    /* 0x00E13ED0-0x00E13EE6: pick a page index in [0, slot_pages) */
    PMAP_$WS_RANDOM_SEED = (uint16_t)((uint16_t)(PMAP_$WS_RANDOM_SEED * PMAP_L_RAND_MULT)
                              & PMAP_L_RAND_MASK);
    /* `move.w D4w,D5w` into a cleared D5: the quotient is kept 16-bit. */
    target = (uint16_t)(((uint32_t)slot_pages * (uint32_t)PMAP_$WS_RANDOM_SEED) >> 10);

    accumulator = 0;

    /* 0x00E13EE8: second walk, same slot range, no page_count != 0 guard */
    if (MMAP_WSL_HI_MARK >= 5) {
        for (slot = MMAP_WSL_HI_MARK; slot >= 5; slot--) {
            ws_hdr_t *ws = &MMAP_WSL[slot];

            if (ws->ws_floor < ws->page_count || total_pages == 0) {
                accumulator += ws->page_count;              /* 0x00E13F28 */
            }

            if ((uint32_t)target < accumulator) {            /* 0x00E13F2A */
                MMAP_$WS_SCAN(slot, 0, 1, PMAP_L_WS_SCAN_ALL);  /* 0x00E13F2E */
                break;
            }
        }
    }

    return true;
}

void PMAP_$PURIFIER_L(void)
{
    /* Frame slots, named after the A6 displacements they occupy. */
    uint32_t batch_pages[PMAP_L_BATCH_MAX];  /* -0x48 */
    uint32_t page_counts[5];                 /* -0x98 */
    uint16_t page_count;                     /* -0xA6 */
    int16_t  ec_wait_result;                 /* -0xA2 */
    status_$t status;                        /* -0x84 */
    pmap_qblk_t *qblk_main;                  /* -0x58 */
    pmap_qblk_t *qblk_alt[3];                /* -0x54 */
    int32_t  wait_value;                     /* -0x80 */
    uint32_t scan_time;                      /* -0x7C */
    uint32_t log_time;                       /* -0x78 */
    uint32_t shutdown_time;                  /* -0x74 */
    uint32_t carryover;                      /* -0x70 */
    uint32_t carryover_delta;                /* -0x6C */
    int32_t  steal_count;                    /* -0x64 */
    int32_t  prev_steal;                     /* -0x60 */
    boolean  did_advance;                    /* -0xAE */
    boolean  below_thresh;                   /* -0xAC */
    uid_t    log_uid;                        /* -0x08 */
    uint32_t total_pages;                    /* D6 */
    uint32_t scanned;                        /* D7 */
    int i;

    /* 0x00E13AAA: brief lock/unlock for initialization synchronization */
    ML_$LOCK(1);
    ML_$UNLOCK(1);

    /* 0x00E13AC6 */
    PROC1_$SET_LOCK(PROC_LOCK_ID);

    /* 0x00E13AD4: all three deadlines start at now + 0xE4 */
    scan_time = TIME_$CLOCKH + PMAP_L_INITIAL_DELAY;
    log_time = scan_time;
    shutdown_time = scan_time;

    /* 0x00E13AEC */
    wait_value = PMAP_$L_PURIFIER_EC.value + 1;

    /* 0x00E13AF6: divu.w - a 16-bit unsigned divide in the original */
    PMAP_$LOW_THRESH = (uint16_t)(MMAP_$PAGEABLE_PAGES_LOWER_LIMIT / 0x32);
    PMAP_$MID_THRESH = (uint16_t)(MMAP_$PAGEABLE_PAGES_LOWER_LIMIT / 0x14);

    carryover = 0;
    carryover_delta = 0;
    steal_count = 0;
    prev_steal = 0;

    PMAP_$INIT_TIMERS();        /* 0x00E13B22 */

    /* 0x00E13B28: main purifier loop - runs forever */
    for (;;) {
        /*
         * 0x00E13B28-0x00E13B4C: EC_$WAIT takes two 3-element arrays by
         * value (24 bytes of stack, cleaned with `lea (0x18,SP),SP`).  The
         * second eventcount is TIME_$CLOCKH itself, so the wait also ends
         * when the clock reaches the next scan deadline.
         */
        ec_wait_result = EC_$WAIT(
            (ec_$wait_ecs_t){{ &PMAP_$L_PURIFIER_EC,
                               (ec_$eventcount_t *)&TIME_$CLOCKH,
                               NULL }},
            (ec_$wait_vals_t){{ wait_value, (int32_t)scan_time, 0 }});

        ML_$LOCK(PMAP_LOCK_ID);         /* 0x00E13B50 */
        did_advance = false;            /* 0x00E13B5E */

        do {
            /* 0x00E13B62: inner drain loop */
            for (;;) {
                total_pages = MMAP_WSL[MMAP_WSL_POOL_FREE].page_count
                            + MMAP_WSL[MMAP_WSL_POOL_IMPURE].page_count
                            + MMAP_WSL[MMAP_WSL_POOL_PURE].page_count;

                /* 0x00E13B7A: shi - unsigned compare against MID_THRESH */
                below_thresh = (total_pages < (uint32_t)PMAP_$MID_THRESH)
                             ? true : false;

                /*
                 * 0x00E13B82-0x00E13B96: `not.b`/`and.b`/`bmi` leaves the
                 * loop when the pool is at or above the threshold and no
                 * carryover credit is left, and separately when there is
                 * nothing dirty to write.
                 */
                if ((below_thresh >= 0 && carryover == 0)
                    || MMAP_WSL[MMAP_WSL_POOL_DIRTY_LOCAL].page_count == 0) {
                    break;
                }

                /* 0x00E13B9A: below_thresh is recomputed for the call */
                MMAP_$GET_IMPURE(MMAP_WSL_POOL_DIRTY_LOCAL, batch_pages,
                                 (total_pages < (uint32_t)PMAP_$MID_THRESH)
                                     ? true : false,
                                 PMAP_L_BATCH_MAX, page_counts, &page_count);

                /* 0x00E13BC0/0x00E13BC4: D4w = returned, D7 = scanned */
                scanned = page_counts[0];

                if (page_count != 0) {          /* 0x00E13BC8 */
                    boolean batch_advanced;     /* D5b */

                    /*
                     * 0x00E13BD6-0x00E13C92: mark every page of the batch
                     * as "write in progress" and freeze its timestamps.
                     */
                    for (i = 0; i < (int)page_count; i++) {
                        uint32_t vpn = batch_pages[i];
                        mmape_t *page = MMAPE_FOR_VPN(vpn);
                        uint16_t seg = page->segment;
                        uint8_t  page_idx = page->seg_offset;
                        uint16_t *pmape = PMAPE_FOR_VPN(vpn);
                        aote_t *aote = NULL;

                        /*
                         * 0x00E13C06: flags2 bit 7 gates every AOTE access
                         * below.  The AOTE pointer is fetched here even
                         * though the first use is at 0x00E13C58.
                         */
                        if ((int8_t)page->flags2 >= 0) {
                            aote = pmap_$aote_for_segment(seg);

                            /*
                             * 0x00E13C28-0x00E13C2E: reads aote->vol_index
                             * into the high word of the frame's UID slot.
                             * Dead in the original - the slot is fully
                             * overwritten by UID_$NIL at 0x00E13D8E before
                             * anything reads it.  Kept for fidelity.
                             */
                            log_uid.high = (log_uid.high & 0x0000FFFFu)
                                         | ((uint32_t)aote->vol_index << 16);
                        }

                        /* 0x00E13C32: 1-based segment index */
                        PMAP_SEGMAP[seg][page_idx].flags |= PMAP_SEGMAP_WRITING;

                        /* 0x00E13C38-0x00E13C4A: PFT low word, bit 14 */
                        if ((pmape[1] & PFT_FLAG_MODIFIED) != 0) {
                            pmape[1] &= (uint16_t)~PFT_FLAG_MODIFIED;

                            /* 0x00E13C52: same flags2 test again */
                            if ((int8_t)page->flags2 >= 0) {
                                TIME_$ABS_CLOCK((clock_t *)&aote->dtu_high);
                                TIME_$CLOCK((clock_t *)&aote->len_high);
                                aote->dta_high = aote->len_high;
                                aote->dta_low = aote->len_low;
                                aote->flags |= 0x20;    /* bset.b #5 */
                            }
                        }

                        /*
                         * 0x00E13C86: a pending pool move recorded in
                         * mmape_t.priority is applied to wsl_index.
                         */
                        if (page->priority != 0) {
                            page->wsl_index = page->priority;
                        }
                    }

                    ML_$UNLOCK(PMAP_LOCK_ID);       /* 0x00E13C96 */

                    /* 0x00E13CA4 */
                    DISK_$GET_QBLKS((int16_t)page_count,
                                    (int32_t *)&qblk_main,
                                    (uint32_t *)qblk_alt);

                    /* 0x00E13CBE */
                    pmap_$fill_write_qblks((int32_t *)batch_pages,
                                           (uint32_t *)qblk_main,
                                           (int16_t)page_count);

                    /* 0x00E13CD0: `st -(SP)` - the boolean is TRUE */
                    DISK_$WRITE_MULTI(true, qblk_main, &status);
                    if (status != 0) {
                        CRASH_SYSTEM(&status);      /* 0x00E13CEA */
                    }

                    /*
                     * 0x00E13CF6-0x00E13D08: per-process pages-written
                     * counter, 0xE25D18 + (int16)(PROC1_$CURRENT << 4),
                     * the same slot pmap_$flush_write_batch increments.
                     */
                    PROC_STATS_BASE[PROC1_$CURRENT * 4 + 2] +=
                        (uint32_t)page_count;   /* index truncated to 16 bits */

                    batch_advanced = false;         /* 0x00E13D0C */
                    ML_$LOCK(PMAP_LOCK_ID);         /* 0x00E13D0E */

                    /* 0x00E13D1E-0x00E13D7C: walk the result chain */
                    if (page_count != 0) {
                        pmap_qblk_t *qblk = qblk_main;

                        for (i = 0; i < (int)page_count; i++) {
                            uint32_t vpn;

                            pmap_$write_complete((int32_t)qblk->vpn,
                                                 &qblk->status);
                            vpn = qblk->vpn;        /* 0x00E13D36 */

                            if (qblk->status == 0) {
                                /* 0x00E13D40: write succeeded */
                                batch_advanced = true;
                                MMAP_$AVAIL(vpn);
                            } else if (MMAPE_FOR_VPN(vpn)->wsl_index
                                       == MMAP_WSL_POOL_DIRTY_LOCAL) {
                                /*
                                 * 0x00E13D52: the write failed but the page
                                 * is still in the local dirty pool.  Pull it
                                 * out of that pool, reassign it to the wired
                                 * pool so it cannot be reclaimed, and make it
                                 * available again.
                                 */
                                MMAP_$UNAVAIL_REMOV(vpn, true);
                                MMAPE_FOR_VPN(vpn)->wsl_index =
                                    WSL_INDEX_WIRED;    /* 0x00E13D68: 5 */
                                MMAP_$AVAIL(vpn);
                            }
                            /* other errors leave the page where it is */

                            qblk = qblk->next;      /* 0x00E13D78 */
                        }
                    }

                    /* 0x00E13D80 */
                    if (NETLOG_$OK_TO_LOG < 0) {
                        log_uid = UID_$NIL;         /* 0x00E13D8E */

                        NETLOG_$LOG_IT(
                            0x0D, (uint32_t *)&log_uid,
                            page_count,
                            (uint16_t)ec_wait_result,
                            (uint16_t)(qblk_main->log_info >> 16),
                            (uint16_t)(qblk_main->log_info),
                            (uint16_t)(qblk_alt[0]->log_info >> 16),
                            (uint16_t)(qblk_alt[0]->log_info));
                    }

                    /* 0x00E13DD0 */
                    DISK_$RTN_QBLKS((int16_t)page_count,
                                    (int32_t)(uintptr_t)qblk_main,
                                    (uint32_t)(uintptr_t)qblk_alt[0]);

                    if (batch_advanced < 0) {       /* 0x00E13DE4 */
                        EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC);
                        EC_$ADVANCE(&PMAP_$PAGES_EC);
                    }

                    /* 0x00E13E02: unconditional, even when false */
                    did_advance = batch_advanced;

                    /* 0x00E13E06 */
                    PMAP_$PUR_L_CNT += (uint32_t)page_count;
                }

                /* 0x00E13E0A: saturating carryover -= pages scanned */
                if (carryover < scanned) {
                    carryover = 0;
                } else {
                    carryover -= scanned;
                }
                /* 0x00E13E14 / 0x00E13E1C: back to 0x00E13B62 */
            }

            /* 0x00E13E20 */
            wait_value = PMAP_$L_PURIFIER_EC.value + 1;
            total_pages += MMAP_WSL[MMAP_WSL_POOL_DIRTY_LOCAL].page_count;

            /* 0x00E13FA2 / 0x00E13E34: working-set scanning when low */
            while (total_pages < PMAP_L_LOW_WATER) {
                if (!pmap_$purifier_ws_scan_pass(total_pages, &prev_steal)) {
                    break;                          /* 0x00E13ECC */
                }

                ML_$UNLOCK(PMAP_LOCK_ID);           /* 0x00E13F52 */
                TIME_$WAIT((uint16_t *)&pmap_l_relative_delay_type,
                           &PMAP_$SHORT_WAIT_DELAY, &status);
                ML_$LOCK(PMAP_LOCK_ID);             /* 0x00E13F76 */

                /* 0x00E13F84: all five pools this time */
                total_pages = MMAP_WSL[MMAP_WSL_POOL_FREE].page_count
                            + MMAP_WSL[MMAP_WSL_POOL_PURE].page_count
                            + MMAP_WSL[MMAP_WSL_POOL_IMPURE].page_count
                            + MMAP_WSL[MMAP_WSL_POOL_DIRTY_LOCAL].page_count
                            + MMAP_WSL[MMAP_WSL_POOL_DIRTY_RMT].page_count;
            }

            /* 0x00E13FAC: make sure waiters see at least one advance */
            if (did_advance >= 0) {
                EC_$ADVANCE(&PMAP_$PAGES_EC);
                did_advance = true;
            }

            /*
             * 0x00E13FC2: re-enter the drain loop while the pool is still
             * below the threshold and dirty pages remain.
             */
        } while (below_thresh < 0
                 && MMAP_WSL[MMAP_WSL_POOL_DIRTY_LOCAL].page_count != 0);

        /* 0x00E13FD2: periodic threshold adjustment (signed compare) */
        if ((int32_t)TIME_$CLOCKH >= (int32_t)scan_time) {
            int32_t total_steal;
            uint32_t steal_delta;

            scan_time += PMAP_L_SCAN_PERIOD;        /* 0x00E13FDE */
            carryover += carryover_delta;           /* 0x00E13FE4 */

            total_steal = (int32_t)MMAP_$STEAL_CNT + prev_steal;
            steal_delta = (uint32_t)(total_steal - steal_count);

            if (total_steal == steal_count) {       /* 0x00E13FFC */
                /* No stealing at all: let the scan interval grow. */
                PMAP_$WS_INTERVAL += PMAP_$WS_SCAN_DELTA;
                if (PMAP_$MAX_WS_INTERVAL < PMAP_$WS_INTERVAL) {
                    PMAP_$WS_INTERVAL = PMAP_$MAX_WS_INTERVAL;
                }
            } else if (steal_delta > PMAP_L_STEAL_HYSTERESIS) {  /* 0x00E14016 */
                /* Heavy stealing: halve the interval, clamped at the floor. */
                PMAP_$WS_INTERVAL >>= 1;
                if (PMAP_$MIN_WS_INTERVAL > PMAP_$WS_INTERVAL) {
                    PMAP_$WS_INTERVAL = PMAP_$MIN_WS_INTERVAL;
                }
            }

            steal_count = total_steal;              /* 0x00E14030 */
        }

        ML_$UNLOCK(PMAP_LOCK_ID);                   /* 0x00E14034 */

        /* 0x00E14042: periodic log flush and threshold recalculation */
        if (scan_time >= log_time) {
            uint32_t quotient;

            /* 0x00E1404E */
            carryover_delta = M$DIU$LLW(
                MMAP_WSL[MMAP_WSL_POOL_DIRTY_LOCAL].page_count + 0x0B, 0x0C);

            /* 0x00E1406A: LOW_THRESH slews half-way to limit/0x32 */
            quotient = M$DIU$LLW(MMAP_$PAGEABLE_PAGES_LOWER_LIMIT, 0x32);
            PMAP_$LOW_THRESH =
                (uint16_t)(((uint32_t)PMAP_$LOW_THRESH + quotient) >> 1);

            /* 0x00E1408C: MID_THRESH slews half-way to limit/0x14 */
            quotient = M$DIU$LLW(MMAP_$PAGEABLE_PAGES_LOWER_LIMIT, 0x14);
            PMAP_$MID_THRESH =
                (uint16_t)(((uint32_t)PMAP_$MID_THRESH + quotient) >> 1);

            /* 0x00E140AE */
            int32_t log_vpn = LOG_$UPDATE();
            if (log_vpn != 0) {
                int8_t saved_chksum = 0;

                ML_$LOCK(PMAP_LOCK_ID);

                /* 0x00E140C6: checksums are suppressed when not diskless */
                if (NETWORK_$DISKLESS >= 0) {
                    saved_chksum = DISK_$DO_CHKSUM;
                    DISK_$DO_CHKSUM = 0;
                }

                pmap_$write_page((uint32_t)log_vpn, &status, 0);

                if (NETWORK_$DISKLESS >= 0) {
                    DISK_$DO_CHKSUM = saved_chksum;
                }

                if (status != 0) {
                    LOG_$LOGFILE_PTR = 0;           /* 0x00E14104 */
                }

                ML_$UNLOCK(PMAP_LOCK_ID);
            }

            log_time = scan_time + PMAP_L_LOG_PERIOD;   /* 0x00E14118 */
        }

        /* 0x00E14122: periodic shutdown check */
        if (PMAP_$SHUTTING_DOWN_FLAG >= 0 && scan_time >= shutdown_time) {
            if (NETWORK_$DISKLESS >= 0) {
                CAL_$SHUTDOWN(&status);             /* 0x00E1413E */
                if (status != 0) {
                    CRASH_SYSTEM(&status);
                }
            }
            shutdown_time = scan_time + PMAP_L_SHUTDOWN_PERIOD;
        }
    }
}
