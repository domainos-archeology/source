/*
 * FILE_$SET_MAND_LOCK - Set mandatory lock flag
 *
 * Sets or clears the mandatory lock flag on a file. When set, the file
 * requires explicit locking for access rather than advisory locking.
 *
 * Original address: 0x00E5DC6A
 */

#include "file/file_internal.h"

/*
 * FILE_$SET_MAND_LOCK
 *
 * Assembly analysis:
 *   00e5dc6a    link.w A6,-0x38       ; 56-byte local buffer
 *   00e5dc6e    movea.l (0xc,A6),A0   ; A0 = flag (param 2)
 *   00e5dc72    move.b (A0),(-0x38,A6); Copy *flag to local buffer
 *   00e5dc76    subq.l #0x2,SP        ; Align stack for return value
 *   00e5dc78    move.l (0x10,A6),-(SP); Push status_ret (param 3)
 *   00e5dc7c    move.l #0x80000,-(SP) ; Push flags (0x80000)
 *   00e5dc82    pea (-0x38,A6)        ; Push local buffer address
 *   00e5dc86    move.w #0x19,-(SP)    ; Push attr_id = 25 (0x19)
 *   00e5dc8a    move.l (0x8,A6),-(SP) ; Push file_uid
 *   00e5dc8e    bsr.w 0x00e5d242      ; Call FILE_$SET_ATTRIBUTE
 *   00e5dc92    unlk A6
 *   00e5dc94    rts
 *
 * The function copies the flag byte from the parameter to a local buffer.
 * The flags value 0x80000 encodes:
 *   - Low 16 bits (0x0000): No specific rights required
 *   - High 16 bits (0x0008): Option flags
 */
void FILE_$SET_MAND_LOCK(uid_t *file_uid, uint8_t *flag, status_$t *status_ret)
{
    uint8_t attr_buffer[56];

    /* Copy the flag value to local buffer */
    attr_buffer[0] = *flag;

    /* Set attribute 25 (mandatory lock) with flags 0x80000 */
    FILE_$SET_ATTRIBUTE(file_uid, FILE_ATTR_MAND_LOCK, attr_buffer,
                        FILE_FLAGS_MAND_LOCK_MASK, status_ret);
}
