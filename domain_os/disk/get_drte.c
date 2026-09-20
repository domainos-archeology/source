/*
 * DISK_$GET_DRTE - Find the registered driver for a controller type/number
 *
 * 0x00E3DA1C - 0x00E3DA62 (72 bytes, A5 = DISK_$DEVICES at 0xE7AD5C).
 * Re-emitted from the disassembly on 2026-09-19: the earlier file indexed
 * the table by a number.  The image takes TWO word arguments by reference
 * and searches the 32-entry table (`moveq #0x1f` / `dbf`, stride 0x0c) for
 * the first entry with a non-zero jump_table whose device_type (+0x04)
 * and controller (+0x06) match; the result (D0 via A0) is that entry or
 * NULL (`suba.l A0,A0` at 0x00E3DA36).
 *
 * Arguments:
 *   (0x8,A6) ctype_ptr  -> word controller type
 *   (0xc,A6) cnum_ptr   -> word controller number
 */

#include "disk/disk_internal.h"

disk_device_entry_t *DISK_$GET_DRTE(uint16_t *ctype_ptr, uint16_t *cnum_ptr)
{
    uint16_t ctype;                 /* D0 */
    uint16_t cnum;                  /* D1 */
    disk_device_entry_t *found;     /* A0 */
    int16_t i;

    /* 0x00E3DA2A - 0x00E3DA36 */
    ctype = *ctype_ptr;
    cnum = *cnum_ptr;
    found = NULL;

    /* 0x00E3DA38 - 0x00E3DA56 */
    for (i = 0; i < DISK_MAX_DEVICES; i++) {
        disk_device_entry_t *e = &DISK_$DEVICES[i];
        if (e->jump_table != NULL &&
            e->device_type == ctype &&
            e->controller == cnum) {
            found = e;
            break;
        }
    }

    return found;
}
