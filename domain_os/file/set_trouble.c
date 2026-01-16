/*
 * FILE_$SET_TROUBLE - Set file trouble flag
 *
 * Sets the trouble flag on a file, indicating some issue with the file.
 * This might be used to mark files that have integrity issues or need
 * administrative attention.
 *
 * Original address: 0x00E5DC42
 */

#include "file/file_internal.h"

/*
 * FILE_$SET_TROUBLE
 *
 * Assembly analysis:
 *   00e5dc42    link.w A6,-0x38      ; 56-byte local buffer
 *   00e5dc46    st (-0x38,A6)        ; local_3c[0] = 0xFF (set flag TRUE)
 *   00e5dc4a    subq.l #0x2,SP       ; Align stack for return value
 *   00e5dc4c    move.l (0x10,A6),-(SP); Push status_ret (param 3)
 *   00e5dc50    move.l #0xffff,-(SP) ; Push flags (0xFFFF)
 *   00e5dc56    pea (-0x38,A6)       ; Push local buffer address
 *   00e5dc5a    move.w #0x2,-(SP)    ; Push attr_id = 2
 *   00e5dc5e    move.l (0x8,A6),-(SP); Push file_uid
 *   00e5dc62    bsr.w 0x00e5d242     ; Call FILE_$SET_ATTRIBUTE
 *   00e5dc66    unlk A6
 *   00e5dc68    rts
 *
 * The function uses a local buffer to pass the flag value (0xFF = TRUE).
 * The second parameter (unused) is preserved for API compatibility but
 * is not referenced in the implementation.
 */
void FILE_$SET_TROUBLE(uid_t *file_uid, void *unused, status_$t *status_ret)
{
    uint8_t attr_buffer[56];

    (void)unused;  /* Unused - for API compatibility */

    /* Set trouble flag to TRUE (0xFF) */
    attr_buffer[0] = 0xFF;

    /* Set attribute 2 (trouble) with flags 0xFFFF */
    FILE_$SET_ATTRIBUTE(file_uid, FILE_ATTR_TROUBLE, attr_buffer,
                        FILE_FLAGS_TROUBLE_MASK, status_ret);
}
