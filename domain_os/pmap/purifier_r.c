/*
 * PMAP_$PURIFIER_R - Remote page purifier process
 *
 * Background daemon that writes dirty pages belonging to remote (network)
 * objects.  It wakes on PMAP_$R_PURIFIER_EC or on the clock, drains the
 * "dirty, remote" page pool one page at a time, and goes back to sleep.
 *
 * This procedure never returns.
 *
 * Original address: 0x00e1416c
 * Original size: 582 bytes
 *
 * Module base: "lea (0xe24d44).l,A5" at 0x00E14174, so
 *   (0x748,A5) = 0xE2548C = PMAP_$PUR_R_CNT
 *   (0x750,A5) = 0xE25494 = PMAP_$PAGES_EC
 *   (0x75c,A5) = 0xE254A0 = PMAP_$R_PURIFIER_EC
 *   (0x78a,A5) = 0xE254CE = PMAP_$MID_THRESH (a 16-bit word)
 */

#include "pmap/pmap_internal.h"
#include "math/math.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"

/*
 * 0x00E141AA / 0x00E14200: the scan deadline and the carryover-recalculation
 * deadline both advance by 0xE4 clock ticks.
 */
#define PMAP_R_RECALC_INTERVAL  0xE4

/* 0x00E14226: "moveq #0x26,D1 / add.l D1,(-0x60,A6)" */
#define PMAP_R_SCAN_INTERVAL    0x26

/* 0x00E14214: M$DIU$LLW(WSL[dirty-remote] + 5, 6) */
#define PMAP_R_CARRYOVER_BIAS   5
#define PMAP_R_CARRYOVER_DIVISOR 6

/* 0x00E14274: MMAP_$GET_IMPURE is asked for a single page per iteration. */
#define PMAP_R_BATCH_MAX        1

/*
 * mmape_t.wsl_index values the retry path tests and writes
 * (0x00E1432E "cmpi.b #0x4" and 0x00E14364 "move.b #0x5").
 */
#define PMAP_R_WSL_DIRTY_RMT    0x04
#define PMAP_R_WSL_WIRED        0x05

/*
 * Write statuses at 0x00E1433A-0x00E14350 that are NOT retried: the page is
 * left in its current pool for someone else to deal with.
 */
#define PMAP_R_STATUS_NO_RETRY_1  0x00030001
#define PMAP_R_STATUS_NO_RETRY_2  0x00030005
#define PMAP_R_STATUS_NO_RETRY_3  0x000F0001

