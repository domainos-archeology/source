/*
 * FILE_$MK_IMMUTABLE - Mark file as immutable
 *
 * Sets the immutable flag on a file, preventing modifications.
 * Calls FILE_$SET_ATTRIBUTE with attribute ID 1.
 *
 * Original address: 0x00E5DBC0
 */

#include "file/file_internal.h"

/*
 * FILE_$MK_IMMUTABLE
 *
 * Assembly analysis:
 *   00e5dbc0    link.w A6,-0x38      ; 56-byte local buffer
 *   00e5dbc4    st (-0x38,A6)        ; local_3c[0] = 0xFF (set flag TRUE)
 *   00e5dbc8    subq.l #0x2,SP       ; Align stack for return value
 *   00e5dbca    move.l (0xc,A6),-(SP); Push status_ret
 *   00e5dbce    move.l #0x2ffff,-(SP); Push flags (0x2FFFF)
 *   00e5dbd4    pea (-0x38,A6)       ; Push local buffer address
 *   00e5dbd8    move.w #0x1,-(SP)    ; Push attr_id = 1
 *   00e5dbdc    move.l (0x8,A6),-(SP); Push file_uid
 *   00e5dbe0    bsr.w 0x00e5d242     ; Call FILE_$SET_ATTRIBUTE
 *   00e5dbe4    unlk A6
 *   00e5dbe6    rts
 *
 * The function uses a local buffer to pass the flag value (0xFF = TRUE).
 * The flags value 0x2FFFF encodes:
 *   - Low 16 bits (0xFFFF): Required rights mask
 *   - High 16 bits (0x0002): Option flags
 */
void FILE_$MK_IMMUTABLE(uid_t *file_uid, status_$t *status_ret)
{
    uint8_t attr_buffer[56];

    /* Set immutable flag to TRUE (0xFF) */
    attr_buffer[0] = 0xFF;

    /* Set attribute 1 (immutable) with flags 0x2FFFF */
    FILE_$SET_ATTRIBUTE(file_uid, FILE_ATTR_IMMUTABLE, attr_buffer,
                        FILE_FLAGS_IMMUTABLE_MASK, status_ret);
}
