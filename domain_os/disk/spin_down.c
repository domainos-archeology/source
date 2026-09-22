/*
 * DISK_$SPIN_DOWN - Spin every registered drive down and wait for the slowest
 *
 * 0x00E3DB04 - 0x00E3DB70 (110 bytes, A5 = DISK_$DEVICES at 0xE7AD5C).
 * Verified against the disassembly on 2026-09-19; the earlier emission
 * was faithful but addressed the table as a private constant.  (The batch
 * list gives 0xE7AD5C, the table itself.)
 *
 * For each of the 32 entries with a driver (`moveq #0x1f` / dbf) whose
 * vector slot +0x00 is non-null, the slot is called as a word function
 * with the address of the entry's controller word (`pea (0x6,A3)`,
 * 0x00E3DB2A) and the largest result kept (signed `bge`, 0x00E3DB36).
 * A positive maximum is then slept through: TIME_$WAIT with the constant
 * type word at 0x00E3DB72 (bytes 00 00, relative), a clock whose high
 * longword is max << 2 and whose low word is 0 (0x00E3DB4A - 0x00E3DB52),
 * and a status cell that is never read.
 */

#include "disk/disk_internal.h"
#include "time/time.h"

/* 0x00E3DB72: 00 00, passed by `pea (0x12,PC)` at 0x00E3DB5E */
static const uint16_t disk_$spin_down_wait_type_00e3db72 = 0;

void DISK_$SPIN_DOWN(void)
{
    int16_t max_time = 0;               /* D2 */
    int16_t t;                          /* D0 */
    clock_t delay;                      /* (-0x10,A6) */
    status_$t wait_status;              /* (-0x14,A6): never read */
    int16_t i;

    /* 0x00E3DB12 - 0x00E3DB42 */
    for (i = 0; i < DISK_MAX_DEVICES; i++) {
        disk_device_entry_t *e = &DISK_$DEVICES[i];
        disk_jump_table_t *jt;
        if (e->jump_table == NULL) {
            continue;
        }
        jt = (disk_jump_table_t *)e->jump_table;
        if (jt->spin_down == NULL) {
            continue;
        }
        t = jt->spin_down(&e->controller);
        if (max_time < t) {
            max_time = t;
        }
    }

    /* 0x00E3DB46 - 0x00E3DB62 */
    if (max_time > 0) {
        delay.high = (uint32_t)((int32_t)max_time << 2);
        delay.low = 0;
        TIME_$WAIT((uint16_t *)&disk_$spin_down_wait_type_00e3db72, &delay,
                   &wait_status);
    }
}