void PMAP_$PURIFIER_R(void)
{
    uint32_t batch_pages[PMAP_R_BATCH_MAX];   /* (-0x40,A6) */
    uint32_t scanned;                         /* (-0x58,A6), read into D6 */
    uint16_t page_count;                      /* (-0x66,A6) */
    status_$t status;                         /* (-0x4c,A6) */
    int32_t wait_value;                       /* (-0x64,A6) */
    uint32_t scan_time;                       /* (-0x60,A6) */
    uint32_t recalc_time;                     /* (-0x5c,A6) */
    uint32_t carryover;                       /* D5 */
    uint32_t carryover_delta;                 /* (-0x44,A6) */
    uint32_t total_pages;                     /* D2 */
    int i;

    /*
     * 0x00E1417A-0x00E14194: take and immediately drop the lock, so the
     * daemon cannot start before whoever is holding it during boot is done.
     */
    ML_$LOCK(1);
    ML_$UNLOCK(1);

    PROC1_$SET_LOCK(PROC_LOCK_ID);            /* 0x00E1419C */

    /* 0x00E141A4-0x00E141B4 */
    scan_time = TIME_$CLOCKH + PMAP_R_RECALC_INTERVAL;
    recalc_time = scan_time;

    /* 0x00E141B8 */
    wait_value = PMAP_$R_PURIFIER_EC.value + 1;

    carryover = 0;                            /* 0x00E141C2: clr.l D5 */
    carryover_delta = 0;                      /* 0x00E141C4 */

    /* 0x00E141C8: main loop - runs forever (0x00E143AE branches back here) */
    for (;;) {
        /*
         * 0x00E141C8-0x00E141EA: EC_$WAIT takes two 3-element arrays by
         * value (24 bytes, cleaned with "lea (0x18,SP),SP").  The second
         * eventcount is TIME_$CLOCKH, so the wait also ends when the clock
         * reaches the next scan deadline.  The result is discarded here.
         */
        (void)EC_$WAIT(
            (ec_$wait_ecs_t){{ &PMAP_$R_PURIFIER_EC,
                               (ec_$eventcount_t *)&TIME_$CLOCKH,
                               NULL }},
            (ec_$wait_vals_t){{ wait_value, (int32_t)scan_time, 0 }});

        /* 0x00E141EE: signed compare (bgt) against the clock */
        if ((int32_t)scan_time <= (int32_t)TIME_$CLOCKH) {
            /* 0x00E141FA: unsigned compare (bcs) against the recalc deadline */
            if (recalc_time <= scan_time) {
                recalc_time = scan_time + PMAP_R_RECALC_INTERVAL;
                carryover_delta = M$DIU$LLW(
                    MMAP_WSL[MMAP_WSL_POOL_DIRTY_RMT].page_count
                        + PMAP_R_CARRYOVER_BIAS,
                    PMAP_R_CARRYOVER_DIVISOR);      /* 0x00E1421A */
            }

            scan_time += PMAP_R_SCAN_INTERVAL;      /* 0x00E14226 */
            carryover += carryover_delta;           /* 0x00E1422C */
        }

        ML_$LOCK(PMAP_LOCK_ID);                     /* 0x00E14230 */

        /* 0x00E1423E: the three "clean" pools */
        total_pages = MMAP_WSL[MMAP_WSL_POOL_FREE].page_count
                    + MMAP_WSL[MMAP_WSL_POOL_IMPURE].page_count
                    + MMAP_WSL[MMAP_WSL_POOL_PURE].page_count;

        /*
         * 0x00E14250-0x00E14266: keep draining while there is something
         * dirty and either carryover credit is left or the clean pools are
         * still below the mid threshold.  Both comparisons are unsigned.
         */
        while (MMAP_WSL[MMAP_WSL_POOL_DIRTY_RMT].page_count != 0
               && (carryover != 0
                   || total_pages < (uint32_t)PMAP_$MID_THRESH)) {

            /* 0x00E1426A-0x00E14292 */
            MMAP_$GET_IMPURE(MMAP_WSL_POOL_DIRTY_RMT, batch_pages,
                             (total_pages < (uint32_t)PMAP_$MID_THRESH)
                                 ? true : false,   /* 0x00E14280: shi */
                             PMAP_R_BATCH_MAX, &scanned, &page_count);

            /* 0x00E1429E: skip the body entirely when nothing came back */
            if (page_count != 0) {
                /* 0x00E142AA: dbf over page_count entries */
                for (i = 0; i < (int)page_count; i++) {
                    uint32_t vpn = batch_pages[i];
                    mmape_t *page = MMAPE_FOR_VPN(vpn);
                    uint16_t seg = page->segment;         /* 0x00E142CA */
                    uint8_t page_idx = page->seg_offset;  /* 0x00E142D6 */
                    uint8_t priority;

                    /*
                     * 0x00E142C4-0x00E142E0: "lea 0xED5000 + seg*0x80, A1 /
                     * ... / bset.b #7,(-0x80,A1)" - the array base is
                     * 0xED4F80 and the segment index is 1-based.
                     */
                    PMAP_SEGMAP[seg][page_idx].flags |= PMAP_SEGMAP_WRITING;

                    /*
                     * 0x00E142F0: clear the MMU modified bit unconditionally
                     * (no test, unlike PMAP_$PURIFIER_L).
                     */
                    PMAPE_FOR_VPN(vpn)[1] &= (uint16_t)~PFT_FLAG_MODIFIED;

                    /*
                     * 0x00E142F6-0x00E14300: remember which pool the page
                     * came from when its priority says it is worth retrying.
                     * "cmpi.b #5 / bls" is an unsigned compare.
                     */
                    priority = page->priority;
                    if (priority > 5) {
                        page->wsl_index = priority;
                    }

                    /* 0x00E14304-0x00E14312: write, sync flag TRUE */
                    pmap_$write_page(vpn, &status, true);

                    if (status == status_$ok) {           /* 0x00E14316 */
                        EC_$ADVANCE(&PMAP_$PAGES_EC);     /* 0x00E14320 */
                        MMAP_$AVAIL(vpn);                 /* 0x00E1436C */
                    } else if (page->wsl_index == PMAP_R_WSL_DIRTY_RMT
                               && status != PMAP_R_STATUS_NO_RETRY_1
                               && status != PMAP_R_STATUS_NO_RETRY_2
                               && status != PMAP_R_STATUS_NO_RETRY_3) {
                        /*
                         * 0x00E14352-0x00E14364: recoverable failure - pull
                         * the page out of its list, park it in the wired
                         * pool and make it available again.
                         */
                        MMAP_$UNAVAIL_REMOV(vpn, true);
                        page->wsl_index = PMAP_R_WSL_WIRED;
                        MMAP_$AVAIL(vpn);                 /* 0x00E1436C */
                    }
                    /*
                     * Every other failure falls straight to 0x00E14374:
                     * the page keeps its current state and MMAP_$AVAIL is
                     * NOT called.
                     */
                }
            }

            /* 0x00E1437A-0x00E14382: 16-bit count, zero-extended */
            PMAP_$PUR_R_CNT += (uint32_t)page_count;

            /*
             * 0x00E14386-0x00E14392: "cmp.l D5,D6 / bls" - when the number
             * of pages scanned is at or below the carryover credit, spend
             * that much credit; otherwise the credit is exhausted.
             */
            if (scanned <= carryover) {
                carryover -= scanned;
            } else {
                carryover = 0;
            }

            /* 0x00E1438C/0x00E14392 both branch back to 0x00E1423E */
            total_pages = MMAP_WSL[MMAP_WSL_POOL_FREE].page_count
                        + MMAP_WSL[MMAP_WSL_POOL_IMPURE].page_count
                        + MMAP_WSL[MMAP_WSL_POOL_PURE].page_count;
        }

        /* 0x00E14396 */
        wait_value = PMAP_$R_PURIFIER_EC.value + 1;

        ML_$UNLOCK(PMAP_LOCK_ID);               /* 0x00E143A6 */
    }
}
