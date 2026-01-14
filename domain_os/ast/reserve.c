/*
 * AST_$RESERVE - Reserve disk space for an object
 *
 * Ensures that disk space is allocated for the specified byte range
 * of an object. For remote objects, forwards the request to the server.
 * For local objects, allocates disk blocks as needed.
 *
 * Parameters:
 *   uid - Pointer to object UID
 *   start_byte - Starting byte offset
 *   byte_count - Number of bytes to reserve
 *   status - Status return
 *
 * Original address: 0x00e0677e
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "rem_file/rem_file.h"
#include "disk/disk.h"

void AST_$RESERVE(uid_t *uid, uint32_t start_byte, uint32_t byte_count, status_$t *status)
{
    aote_t *aote;
    aste_t *aste;
    uint32_t end_byte;
    int16_t end_segment;
    uint16_t end_page;
    uid_t vol_uid;

    *status = status_$ok;

    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* Look up AOTE by UID */
    FUN_00e0209e(uid);
    aote = NULL;  /* TODO: Get from FUN_00e0209e return in A0 */

    if (aote == NULL) {
        /* AOTE not cached - try to load it */
        FUN_00e020fa(uid, 0, status, 0);
        aote = NULL;  /* TODO: Get from FUN_00e020fa return in A0 */
        if (aote == NULL) {
            goto done;
        }
    } else {
        /* Mark AOTE as busy */
        aote->flags |= AOTE_FLAG_BUSY;
    }

    /* Check if remote object */
    if (*(int8_t *)((char *)aote + 0xB9) < 0) {
        /* Remote object - forward to server */
        vol_uid.high = *(uint32_t *)((char *)aote + 0xAC);
        vol_uid.low = *(uint32_t *)((char *)aote + 0xB0);
        ML_$UNLOCK(AST_LOCK_ID);
        REM_FILE_$RESERVE(&vol_uid, uid, start_byte, (int16_t)byte_count, status);
        goto done_no_unlock;
    }

    /* Local object - reserve disk blocks */
    aote->flags |= AOTE_FLAG_IN_TRANS;

    end_byte = start_byte + byte_count - 1;
    end_segment = (int16_t)(end_byte >> 15);  /* 32KB segments */
    end_page = (uint16_t)end_byte & 0x1F;

    /* Iterate through segments */
    while (1) {
        /* Find or create ASTE for segment */
        aste = FUN_00e0250c(aote, end_segment);
        if (aste == NULL) {
            aste = FUN_00e0255c(aote, end_segment, status);
        }

        if (aste == NULL) {
            break;
        }

        /* Mark ASTE as in transition and locked */
        *(uint8_t *)((char *)aste + 0x12) |= 0x80;
        *(uint8_t *)((char *)aste + 0x12) |= 0x40;

        ML_$UNLOCK(AST_LOCK_ID);
        ML_$LOCK(PMAP_LOCK_ID);

        /* Process pages in this segment */
        uint16_t start_page = (uint16_t)start_byte & 0x1F;
        uint32_t *segmap_ptr = (uint32_t *)((uint32_t)aste->seg_index * 0x80 +
                                            SEGMAP_BASE + start_page * 4 - 0x80);

        while (1) {
            /* Wait for pages in transition */
            while (*(int16_t *)segmap_ptr < 0) {
                FUN_00e00c08();
            }

            /* Check if page needs disk allocation */
            if ((*segmap_ptr & SEGMAP_FLAG_IN_USE) == 0 &&
                (*segmap_ptr & SEGMAP_DISK_ADDR_MASK) == 0) {
                /* Mark pages in transition and allocate */
                uint16_t alloc_count = 0;
                uint32_t *map_ptr = segmap_ptr;

                while (alloc_count < 32) {
                    *(uint8_t *)map_ptr |= 0x80;
                    alloc_count++;
                    map_ptr++;

                    if ((*(uint16_t *)map_ptr & 0x4000) != 0) break;
                    if (*(int16_t *)map_ptr < 0) break;
                    if ((*map_ptr & SEGMAP_DISK_ADDR_MASK) != 0) break;
                }

                /* Allocate disk blocks */
                uint32_t disk_block;
                DISK_$ALLOC_W_HINT(*(uint16_t *)((char *)aote + 0x24),
                                   0, &disk_block, alloc_count, status);

                if (*status != status_$ok) {
                    FUN_00e0283c(segmap_ptr, alloc_count);
                    break;
                }

                /* Store disk addresses in segment map */
                map_ptr = segmap_ptr;
                for (int i = 0; i < alloc_count; i++) {
                    *map_ptr = (*map_ptr & ~SEGMAP_DISK_ADDR_MASK) | (disk_block + i);
                    *(uint8_t *)map_ptr &= 0x7F;  /* Clear in-transition */
                    map_ptr++;
                }

                segmap_ptr = map_ptr;
            } else {
                segmap_ptr++;
            }

            /* Check if we've processed all pages in range */
            /* TODO: Complete bounds checking logic */
            break;
        }

        ML_$UNLOCK(PMAP_LOCK_ID);
        ML_$LOCK(AST_LOCK_ID);

        /* Clear ASTE flags */
        *(uint8_t *)((char *)aste + 0x12) &= ~0x80;
        *(uint8_t *)((char *)aste + 0x12) &= ~0x40;

        /* Move to previous segment */
        end_segment--;
        if (end_segment < 0) break;
    }

    /* Clear in-transition flag */
    aote->flags &= ~AOTE_FLAG_IN_TRANS;
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);

done:
    ML_$UNLOCK(AST_LOCK_ID);
done_no_unlock:
    PROC1_$INHIBIT_END();
}
