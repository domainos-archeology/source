/*
 * win/cinit.c - WIN_$CINIT (0x00E30340, 150 bytes; map `I E30340 WIN_ size
 * = 98`)
 *
 * Winchester controller initialisation: probe the controller at the DCTE's
 * +0x34 address, fill in the module block's controller fields, initialise
 * the unit's eventcount and register the driver with DISK.
 *
 * Frame (link.w A6,-0x14; A3 A2 D2 saved):
 *   A6-0x14  4  jump_table_va  DISK_$REGISTER's fifth argument, by address
 *   A6-0x0A     probe result   io_$probe's third argument
 *   A2          dcte (argument 1)
 *   A3          the module block 0xE2B89C
 *   D2          the status being returned (0 unless the probe failed)
 */

#include "win/win_internal.h"

/*
 * 0x00E303D6: 00 00 - the constant controller-type word.  io_$probe gets
 * its address (pea (0x7c,PC) at 0x00E30358; 0x00E3035A + 0x7C) and
 * DISK_$REGISTER gets it TWICE, as `type` and `controller` (pea (0x16,PC)
 * at 0x00E303BE, then `move.l (SP),-(SP)`).
 */
static uint16_t WIN_TYPE = 0x0000;

status_$t WIN_$CINIT(void *controller)
{
    dcte_t *dcte = (dcte_t *)controller;    /* A2 */
    uint8_t *win_data = WIN_DATA_BASE;      /* A3 */
    uint8_t probe_result[0x0A];             /* A6-0x0A */
    uint32_t jump_table_va;                 /* A6-0x14 */
    status_$t status;                       /* D2 */

    status = status_$ok;

    /* 0x00E30350-0x00E30368: io_$probe(&0, &dcte->disk_dinit, result); a
     * Domain false (`bmi` not taken) is "not in system". */
    if (io_$probe(&WIN_TYPE, &dcte->disk_dinit, probe_result) >= 0) {
        status = status_$io_controller_not_in_system;   /* 0x00E3036A */
        return status;
    }

    /* 0x00E30372-0x00E30380: the DCTE, its +0x34 register address and its
     * +0x3C word into the block's +0, +4 and +8. */
    *(uint32_t *)(win_data + WIN_CTRL_INFO_OFFSET) = ARCH_PTR_TO_VA(dcte);
    *(uint32_t *)(win_data + WIN_BASE_ADDR_OFFSET) = dcte->disk_dinit;
    *(uint16_t *)(win_data + WIN_DEV_TYPE_OFFSET) =
        (uint16_t)(dcte->disk_error_que >> 16);

    /* 0x00E30386-0x00E3038C: `bset.b #5` and `bset.b #3` on the BYTE at
     * +0x0A, i.e. bits 13 and 11 of the word DISK_$REGISTER reads there. */
    win_data[WIN_FLAGS_OFFSET] |= 0x28;

    /* 0x00E30392-0x00E303A8: EC_$INIT of the eventcount at +0x30 + cnum*12. */
    EC_$INIT(WIN_UNIT_EC(dcte->cnum));

    /*
     * 0x00E303AA-0x00E303C4: DISK_$REGISTER(&WIN_TYPE, &WIN_TYPE, &+0x0A,
     * &+0x08, &jump_table_va) where the last is a longword holding the
     * address of the jump table at +0x10.  Its result is not examined.
     */
    jump_table_va = ARCH_PTR_TO_VA(win_data + WIN_JUMP_TABLE_OFFSET);
    (void)DISK_$REGISTER(&WIN_TYPE, &WIN_TYPE,
                         (uint16_t *)(void *)(win_data + WIN_FLAGS_OFFSET),
                         (uint16_t *)(void *)(win_data + WIN_DEV_TYPE_OFFSET),
                         (void **)&jump_table_va);

    /* 0x00E303CA */
    return status;
}
