/*
 * DEBUG_SETUP_INTERNAL - Attach a target process to a debugger
 *
 * Re-emitted from the image (0x00E4194C..0x00E41A1A, 208 bytes).
 *
 * Links `target` at the head of `debugger`'s debug-target list (unlinking
 * it from any previous debugger first), resets the target's ptrace
 * options, optionally pokes a longword into the target's creation record
 * through XPD_$WRITE, and wakes the target's guardian around the change
 * when the target is in fault mode.
 *
 * Frame (link.w A6,-0x24):
 *   (0x8,A6)  target_idx    word  -> D2
 *   (0xA,A6)  debugger_idx  word  -> D3
 *   (0xC,A6)  flag          byte  -> D4  (high byte of the word slot;
 *                                         callers push `st`/`clr.w`)
 *   A6-0x18   14-byte copy of the ptrace option record
 *   A6-0x1C   status for XPD_$WRITE (never examined)
 *
 * Both entries are addressed as A_n = 0xEA551C + idx*0xE4 = entry + 0xE4:
 * (-0xBE,A2) = +0x26 debugger_idx, (-0xBC,A2) = +0x28 next target,
 * (-0xC0,A3) = +0x24 first target, (-0xB9,A2) = low byte of flags (+0x2A),
 * (-0x16,A2) = +0xCE ptrace_opts, (-0x78,A2) = +0x6C cr_rec_2,
 * (-0x4E,A2) = +0x96 asid.
 *
 * Callers: PROC2_$CREATE 0x00E7295A, PROC2_$FORK 0x00E73070,
 * PROC2_$DEBUG 0x00E416F4, PROC2_$OVERRIDE_DEBUG 0x00E417E2.
 *
 * Original address: 0x00e4194c
 */

#include "proc2/proc2_internal.h"

/*
 * Constant cells in the code region, passed by reference to XPD_$WRITE
 * (cell = pea address + 2 + displacement; bytes read from the image):
 *
 *   0x00E41A1C  ff ff ff ff   `pea (0x36,PC)` at 0x00E419E4  -> argument 4,
 *                             the source longword
 *   0x00E41A20  00 00 00 01   `pea (0x36,PC)` at 0x00E419E8  -> argument 3,
 *                             the byte count.  DEBUG_CLEAR_INTERNAL reuses
 *                             this same cell (`pea (-0x62,PC)` at 0x00E41A80),
 *                             so it is defined once here and declared in
 *                             proc2_internal.h.
 */
static const uint32_t proc2_debug_setup_write_value = 0xFFFFFFFFu;   /* 0x00E41A1C */
const int32_t PROC2_$DEBUG_XPD_WRITE_LEN = 1;                        /* 0x00E41A20 */

void DEBUG_SETUP_INTERNAL(int16_t target_idx, int16_t debugger_idx, int8_t flag)
{
    proc2_info_t *target_entry;      /* A2 (biased) */
    proc2_info_t *debugger_entry;    /* A3 (biased) */
    xpd_$ptrace_opts_t local_opts;   /* A6-0x18 */
    status_$t status;                /* A6-0x1C */
    int i;

    /* 0x00E41966-0x00E4197C */
    target_entry = P2_INFO_ENTRY(target_idx);
    debugger_entry = P2_INFO_ENTRY(debugger_idx);

    /* 0x00E41980: tst.w (-0xbe,A2) -- already debugged: unlink first */
    if (target_entry->debugger_idx != 0) {
        DEBUG_UNLINK_FROM_LIST(target_idx);            /* 0x00E4198A */
    }

    /* 0x00E41990-0x00E4199A: push target onto the debugger's list head */
    target_entry->debugger_idx = (uint16_t)debugger_idx;
    target_entry->next_debug_target_idx = debugger_entry->first_debug_target_idx;
    debugger_entry->first_debug_target_idx = (uint16_t)target_idx;

    /*
     * 0x00E4199E: btst.b #0x4,(-0xb9,A2) -- bit 4 of the low byte of the
     * flags word at +0x2A, i.e. flags & 0x0010 (fault mode).
     * 0x00E419A6: pea (0x8,A6) -- the address of the target_idx argument.
     */
    if ((target_entry->flags & 0x0010) != 0) {
        PROC2_$AWAKEN_GUARDIAN(&target_idx);           /* 0x00E419AA */
    }

    /*
     * 0x00E419B0-0x00E419BE: copy 4+4+4+2 = 14 bytes from entry+0xCE to
     * the local, 0x00E419C4 reset it, 0x00E419CC-0x00E419DA copy it back.
     */
    for (i = 0; i < 14; i++) {
        ((uint8_t *)&local_opts)[i] = target_entry->ptrace_opts[i];
    }
    XPD_$RESET_PTRACE_OPTS(&local_opts);
    for (i = 0; i < 14; i++) {
        target_entry->ptrace_opts[i] = ((uint8_t *)&local_opts)[i];
    }

    /* 0x00E419DC: tst.b D4b / bpl -- Domain boolean, true when negative */
    if (flag < 0) {
        /*
         * 0x00E419E0-0x00E419F8, pushes right to left:
         *   pea (-0x1c,A6)          arg 5  &status
         *   pea 0x00E41A1C          arg 4  &0xFFFFFFFF
         *   pea 0x00E41A20          arg 3  &1
         *   movea.l (-0x78,A2),A0 ; pea (0x90,A0)
         *                           arg 2  VA cr_rec_2 + 0x90, by value
         *   pea (-0x4e,A2)          arg 1  &target->asid
         */
        XPD_$WRITE(&target_entry->asid,
                   ARCH_VA_TO_PTR(target_entry->cr_rec_2 + 0x90),
                   &PROC2_$DEBUG_XPD_WRITE_LEN,
                   &proc2_debug_setup_write_value, &status);
    }

    /* 0x00E41A02: the same fault-mode test, guardian woken again */
    if ((target_entry->flags & 0x0010) != 0) {
        PROC2_$AWAKEN_GUARDIAN(&target_idx);           /* 0x00E41A0E */
    }
}
