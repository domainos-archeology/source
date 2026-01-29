/*
 * DIR_$OLD_INIT - Legacy directory subsystem initialization
 *
 * Initializes the OLD directory slot table by clearing all 58
 * handle pointer entries. Each entry is 8 bytes apart, with the
 * handle pointer at offset 0x2B8 from the data base (0xE7FD24).
 *
 * Original address: 0x00E314F4
 * Original size: 38 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_INIT - Legacy directory initialization
 *
 * Assembly loop:
 *   moveq #0x39, D0          ; count = 57 (dbf iterates 58 times)
 *   movea.l #0xe7fd24, A0    ; A0 = slot table base
 *   clr.l (0x2b8, A0)        ; clear handle at slot[i] + 0x2b8
 *   addq.l #8, A0            ; advance to next 8-byte slot
 *   dbf D0, loop
 *
 * This clears the active handle field for each of 58 directory slots.
 */
void DIR_$OLD_INIT(void)
{
    int16_t i;

    /* Clear handle pointer for all 58 slots
     * Each slot is 8 bytes, handle at offset 0x2B8 from slot base */
    for (i = 0; i < DIR_OLD_NUM_SLOTS; i++) {
        *((uint32_t *)(&DAT_00e7fd24 + i * 8 + DIR_OLD_HANDLE_OFFSET)) = 0;
    }
}
