/*
 * DISK_$REGISTER - Register a disk driver in DISK_$DEVICES
 *
 * 0x00E3D9AC - 0x00E3DA1A (112 bytes, A5 = DISK_$DEVICES at 0xE7AD5C).
 * Verified against the disassembly on 2026-09-19; the earlier emission was
 * faithful but addressed the table as a private constant.  (The batch
 * list gives 0xE7AD5C, the table itself.)
 *
 * Arguments (all by reference to words, the last to a pointer):
 *   (0x8,A6)  type        -> word, stored at entry +0x04
 *   (0xc,A6)  controller  -> word, stored at entry +0x06
 *   (0x10,A6) units       -> word, stored at entry +0x08
 *   (0x14,A6) flags       -> word, stored at entry +0x0a
 *   (0x18,A6) jump_table  -> pointer to the driver vector (A2)
 * Result: D0b, 0xFF once stored, 0 if the vector lacks a dinit (+0x08)
 * or do_io (+0x10) entry or the table is full.
 *
 * Note that the readers in this tree (DISK_$ADD_QUE 0x00E3C732,
 * DISK_$FORMAT 0x00E3D3FE, DISK_$GET_MNT_INFO 0x00E6BFDA) treat the
 * +0x08 word as a flags word; what a driver passes as `units` is
 * therefore what they see.  See the bead on disk_device_entry_t.
 */

#include "disk/disk_internal.h"

uint8_t DISK_$REGISTER(uint16_t *type, uint16_t *controller, uint16_t *units,
                       uint16_t *flags, void **jump_table)
{
    uint16_t dev_type = *type;                  /* D2 */
    uint16_t ctlr = *controller;                /* D3 */
    uint16_t unit_count = *units;               /* (-0xa,A6) */
    uint16_t dev_flags = *flags;                /* D4 */
    disk_jump_table_t *jt = (disk_jump_table_t *)*jump_table;   /* A2 */
    uint8_t result = 0;                         /* D0b */
    int16_t i;

    /* 0x00E3D9DC - 0x00E3D9E6 */
    if (jt->dinit == NULL || jt->do_io == NULL) {
        return result;
    }

    /* 0x00E3D9E8 - 0x00E3DA0E: first empty slot of 32 */
    for (i = 0; i < DISK_MAX_DEVICES; i++) {
        disk_device_entry_t *e = &DISK_$DEVICES[i];
        if (e->jump_table != NULL) {
            continue;
        }
        e->device_type = dev_type;
        e->controller = ctlr;
        e->unit_count = unit_count;
        e->flags = dev_flags;
        e->jump_table = jt;
        result = 0xFF;                          /* st D0b */
        break;
    }

    return result;
}
