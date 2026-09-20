/*
 * DISK_$ERROR_QUE - Ask the volume's driver about a queued error
 *
 * 0x00E3DAD4 - 0x00E3DB02 (48 bytes).  Re-emitted from the disassembly on
 * 2026-09-19: the driver slot is a Pascal FUNCTION (`subq.l #0x2,SP` at
 * 0x00E3DAE0 opens a word result slot) whose D0 this routine hands back
 * untouched, and the third argument is the address of a byte cell
 * (disk_$wait_io: `pea (-0xc,A6)` at 0x00E3CA84, read with `move.b`).
 *
 * Arguments:
 *   (0x8,A6)  vol         the volume descriptor (+0x7c form); dev_info
 *                         (+0x18) -> disk_device_entry_t -> jump_table
 *   (0xc,A6)  is_timeout  word by value (disk_$wait_io passes D3)
 *   (0xe,A6)  result      -> byte cell the driver fills (bit 7 = error
 *                         present, tested `bpl` at 0x00E3CA9C)
 *
 * The driver's slot +0x14 is called with the same three arguments
 * (0x00E3DAEC - 0x00E3DAF8).
 */

#include "disk/disk_internal.h"

int16_t DISK_$ERROR_QUE(void *vol, uint16_t is_timeout, int8_t *result)
{
    disk_$volume_t *v = (disk_$volume_t *)vol;                      /* A2 */
    disk_device_entry_t *dev = (disk_device_entry_t *)v->dev_info;  /* A1 = (0x18,A2) */
    disk_jump_table_t *jt = (disk_jump_table_t *)dev->jump_table;   /* A0 = (A1) */

    /* 0x00E3DAE8 - 0x00E3DAF8 */
    return jt->error_que(vol, is_timeout, result);
}
