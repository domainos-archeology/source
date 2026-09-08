/*
 * AST_$DISMOUNT - Dismount a volume
 *
 * Dismounts a volume by flushing all cached data for objects on that
 * volume and then calling the VTOC dismount routine.
 *
 * Original address: 0x00e069ca
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "vtoc/vtoc.h"
#include "network/network.h"

void AST_$DISMOUNT(uint16_t vol_index, uint8_t flags, status_$t *status)
{
    status_$t local_status;
    uint16_t vol_mask;
    aote_t *aote;
    int32_t wait_value;

    local_status = status_$ok;

    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* Mark volume as dismounting */
    vol_mask = (uint16_t)(1 << (vol_index & 0x1F));
    ast_$vol_info_count |= vol_mask;

    /* Increment dismount sequence */
    AST_$DISM_SEQN++;

    /* Wait for any in-progress operations on this volume to complete */
    while (DAT_00e1e092[vol_index] != 0) {
        wait_value = (int32_t)(AST_$DISM_EC.value + 1);     /* 0xE06A12 */

        ML_$UNLOCK(AST_LOCK_ID);
        /*
         * 0xE06A2A-0xE06A3A pushes both three-element arrays by value, 24
         * bytes: the eventcount list is { &AST_$DISM_EC, NULL, NULL } (A2 is
         * zero from 0xE06A02) and the value list is { wait_value, 0, 0 }.
         * The returned index is discarded.
         */
        EC_$WAIT((ec_$wait_ecs_t){{ &AST_$DISM_EC, NULL, NULL }},
                 (ec_$wait_vals_t){{ wait_value, 0, 0 }});
        ML_$LOCK(AST_LOCK_ID);
    }

    /* Scan all AOTEs for objects on this volume */
    aote = (aote_t *)0xEC7B60;  /* Start of AOTE area */

    while (aote < AST_$AOTE_LIMIT) {
        /* Check if AOTE is for a local object on this volume */
        if ((int8_t)*((char *)aote + 0xB9) >= 0 &&  /* Local object */
            *((uint8_t *)aote + 0xB8) == vol_index) {  /* On this volume */

            /* Wait if AOTE is in transition */
            if ((int8_t)aote->flags < 0) {
                AST_$WAIT_FOR_AST_INTRANS();
                continue;  /* Re-scan from beginning */
            }

            /* Check if AOTE has cached data (not paging file) */
            uint8_t uid_first = *((uint8_t *)((char *)aote + 0x10));
            if (uid_first != 0) {
                /* Skip paging file */
                if (*(uint32_t *)((char *)aote + 0x10) != NETWORK_$PAGING_FILE_UID.high ||
                    *(uint32_t *)((char *)aote + 0x14) != NETWORK_$PAGING_FILE_UID.low) {

                    /* Flush cached data */
                    /* 0x00E06A9C-0x00E06AA4: `st -(SP)` (flags3), `st -(SP)`
                     * (flags2), `move.b (0xa,A6),-(SP)` (flags1) - three
                     * single bytes.  (source-o7gq) */
                    ast_$process_aote(aote, (boolean)flags, -1, -1,
                                      &local_status);

                    if (local_status != status_$ok) {
                        ML_$UNLOCK(AST_LOCK_ID);
                        AST_$DISMOUNT_FAILED_PTR = aote;
                        goto done;
                    }

                    /* Free the AOTE */
                    ast_$release_aote(aote);
                }
            }
        }

        /* Move to next AOTE */
        aote = (aote_t *)((char *)aote + 0xC0);
    }

    ML_$UNLOCK(AST_LOCK_ID);

    /* Call VTOC dismount */
    VTOC_$DISMOUNT(vol_index, flags, &local_status);

done:
    /* Clear dismount flag */
    ast_$vol_info_count &= ~vol_mask;

    PROC1_$INHIBIT_END();

    *status = local_status;
}
