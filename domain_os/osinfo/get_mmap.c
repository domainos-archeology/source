/*
 * OSINFO_$GET_MMAP - query and tune the memory-management subsystem
 *
 * Original address: 0x00E5C694
 * Size: 864 bytes (0x00E5C694 .. 0x00E5C9FF); the five-entry jump table
 * for the set operations sits inside the body at 0x00E5C6D6.
 *
 * Byte 1 of the record the first argument points at is a flag set; each
 * flag runs one block, in the order below (SET_PARAMS, FIND_PAGE, GET_PID,
 * GET_GLOBAL, GET_COUNTERS, GET_WS_LIST, GET_WS_INFO).  FIND_PAGE's two
 * failure paths ("invalid asid" and "no page at or above the start") leave
 * the routine at once, skipping the later blocks.
 *
 * Frame (link.w A6,-0x18; D2-D7/A2-A4 saved):
 *   (0x8,A6)   flags_p    pointer -> D3; `btst.b #n,(0x1,A1)` on it
 *   (0xc,A6)   counters   pointer -> D4 (osinfo_paging_counters_t)
 *   (0x10,A6)  info       pointer -> A2 (osinfo_global_info_t)
 *   (0x14,A6)  ws_data    pointer, 12-byte entries (GET_WS_INFO)
 *   (0x18,A6)  ws_list    pointer, 4-byte entries (GET_WS_LIST)
 *   (0x1c,A6)  uid_out    pointer -> D5
 *   (0x20,A6)  status     pointer -> D6, cleared first
 *   (-0x8,A6)  wsl_index  word, MMAP_$WS_OWNER[asid - 1]
 *
 * Jump table at 0x00E5C6D6 (word offsets 0x0a 0x26 0x32 0xa4 0x6c):
 *   op 0 -> 0x00E5C6E0  PMAP_$WS_INTERVAL = PMAP_$MAX_WS_INTERVAL = info+0x2a,
 *                       PMAP_$MIN_WS_INTERVAL = info+0x2e
 *   op 1 -> 0x00E5C6FC  PMAP_$IDLE_INTERVAL = info+0x28
 *   op 2 -> 0x00E5C708  MMAP_$SET_WS_MAX(MMAP_$WS_OWNER[asid-1], info+0x28, status)
 *   op 3 -> 0x00E5C77A  PMAP_$PURGE_WS(asid, true)
 *   op 4 -> 0x00E5C742  MMAP_$WSL[MMAP_$WS_OWNER[asid-1]].ws_floor = info+0x28
 * where asid = info+0x2e, replaced by PROC1_$CURRENT when 0 or above 0x40
 * (`cmpi.w #0x40` / `bls`).  An op of 5 or more does nothing.
 *
 * Re-emitted from the disassembly 2026-09-27: the previous C took the
 * first argument by value; it is a pointer to the record whose byte 1
 * holds the flags.
 */

#include "osinfo/osinfo_internal.h"
#include "mmap/mmap.h"
#include "pmap/pmap.h"
#include "ast/ast.h"
#include "proc2/proc2.h"

/* `movea.l #0xec5400,A4` at 0x00E5C81E: the AST, 0x14-byte entries whose
 * AOTE pointer is at +4 (read as (-0x10,A4) from the NEXT entry's base) */
#define OSINFO_AST_STRIDE   0x14

/*
 * Selects the current process when the caller's ASID word is 0 or above
 * 0x40 (0x00E5C708 .. 0x00E5C71A and its two copies).
 */
static uint16_t osinfo_$mmap_asid(uint16_t asid)
{
    if (asid == 0 || asid > 0x40) {
        return PROC1_$CURRENT;
    }
    return asid;
}

