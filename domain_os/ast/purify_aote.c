/*
 * ast_$purify_aote - Purify/write back AOTE attributes
 *
 * Writes back modified object attributes to the VTOCE (Volume Table of
 * Contents Entry). Handles both local and remote objects.
 *
 * Parameters:
 *   aote - The AOTE to purify
 *   flags - Purification flags (passed to VTOCE_$WRITE)
 *   status - Output status
 *
 * Original address: 0x00e013a0
 */

#include "ast/ast_internal.h"

/* External function prototypes */

/* Network info flags pointer */
#if defined(ARCH_M68K)
#define NET_INFO_FLAGS ((void *)0xE01500)
#else
#define NET_INFO_FLAGS net_info_flags
#endif

/* Status codes */
#define status_$disk_write_protected 0x00080007

void ast_$purify_aote(aote_t *aote, uint16_t flags, status_$t *status)
{
    status_$t local_status;
    uint32_t attrs_buffer[36];  /* 144 bytes for attributes */
    uint32_t clock_high;
    uint16_t clock_low;
    int16_t i;
    uint32_t *src, *dst;

    *status = status_$ok;

    /* Check if this is a remote object (bit 7 at offset 0xB9) */
    if (*((int8_t *)aote + 0xB9) < 0) {
        /* Remote object - check TOUCHED flag (0x10 at offset 0xBF) */
        if ((aote->status_flags & 0x10) != 0) {
            /* Only update if not read-only (bit 0 at offset 0x0F) */
            if ((*((uint8_t *)aote + 0x0F) & 1) == 0) {
                /* Clear TOUCHED flag */
                aote->flags &= ~AOTE_FLAG_TOUCHED;

                /* Get updated info from network */
                ML_$UNLOCK(AST_LOCK_ID);
                NETWORK_$AST_GET_INFO((char *)aote + 0x9C, NET_INFO_FLAGS,
                                      attrs_buffer, &local_status);
                ML_$LOCK(AST_LOCK_ID);

                if (local_status == status_$ok) {
                    /* Copy DTS (Data Time Stamp) - at offset 0x30 in AOTE */
                    /* attrs_buffer[0x24/4] = clock_high, attrs_buffer[0x28/4] = clock_low partial */
                    ML_$LOCK(PMAP_LOCK_ID);
                    *((uint32_t *)((char *)aote + 0x30)) = attrs_buffer[0x24 / 4];
                    *((uint16_t *)((char *)aote + 0x34)) = (uint16_t)attrs_buffer[0x28 / 4];
                    ML_$UNLOCK(PMAP_LOCK_ID);
                } else {
                    /* Restore TOUCHED flag on failure */
                    aote->flags |= AOTE_FLAG_TOUCHED;
                }
            }
        }
        return;
    }

    /* Local object */

    /* Check TOUCHED flag (0x10) */
    if ((aote->status_flags & 0x10) != 0) {
        /* Clear TOUCHED flag */
        aote->flags &= ~AOTE_FLAG_TOUCHED;

        /* Get current time */
        ML_$LOCK(PMAP_LOCK_ID);
        TIME_$CLOCK((clock_t *)((char *)aote + 0x30));
        ML_$UNLOCK(PMAP_LOCK_ID);

        /* Set DIRTY flag */
        aote->flags |= AOTE_FLAG_DIRTY;
    }

    /* Check DIRTY flag (0x20) */
    if ((aote->status_flags & 0x20) != 0) {
        /* Clear DIRTY flag */
        aote->flags &= ~AOTE_FLAG_DIRTY;

        /* Copy attributes (144 bytes = 36 uint32_t starting at offset 0x0C) */
        src = (uint32_t *)((char *)aote + 0x0C);
        dst = attrs_buffer;
        for (i = 0x23; i >= 0; i--) {
            *dst++ = *src++;
        }

        /* Write to VTOCE */
        ML_$UNLOCK(AST_LOCK_ID);
        VTOCE_$WRITE((vtoc_$lookup_req_t *)((char *)aote + 0x9C),
                     (vtoce_$result_t *)attrs_buffer, (uint8_t)flags, status);
        ML_$LOCK(AST_LOCK_ID);

        if (*status != status_$ok) {
            if (*status == status_$disk_write_protected) {
                /* Ignore write-protected error */
                *status = status_$ok;
            } else {
                /* Set error flag and restore DIRTY */
                *(uint8_t *)status |= 0x80;
                aote->flags |= AOTE_FLAG_DIRTY;
            }
        }
    }
}
