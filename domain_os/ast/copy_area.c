/*
 * AST_$COPY_AREA - Copy pages between areas
 *
 * Copies segment map entries and associated data between two areas.
 * Used for fork-like operations and area duplication. Handles both
 * local and remote objects, with network operations for remote cases.
 *
 * Parameters:
 *   partner_index - Network partner index
 *   unused - Unused parameter
 *   src_aste - Source ASTE
 *   dst_aste - Destination ASTE
 *   start_seg - Starting segment number
 *   buffer - Buffer for data transfer
 *   status - Status return
 *
 * Original address: 0x00e03a30
 */

#include "ast/ast_internal.h"
#include "netbuf/netbuf.h"
#include "network/network.h"
#include "area/area.h"
#include "anon/anon.h"

void AST_$COPY_AREA(uint16_t partner_index, uint16_t unused,
                    aste_t *src_aste, aste_t *dst_aste,
                    uint16_t start_seg, char *buffer, status_$t *status)
{
    uint32_t *src_segmap;
    uint8_t *dst_segmap;
    int page;
    int8_t in_transition;
    uint32_t ppn_array[32];
    int16_t vol_index;

    *status = status_$ok;

    /* Get segment map pointers */
    src_segmap = (uint32_t *)((uint32_t)src_aste->seg_index * 0x80 + SEGMAP_BASE - 0x80);
    dst_segmap = (uint8_t *)((uint32_t)dst_aste->seg_index * 0x80 + SEGMAP_BASE - 0x80);

    /* Get volume index for network operations */
    vol_index = *(int16_t *)(partner_index * 0x30 + 0xD94BF8);

    in_transition = 0;
    uint32_t seg_offset = (uint32_t)start_seg << 5;

    ML_$LOCK(PMAP_LOCK_ID);

    /* Process each of 32 pages */
    for (page = 0; page < 32; page++) {
        /* Wait for source page not in transition */
        while (*(int16_t *)src_segmap < 0) {
            if (in_transition < 0) {
                EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC);
                in_transition = 0;
            }
            FUN_00e00c08();
        }

        /* Check if source page is installed */
        if ((*src_segmap & SEGMAP_FLAG_IN_USE) == 0) {
            /* Page not installed */
            if ((*src_segmap & SEGMAP_DISK_ADDR_MASK) == 0) {
                /* No data - clear destination */
                dst_segmap[0] = 0;
                dst_segmap[1] = 0;
                dst_segmap[2] = 0;
                dst_segmap[3] = 0;
            } else {
                /* Has disk address - need to read from disk/network */
                uint16_t count = 0;
                uint32_t *map_ptr = src_segmap;

                /* Mark pages in transition */
                while (count < (32 - page)) {
                    *(uint8_t *)map_ptr |= 0x80;
                    count++;
                    map_ptr++;

                    if (*(int16_t *)map_ptr < 0) break;
                    if ((*map_ptr & SEGMAP_FLAG_IN_USE) != 0) break;
                    if ((*map_ptr & SEGMAP_DISK_ADDR_MASK) == 0) break;
                }

                /* Allocate pages */
                FUN_00e00d46((count << 16) | 1, ppn_array);

                ML_$UNLOCK(PMAP_LOCK_ID);

                /* Read data (local or network) */
                if (vol_index != 0) {
                    /* Network read */
                    uint8_t temp_buf[8];
                    for (int i = 0; i < count; i++) {
                        NETBUF_$RTN_DAT(ppn_array[i] << 10);
                        NETWORK_$READ_AHEAD(&AREA_$PARTNER, &ANON_$UID, &ppn_array[i],
                                            AREA_$PARTNER_PKT_SIZE, 1, 0, 0,
                                            temp_buf, temp_buf, temp_buf, status);
                        if (*status != status_$ok) {
                            /* Error - get buffer back */
                            uint32_t temp_addr;
                            NETBUF_$GET_DAT(&temp_addr);
                            /* TODO: Handle error properly */
                            break;
                        }
                    }
                } else {
                    /* Local disk read */
                    /* TODO: Implement local disk read */
                }

                ML_$LOCK(PMAP_LOCK_ID);

                /* Clear transition bits for remaining pages */
                if (count > 1) {
                    FUN_00e0283c(src_segmap + 1, count - 1);
                }

                in_transition = -1;
            }

            dst_segmap += 4;
            src_segmap++;
            buffer += 0x400;
            continue;
        }

        /* Source page is installed - copy to destination */
        /* TODO: Implement installed page copy logic */

        dst_segmap += 4;
        src_segmap++;
        buffer += 0x400;
    }

    if (in_transition < 0) {
        EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC);
    }

    ML_$UNLOCK(PMAP_LOCK_ID);
}
