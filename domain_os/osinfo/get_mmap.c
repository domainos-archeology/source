// OSINFO_$GET_MMAP - Get/set memory map information
// Address: 0x00e5c694
// Size: 864 bytes
//
// Complex function to get and set various memory management parameters.
// Flags in byte 1 of param_1 control which operations are performed.

#include "osinfo/osinfo_internal.h"
#include "mmap/mmap.h"
#include "pmap/pmap.h"
#include "ast/ast.h"
#include "proc2/proc2.h"

void OSINFO_$GET_MMAP(int flags, void *counters, void *info,
                      void *ws_data, void *ws_list, void *uid_out,
                      status_$t *status)
{
    uint8_t flag_byte;
    osinfo_global_info_t *global_info = (osinfo_global_info_t *)info;
    osinfo_paging_counters_t *paging = (osinfo_paging_counters_t *)counters;
    uint16_t *ws_list_ptr = (uint16_t *)ws_list;
    uint32_t *ws_data_ptr = (uint32_t *)ws_data;
    uint16_t asid;
    uint32_t ppn;
    uint8_t *page_entry;
    short count;
    short i;

    *status = status_$ok;

    // Extract flags from byte 1 of the flags parameter
    flag_byte = (uint8_t)((flags >> 8) & 0xFF);

    // Handle SET_PARAMS operation
    if (flag_byte & MMAP_FLAG_SET_PARAMS) {
        switch (global_info->set_op) {
        case MMAP_SET_WS_INTERVAL:
            // Set working set interval parameters
            PMAP_$MAX_WS_INTERVAL = *((uint16_t *)((char *)info + 0x2a));
            PMAP_$WS_INTERVAL = PMAP_$MAX_WS_INTERVAL;
            PMAP_$MIN_WS_INTERVAL = *((uint16_t *)((char *)info + 0x2e));
            break;

        case MMAP_SET_IDLE_INTERVAL:
            // Set idle interval
            PMAP_$IDLE_INTERVAL = global_info->set_value;
            break;

        case MMAP_SET_WS_MAX:
            // Set working set max for process
            asid = *((uint16_t *)((char *)info + 0x2e));
            if (asid == 0 || asid > 0x40) {
                asid = PROC1_$CURRENT;
            }
            MMAP_$SET_WS_MAX(MMAP_$WS_OWNER[asid - 1],
                             global_info->set_value, status);
            break;

        case MMAP_PURGE_WS:
            // Purge working set for process
            asid = *((uint16_t *)((char *)info + 0x2e));
            if (asid == 0 || asid > 0x40) {
                asid = PROC1_$CURRENT;
            }
            PMAP_$PURGE_WS(asid, 0xFF00);  // Full purge
            break;

        case MMAP_SET_WS_LIMIT:
            // Set working set limit
            asid = *((uint16_t *)((char *)info + 0x2e));
            if (asid == 0 || asid > 0x40) {
                asid = PROC1_$CURRENT;
            }
            {
                short ws_index = MMAP_$WS_OWNER[asid - 1];
                /*
                 * 00e5c762  movea.l #0xe232b0,A3        ; MMAP_WSL
                 * 00e5c76a  lsl.w #0x2,D1w / lsl.w #0x3,D2w / add.w D2w,D1w
                 * 00e5c772  move.l (0x28,A2),(0x20,A3,D1w*0x1)
                 * i.e. MMAP_WSL[ws_index].ws_floor.
                 */
                MMAP_WSL[ws_index].ws_floor = global_info->set_value;
            }
            break;
        }
    }

    // Handle FIND_PAGE operation
    if (flag_byte & MMAP_FLAG_FIND_PAGE) {
        asid = global_info->asid;
        if (asid == 0 || asid > 0x45) {
            *status = status_$os_info_invalid_asid;
            return;
        }

        ppn = global_info->set_value;  // Starting physical page number
        if (ppn < MMAP_$LPPN) {
            ppn = MMAP_$LPPN;
        }

        if (ppn > MMAP_$HPPN) {
            global_info->set_value = 0xFFFFFFFF;  // No page found
            return;
        }

        // Search through the MMAPE array (0xEB2800, 16 bytes per physical
        // page).  Original: movea.l #0xeb4800,A0 with -0x2000-relative
        // offsets (-0x1ffb = +5, -0x1ffc = +4, -0x1ff7 = +9, -0x1ffe = +2).
        page_entry = (uint8_t *)MMAPE_BASE + ppn * 0x10;
        while (ppn <= MMAP_$HPPN) {
            // Check if page is valid and belongs to this ASID
            // page_entry[-0x1ffb] = offset 5 in 0x10 byte entry (flags)
            // page_entry[-0x1ffc] = offset 4 in 0x10 byte entry (asid)
            // 0xE5C7F2 / 0xE5C802 are "tst.b ... / bpl": Domain booleans
            // tested for their sign, not masked.
            if (((int8_t)page_entry[5] < 0) &&   // Page valid
                (page_entry[4] == asid)) {       // Matches ASID

                // Check if page is wired
                if ((int8_t)page_entry[9] < 0) {  // page_entry[-0x1ff7]
                    *status = status_$os_info_page_wired;
                } else {
                    *status = status_$os_info_page_found;

                    // Get UID via the ASTE -> AOTE link.
                    // Original: move.w (-0x1ffe,A0),D0 (ASTE index at entry+2);
                    //   A4 = 0xEC5400 + idx*0x14; movea.l (-0x10,A4),A1
                    //   i.e. the aote pointer (+0x04) of ASTE_BASE[idx-1];
                    //   0xE5C836 "lea (0x10,A1),A1" then two longword moves.
                    //
                    // The eight bytes at aote + 0x10 are aote_t.uid, the
                    // object UID that ast_$lookup_aote_by_uid hashes on
                    // (bead source-d2b).
                    short ast_index = *(short *)(page_entry + 2);
                    aste_t *aste = &ASTE_BASE[ast_index - 1];
                    *(uid_t *)uid_out = aste->aote->uid;
                }

                ppn++;
                break;
            }
            ppn++;
            page_entry += 0x10;
        }
        global_info->set_value = ppn;  // Next page to search from
    }

    // Handle GET_PID operation
    if (flag_byte & MMAP_FLAG_GET_PID) {
        global_info->pid = PROC2_$GET_PID(uid_out, status);
    }

    // Handle GET_GLOBAL operation
    if (flag_byte & MMAP_FLAG_GET_GLOBAL) {
        global_info->real_pages = MMAP_$REAL_PAGES;
        global_info->pageable_lower_limit = MMAP_$PAGEABLE_PAGES;
        global_info->remote_pages = MMAP_$REMOTE_PAGES;
        global_info->wsl_hi_mark = MMAP_WSL_HI_MARK;   /* move.w (0xE23CA6) */

        /* Copy the five global pool page counts (0x00E5C898-0x00E5C8B8) */
        global_info->ws_data[0] = MMAP_WSL[0].page_count;  /* 0xe232b4 */
        global_info->ws_data[1] = MMAP_WSL[1].page_count;  /* 0xe232d8 */
        global_info->ws_data[2] = MMAP_WSL[2].page_count;  /* 0xe232fc */
        global_info->ws_data[3] = MMAP_WSL[3].page_count;  /* 0xe23320 */
        global_info->ws_data[4] = MMAP_WSL[4].page_count;  /* 0xe23344 */

        global_info->ws_interval = PMAP_$WS_INTERVAL;
    }

    // Handle GET_COUNTERS operation
    if (flag_byte & MMAP_FLAG_GET_COUNTERS) {
        paging->pur_l_cnt = PMAP_$PUR_L_CNT;
        paging->pur_r_cnt = PMAP_$PUR_R_CNT;
        paging->page_flt_cnt = AST_$PAGE_FLT_CNT;
        paging->ws_flt_cnt = AST_$WS_FLT_CNT;
        paging->t_pur_scans = PMAP_$T_PUR_SCANS;
        paging->alloc_cnt = MMAP_$ALLOC_CNT;
        paging->alloc_pages = MMAP_$ALLOC_PAGES;
        paging->steal_cnt = MMAP_$STEAL_CNT;
        paging->ws_overflow = MMAP_$WS_OVERFLOW;
        paging->ws_scan_cnt = MMAP_$WS_SCAN_CNT;
        // paging->reserved_28 not set
        paging->ast_alloc_cnt = AST_$ALLOC_CNT;
        paging->alloc_too_few = AST_$ALLOC_TOO_FEW_CNT;
        paging->reclaim_shar_cnt = MMAP_$RECLAIM_SHAR_CNT;
        paging->reclaim_pur_cnt = MMAP_$RECLAIM_PUR_CNT;
        paging->ws_remove = MMAP_$WS_REMOVE;
        paging->scan_fract = PMAP_$SCAN_FRACT;
    }

    // Handle GET_WS_LIST operation
    if (flag_byte & MMAP_FLAG_GET_WS_LIST) {
        count = global_info->ws_list_count;
        if (count > 0x40) {
            count = 0x40;
        }
        global_info->ws_list_count = count;

        if (count != 0) {
            // Copy working set list entries
            for (i = 0; i < count; i++) {
                /*
                 * 00e5c96e  movea.l #0xe23ca8,A0   ; MMAP_$WS_OWNER
                 * 00e5c97c  movea.l #0xe2612c,A4   ; PROC1_$TYPE
                 */
                ws_list_ptr[i * 2] = MMAP_$WS_OWNER[i];
                ws_list_ptr[i * 2 + 1] = PROC1_$TYPE[i];
            }
        }
    }

    // Handle GET_WS_INFO operation
    if ((flag_byte & MMAP_FLAG_GET_WS_INFO) && MMAP_WSL_HI_MARK > 4) {   /* cmpi.w #5,(0xE23CA6) */
        count = MMAP_WSL_HI_MARK - 5;
        // Copy working set info (0xc bytes per entry from 0x24 byte structures)
        for (i = 0; i <= count; i++) {
            uint32_t *src = (uint32_t *)((char *)MMAP_WSL + 0xb4 + i * 0x24);
            ws_data_ptr[i * 3] = src[1];      // offset 0x04
            ws_data_ptr[i * 3 + 1] = src[6];  // offset 0x18
            ws_data_ptr[i * 3 + 2] = src[7];  // offset 0x1c
        }
    }
}
