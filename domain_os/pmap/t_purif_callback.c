/*
 * PMAP_$T_PURIF_CALLBACK - Timer callback: age or purge one working set
 *
 * 0x00E143CC - 0x00E144F4 (298 bytes, A5 = 0xE24D44).  Re-emitted from the
 * disassembly 2026-09-27.  Wrong before: the "partial scan" mode was passed
 * to MMAP_$WS_SCAN as 0x00FF; the image pushes it with `st -(SP)`
 * (0x00E144C4), a byte in the HIGH half of the word, which MMAP_$WS_SCAN
 * tests with `tst.b / bpl` - so the word is 0xFF00 (negative).  The WSL
 * record is now addressed as ws_hdr_t.
 *
 * Each firing advances the scan slot (0xE254E4) round 5..0x45 (PMAP_$CURRENT_SLOT; no map symbol, tree name), bumps
 * PMAP_$T_PUR_SCANS, and for MMAP_WSL[slot]:
 *   - unless flags bit 5 (bit 13 of the first word) is set, max_pages :=
 *     MMAP_$PAGEABLE_PAGES - min(MMAP_$PAGEABLE_PAGES / 4, 0x800)
 *   - with pages, and ws_timestamp <= now - 0x1CA (signed): under the
 *     PMAP lock either MMAP_$PURGE (never scanned and pri_timestamp older
 *     than the cutoff) or scan_pos := page_count and MMAP_$WS_SCAN(slot,
 *     mode, 0x3FFFFF, 0x3FFFFF), full (mode 0, owner reset, ws_timestamp
 *     := now) when flags bit 6 is set, owner > PMAP_$WS_INTERVAL, or the
 *     priority stamp is newer than both the ws stamp and now - 0x26;
 *     partial (mode 0xFF00) otherwise; then EC_$ADVANCE(PMAP_$PAGES_EC).
 */

#include "pmap/pmap_internal.h"
#include "ec/ec.h"
#include "mmap/mmap.h"

#define PMAP_TP_SLOT_LAST       0x45        /* 0x00E143DA */
#define PMAP_TP_SLOT_FIRST      5           /* 0x00E143E2 */
#define PMAP_TP_MAX_LIMIT       0x800       /* 0x00E1441E */
#define PMAP_TP_IDLE_CUTOFF     0x1CA       /* 0x00E14442 */
#define PMAP_TP_RECENT_CUTOFF   0x26        /* 0x00E1449E */
#define PMAP_TP_SCAN_ALL        0x3FFFFF    /* 0x00E144B0 / 0x00E144BC */
#define PMAP_TP_MODE_FULL       0           /* clr.w -(SP) */
#define PMAP_TP_MODE_PARTIAL    ((int16_t)0xFF00)  /* st -(SP) */
#define WS_HDR_FLAG_BIT5        0x20        /* bit 13 of the first word */
#define WS_HDR_FLAG_BIT6        0x40        /* bit 14 of the first word */

void PMAP_$T_PURIF_CALLBACK(void)
{
    ws_hdr_t *ws;               /* A2 */
    uint32_t now;               /* D2 */
    int32_t cutoff;             /* D3 */
    uint32_t limit;

    /* 0x00E143DA - 0x00E143EE */
    if (PMAP_$DATA.current_slot == PMAP_TP_SLOT_LAST) {
        PMAP_$DATA.current_slot = PMAP_TP_SLOT_FIRST;
    } else {
        PMAP_$DATA.current_slot++;
    }
    PMAP_$DATA.t_pur_scans++;

    /* 0x00E143F2 - 0x00E1440A: 0xE232B0 + slot * 0x24 */
    ws = &MMAP_WSL[PMAP_$DATA.current_slot];
    now = TIME_$CLOCKH;

    /* 0x00E1440E - 0x00E14434 */
    if ((ws->flags & WS_HDR_FLAG_BIT5) == 0) {
        limit = MMAP_$PAGEABLE_PAGES >> 2;
        if (limit > PMAP_TP_MAX_LIMIT) {
            limit = PMAP_TP_MAX_LIMIT;
        }
        ws->max_pages = MMAP_$PAGEABLE_PAGES - limit;
    }

    /* 0x00E14438 - 0x00E1444C: signed `blt' */
    if (ws->page_count == 0) {
        return;
    }
    cutoff = (int32_t)(now - PMAP_TP_IDLE_CUTOFF);
    if (cutoff < (int32_t)ws->ws_timestamp) {
        return;
    }

    /* 0x00E14450 */
    ML_$LOCK(PMAP_LOCK_ID);
    /* 0x00E1445E - 0x00E14478 */
    if (ws->ws_timestamp == 0 && (int32_t)ws->pri_timestamp < cutoff) {
        MMAP_$PURGE(PMAP_$DATA.current_slot);
    } else {
        int16_t mode;
        /* 0x00E1447A - 0x00E144A6 */
        ws->scan_pos = ws->page_count;
        if ((ws->flags & WS_HDR_FLAG_BIT6) != 0 ||
            ws->owner > PMAP_$DATA.ws_interval ||
            ((int32_t)ws->pri_timestamp > (int32_t)ws->ws_timestamp &&
             (int32_t)(now - PMAP_TP_RECENT_CUTOFF) > (int32_t)ws->pri_timestamp)) {
            /* 0x00E144A8 - 0x00E144BA */
            ws->owner = 0;
            ws->ws_timestamp = now;
            mode = PMAP_TP_MODE_FULL;
        } else {
            /* 0x00E144BC - 0x00E144C4 */
            mode = PMAP_TP_MODE_PARTIAL;
        }
        MMAP_$WS_SCAN(PMAP_$DATA.current_slot, mode, PMAP_TP_SCAN_ALL, PMAP_TP_SCAN_ALL);
    }
    /* 0x00E144D4 - 0x00E144E6 */
    ML_$UNLOCK(PMAP_LOCK_ID);
    EC_$ADVANCE(&PMAP_$DATA.pages_ec);
}
