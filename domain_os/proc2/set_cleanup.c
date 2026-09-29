/*
 * PROC2_$SET_CLEANUP - Flag a cleanup handler for the calling process
 *
 * Re-emitted from the image (0x00E41572..0x00E415C0, 80 bytes) and
 * verified; the previous body was faithful.
 *
 *   00e41580  move.w (0x8,A6),D0w         ; bit number
 *   00e41584  tst.w PROC1_$AS_ID / beq    ; address space 0: nothing
 *   00e4159a  clr.w D2w
 *   00e415a0  bset.l D0,D2                ; 1 << (bit mod 32), as a longword
 *   00e415a6  move.w D2w,(-0x4,A6)        ; only the low word survives
 *   00e415b4  or.w D3w,(-0x48,A0,D2*0x1)  ; entry+0x9C |= it
 *
 * Callers: ten sites across the I/O managers (0x00E0BA02 .. 0x00E6CE06).
 *
 * Original address: 0x00e41572
 */

#include "proc2/proc2_internal.h"

void PROC2_$SET_CLEANUP(uint16_t bit_num)
{
    proc2_info_t *entry;
    uint32_t mask;               /* D2 */

    /* 0x00E41584: tst.w (0x00e2060a).l / beq exit */
    if (PROC1_$AS_ID != 0) {
        /* 0x00E4158C-0x00E415B0 */
        entry = P2_INFO_ENTRY((int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT]);
        mask = (uint32_t)1 << (bit_num & 0x1F);              /* bset.l D0,D2 */
        entry->cleanup_flags |= (uint16_t)mask;              /* 0x00E415B4: or.w */
    }
}
