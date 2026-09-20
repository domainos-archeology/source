/*
 * DEBUG_CLEAR_INTERNAL - Detach a process from its debugger
 *
 * Re-emitted from the image (0x00E41A24..0x00E41AB8, 150 bytes).
 *
 * Frame (link.w A6,-0xC):
 *   (0x8,A6)  proc_idx  word -> D2
 *   (0xA,A6)  flag      byte -> D3  (high byte of the word slot)
 *   A6-0x8    status for XPD_$WRITE / PROC1_$RESUME (never examined)
 *
 * The entry is addressed as A2 = 0xEA551C + idx*0xE4 = entry + 0xE4:
 * (-0xBE,A2) = +0x26 debugger_idx, (-0xBA,A2) = +0x2A flags,
 * (-0xB9,A2) = low byte of flags, (-0x78,A2) = +0x6C cr_rec_2,
 * (-0x4E,A2) = +0x96 asid, (-0x4A,A2) = +0x9A level1_pid.
 *
 * Callers: PROC2_$DELETE 0x00E744BA, PROC2_$WAIT_REAP_CHILD 0x00E3FB68,
 * PROC2_$WAIT_TRY_ZOMBIE 0x00E3FD54, PROC2_$UNDEBUG 0x00E4188C.
 *
 * Original address: 0x00e41a24
 */

#include "proc2/proc2_internal.h"

/*
 * Constant cells passed by reference to XPD_$WRITE:
 *   0x00E41ABC  00 00 00 00   `pea (0x3e,PC)`  at 0x00E41A7C -> argument 4,
 *                             the source longword (zero)
 *   0x00E41A20  00 00 00 01   `pea (-0x62,PC)` at 0x00E41A80 -> argument 3,
 *                             the byte count -- the cell DEBUG_SETUP_INTERNAL
 *                             owns (PROC2_$DEBUG_XPD_WRITE_LEN)
 */
static const uint32_t proc2_debug_clear_write_value = 0;   /* 0x00E41ABC */

void DEBUG_CLEAR_INTERNAL(int16_t proc_idx, int8_t flag)
{
    proc2_info_t *entry;    /* A2 (biased) */
    status_$t status;       /* A6-0x8 */

    /* 0x00E41A3A-0x00E41A46 */
    entry = P2_INFO_ENTRY(proc_idx);

    /* 0x00E41A4A: tst.w (-0xbe,A2) / beq exit -- not being debugged */
    if (entry->debugger_idx == 0) {
        return;
    }

    /* 0x00E41A50-0x00E41A58 (result slot pushed, nothing read) */
    DEBUG_UNLINK_FROM_LIST(proc_idx);

    /* 0x00E41A5A: bclr.b #0x4,(-0xb9,A2) -- flags &= ~0x0010 */
    entry->flags &= (uint16_t)~0x0010;

    /* 0x00E41A60/0x00E41A64: btst.l #0xd,D0 on the flags word -- zombie */
    if ((entry->flags & PROC2_FLAG_ZOMBIE) != 0) {
        /* 0x00E41A6A: pea (0x8,A6) -- address of the proc_idx argument */
        PROC2_$AWAKEN_GUARDIAN(&proc_idx);             /* 0x00E41A6E */
        return;                                        /* 0x00E41A72 */
    }

    /* 0x00E41A74: tst.b D3b / bpl exit -- Domain boolean */
    if (flag < 0) {
        /*
         * 0x00E41A78-0x00E41A90, pushes right to left:
         *   pea (-0x8,A6)           arg 5  &status
         *   pea 0x00E41ABC          arg 4  &0
         *   pea 0x00E41A20          arg 3  &1
         *   movea.l (-0x78,A2),A0 ; pea (0x90,A0)
         *                           arg 2  VA cr_rec_2 + 0x90, by value
         *   pea (-0x4e,A2)          arg 1  &entry->asid
         */
        XPD_$WRITE(&entry->asid,
                   ARCH_VA_TO_PTR(entry->cr_rec_2 + 0x90),
                   &PROC2_$DEBUG_XPD_WRITE_LEN,
                   &proc2_debug_clear_write_value, &status);

        /* 0x00E41A9A: bclr.b #0x4,(-0xb9,A2) again */
        entry->flags &= (uint16_t)~0x0010;

        /* 0x00E41AA0-0x00E41AAA: PROC1_$RESUME(entry->level1_pid, &status) */
        PROC1_$RESUME(entry->level1_pid, &status);
    }
}