void OSINFO_$GET_MMAP(void *flags_p, void *counters, void *info,
                      void *ws_data, void *ws_list, void *uid_out,
                      status_$t *status)
{
    const uint8_t *flags_rec = (const uint8_t *)flags_p;
    uint8_t flag_byte;
    osinfo_global_info_t *global_info = (osinfo_global_info_t *)info;
    osinfo_paging_counters_t *paging = (osinfo_paging_counters_t *)counters;
    uint16_t *ws_list_ptr = (uint16_t *)ws_list;
    uint32_t *ws_data_ptr = (uint32_t *)ws_data;
    uint16_t asid;                  /* D0w / D1w / D2w */
    uint16_t wsl_index;             /* (-0x8,A6) / D0w */
    uint32_t ppn;                   /* D2 */
    mmape_t *page_entry;            /* A1 / A0 */
    int16_t count;                  /* D0w */
    int16_t i;

    /* 0x00E5C6B0 clr.l (A0) */
    *status = status_$ok;

    /* `btst.b #n,(0x1,A1)` with A1 = flags_p */
    flag_byte = flags_rec[1];

    /* 0x00E5C6B6 btst.b #0x5: SET_PARAMS */
    if (flag_byte & MMAP_FLAG_SET_PARAMS) {
        /* 0x00E5C6C0 .. 0x00E5C6D2: `cmpi.w #0x5` / `bcc` then the jump table */
        if (global_info->set_op < 5) {
            switch (global_info->set_op) {
            case MMAP_SET_WS_INTERVAL:                          /* 0x00E5C6E0 */
                PMAP_$WS_INTERVAL = *(uint16_t *)((uint8_t *)info + 0x2a);
                PMAP_$MAX_WS_INTERVAL = *(uint16_t *)((uint8_t *)info + 0x2a);
                PMAP_$MIN_WS_INTERVAL = global_info->asid;      /* the word at +0x2e */
                break;

            case MMAP_SET_IDLE_INTERVAL:                        /* 0x00E5C6FC */
                PMAP_$IDLE_INTERVAL = global_info->set_value;
                break;

            case MMAP_SET_WS_MAX:                               /* 0x00E5C708 */
                asid = osinfo_$mmap_asid(global_info->asid);
                wsl_index = MMAP_$WS_OWNER[asid - 1];           /* (-0x2,A3,D1w) */
                MMAP_$SET_WS_MAX(wsl_index, global_info->set_value, status);
                break;

            case MMAP_PURGE_WS:                                 /* 0x00E5C77A */
                asid = osinfo_$mmap_asid(global_info->asid);
                /* `st -(SP)` / `move.w D2w,-(SP)`: the boolean lands in the
                 * high byte of its word slot, which PMAP_$PURGE_WS reads as
                 * a byte (`move.b (0xa,A6),D3b` at 0x00E146C8); the int16_t
                 * prototype carries it as 0xFF00. */
                PMAP_$PURGE_WS((int16_t)asid, (int16_t)0xFF00);
                break;

            case MMAP_SET_WS_LIMIT:                             /* 0x00E5C742 */
                asid = osinfo_$mmap_asid(global_info->asid);
                wsl_index = MMAP_$WS_OWNER[asid - 1];
                /* 0x00E5C762 .. 0x00E5C772: 0xE232B0 + index*0x24 + 0x20 */
                MMAP_$WSL[wsl_index].ws_floor = global_info->set_value;
                break;
            }
        }
    }

    /* 0x00E5C79A btst.b #0x6: FIND_PAGE */
    if (flag_byte & MMAP_FLAG_FIND_PAGE) {
        /* 0x00E5C7A4 .. 0x00E5C7B8: `cmpi.w #0x45` / `bls` */
        asid = global_info->asid;
        if (asid == 0 || asid > 0x45) {
            *status = status_$os_info_invalid_asid;
            return;                                             /* -> 0x00E5C9F6 */
        }

        /* 0x00E5C7BC .. 0x00E5C7CA: not below MMAP_$LPPN */
        ppn = global_info->set_value;
        if (ppn < MMAP_$LPPN) {
            ppn = MMAP_$LPPN;
        }

        /* 0x00E5C7CC .. 0x00E5C7DA: above MMAP_$HPPN -> -1, and out */
        if (ppn > MMAP_$HPPN) {
            global_info->set_value = 0xFFFFFFFFu;
            return;                                             /* -> 0x00E5C9F6 */
        }

        /* 0x00E5C7DE .. 0x00E5C850: `movea.l #0xeb4800,A0` with -0x2000
         * displacements = MMAPE_BASE (0xEB2800) + ppn*16; the loop runs
         * while ppn <= MMAP_$HPPN */
        page_entry = &MMAPE_BASE[ppn];
        while (ppn <= MMAP_$HPPN) {
            /* 0x00E5C7F2 `tst.b (-0x1ffb,A0)` / bpl: flags1 sign;
             * 0x00E5C7FA `move.b (-0x1ffc,A0),D0b` / cmp.w D1w: wsl_index */
            if ((int8_t)page_entry->flags1 < 0 &&
                (uint16_t)page_entry->wsl_index == asid) {
                /* 0x00E5C802 `tst.b (-0x1ff7,A0)` / bpl: flags2 sign = wired */
                if ((int8_t)page_entry->flags2 < 0) {
                    *status = status_$os_info_page_wired;      /* 0x00E5C80A */
                } else {
                    *status = status_$os_info_page_found;      /* 0x00E5C814 */
                    /* 0x00E5C81A .. 0x00E5C83C: ASTE index at +2; the
                     * AOTE pointer of ASTE_BASE[idx-1] (+4 = idx*0x14-0x10);
                     * its uid at +0x10 -> *uid_out */
                    *(uid_t *)uid_out =
                        ASTE_BASE[(int16_t)page_entry->segment - 1].aote->uid;
                }
                ppn++;                                          /* 0x00E5C840 */
                break;                                          /* -> 0x00E5C852 */
            }
            ppn++;                                              /* 0x00E5C844 */
            page_entry++;
        }
        global_info->set_value = ppn;                           /* 0x00E5C852 */
    }

    /* 0x00E5C858 btst.b #0x4: GET_PID */
    if (flag_byte & MMAP_FLAG_GET_PID) {
        global_info->pid = PROC2_$GET_PID((uid_t *)uid_out, status);   /* 0x00E5C860 */
    }

    /* 0x00E5C872 btst.b #0x1: GET_GLOBAL */
    if (flag_byte & MMAP_FLAG_GET_GLOBAL) {
        global_info->real_pages = MMAP_$REAL_PAGES;              /* 0xE23CA0 */
        global_info->pageable_lower_limit = MMAP_$PAGEABLE_PAGES;/* 0xE23C98 */
        global_info->remote_pages = MMAP_$REMOTE_PAGES;          /* 0xE23C9C */
        global_info->wsl_hi_mark = MMAP_$WSL_HI_MARK;            /* 0xE23CA6 */
        /* 0x00E5C898 .. 0x00E5C8B8: the five pool page counts */
        global_info->ws_data[0] = MMAP_$WSL[0].page_count;       /* 0xE232B4 */
        global_info->ws_data[1] = MMAP_$WSL[1].page_count;       /* 0xE232D8 */
        global_info->ws_data[2] = MMAP_$WSL[2].page_count;       /* 0xE232FC */
        global_info->ws_data[3] = MMAP_$WSL[3].page_count;       /* 0xE23320 */
        global_info->ws_data[4] = MMAP_$WSL[4].page_count;       /* 0xE23344 */
        global_info->ws_interval = PMAP_$WS_INTERVAL;            /* 0xE254D8 */
    }

    /* 0x00E5C8C8 btst.b #0x0: GET_COUNTERS (A1 still = flags_p) */
    if (flag_byte & MMAP_FLAG_GET_COUNTERS) {
        paging->pur_l_cnt = PMAP_$PUR_L_CNT;                     /* 0xE25490 */
        paging->pur_r_cnt = PMAP_$PUR_R_CNT;                     /* 0xE2548C */
        paging->page_flt_cnt = AST_$PAGE_FLT_CNT;                /* 0xE1E0DC */
        paging->ws_flt_cnt = AST_$WS_FLT_CNT;                    /* 0xE1E0D8 */
        paging->t_pur_scans = PMAP_$T_PUR_SCANS;                 /* 0xE25488 */
        paging->alloc_cnt = MMAP_$ALLOC_CNT;                     /* 0xE232AC */
        paging->alloc_pages = MMAP_$ALLOC_PAGES;                 /* 0xE232A8 */
        paging->steal_cnt = MMAP_$STEAL_CNT;                     /* 0xE232A4 */
        paging->ws_overflow = MMAP_$WS_OVERFLOW;                 /* 0xE232A0 */
        paging->ws_scan_cnt = MMAP_$WS_SCAN_CNT;                 /* 0xE2329C */
        /* +0x28 is not written */
        paging->ast_alloc_cnt = AST_$ALLOC_CNT;                  /* 0xE1E0E4 */
        paging->alloc_too_few = AST_$ALLOC_TOO_FEW_CNT;          /* 0xE1E0E0 */
        paging->reclaim_shar_cnt = MMAP_$RECLAIM_SHAR_CNT;       /* 0xE23298 */
        paging->reclaim_pur_cnt = MMAP_$RECLAIM_PUR_CNT;         /* 0xE23294 */
        paging->ws_remove = MMAP_$WS_REMOVE;                     /* 0xE23290 */
        paging->scan_fract = PMAP_$SCAN_FRACT;                   /* 0xE254CC */
    }

    /* 0x00E5C952 btst.b #0x3: GET_WS_LIST */
    if (flag_byte & MMAP_FLAG_GET_WS_LIST) {
        /* 0x00E5C95A .. 0x00E5C966: `moveq #0x40` / `cmp.w (0x30,A2)` /
         * `bcs` - unsigned clamp to 0x40, stored back */
        count = (int16_t)global_info->ws_list_count;
        if ((uint16_t)count > 0x40) {
            count = 0x40;
        }
        global_info->ws_list_count = (uint16_t)count;
        if (count != 0) {
            /* 0x00E5C96C .. 0x00E5C99E: count entries of
             * { MMAP_$WS_OWNER[i] (0xE23CA8), PROC1_$TYPE[i + 1] (A1 = 0xE2612E,
             * `move.w (-0x2,A1)`; PROC1_$TYPE[0] is at 0xE2612A) } */
            for (i = 0; i < count; i++) {
                ws_list_ptr[i * 2] = MMAP_$WS_OWNER[i];
                ws_list_ptr[i * 2 + 1] = PROC1_$TYPE[i + 1];   /* 0xE2612C = entry 1 */
            }
        }
    }

    /* 0x00E5C9A4 btst.b #0x2: GET_WS_INFO */
    if (flag_byte & MMAP_FLAG_GET_WS_INFO) {
        /* 0x00E5C9AC `cmpi.w #0x5,(0x00e23ca6).l` / `bcs`: nothing below 5 */
        if (MMAP_$WSL_HI_MARK >= 5) {
            /* 0x00E5C9B6 .. 0x00E5C9F2: hi_mark - 5 + 1 entries, from
             * 0xE232B0 + 0xB4 (= MMAP_$WSL[5]) in 0x24-byte steps, 12 bytes
             * out each: +0x04, +0x18, +0x1c */
            count = (int16_t)(MMAP_$WSL_HI_MARK - 5);
            for (i = 0; i <= count; i++) {
                const ws_hdr_t *src = &MMAP_$WSL[5 + i];
                ws_data_ptr[i * 3] = src->page_count;
                ws_data_ptr[i * 3 + 1] = src->pri_timestamp;
                ws_data_ptr[i * 3 + 2] = src->ws_timestamp;
            }
        }
    }
}
