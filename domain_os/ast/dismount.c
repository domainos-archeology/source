/*
 * AST_$DISMOUNT - Dismount a volume
 *
 * Dismounts a volume by flushing all cached data for objects on that
 * volume and then calling the VTOC dismount routine.
 *
 * Original address: 0x00e069ca
 */

#include "ast.h"

/* Internal function prototypes */
extern void FUN_00e01ad2(aote_t *aote, uint8_t flags1, uint16_t flags2, uint16_t flags3, status_$t *status);
extern void FUN_00e00f7c(aote_t *aote);
extern void VTOC_$DISMOUNT(uint16_t vol_index, uint8_t flags, status_$t *status);

/* External data */
extern uint16_t DAT_00e1e0a0;
extern uint32_t DAT_00e1e088;
extern int16_t DAT_00e1e092[];
extern aote_t* AST_$DISMOUNT_FAILED_PTR;
extern uid_t NETWORK_$PAGING_FILE_UID;

void AST_$DISMOUNT(uint16_t vol_index, uint8_t flags, status_$t *status)
{
    status_$t local_status;
    uint16_t vol_mask;
    aote_t *aote;
    ec_$eventcount_t *ec_array[3];
    uint32_t ec_values[3];

    local_status = status_$ok;

    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* Mark volume as dismounting */
    vol_mask = (uint16_t)(1 << (vol_index & 0x1F));
    DAT_00e1e0a0 |= vol_mask;

    /* Increment dismount sequence */
    AST_$DISM_SEQN++;

    /* Wait for any in-progress operations on this volume to complete */
    while (DAT_00e1e092[vol_index] != 0) {
        ec_values[0] = DAT_00e1e088 + 1;

        ML_$UNLOCK(AST_LOCK_ID);
        EC_$WAIT(ec_array, ec_values);
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
                    FUN_00e01ad2(aote, flags, 0xFFFF, 0xFFE0, &local_status);

                    if (local_status != status_$ok) {
                        ML_$UNLOCK(AST_LOCK_ID);
                        AST_$DISMOUNT_FAILED_PTR = aote;
                        goto done;
                    }

                    /* Free the AOTE */
                    FUN_00e00f7c(aote);
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
    DAT_00e1e0a0 &= ~vol_mask;

    PROC1_$INHIBIT_END();

    *status = local_status;
}
