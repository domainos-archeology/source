/*
 * FIM_$INIT_ASID - initialise the FIM per-address-space state for a process
 *
 * Called by PROC2_$INIT_ENTRY_INTERNAL (0x00E732E4) at 0x00E73314 with
 * `pea (0x96,A3)`, i.e. a pointer to proc2_info_t.asid -- so despite the
 * "PID" in the name the argument is an address-space id passed by reference
 * (Pascal `var`/`in` parameter), a word.  proc2_info_t.asid is unsigned, but
 * the prototype in fim/fim.h spells the parameter int16_t * to match the
 * signed word index the code builds from it; PROC2_$INIT_ENTRY_INTERNAL
 * casts at the call site.
 *
 * Original address: 0x00E0AA24
 * Size: 72 bytes
 *
 * Assembly (0x00E0AA24):
 *   00e0aa24  link.w   A6,-0x10
 *   00e0aa28  move.l   D2,-(SP)
 *   00e0aa2a  movea.l  (0x8,A6),A0          ; A0 = &as_id
 *   00e0aa2e  subq.l   #0x2,SP              ; word argument in a longword slot
 *   00e0aa30  move.w   (A0),D2w             ; D2w = as_id
 *   00e0aa32  move.w   D2w,-(SP)
 *   00e0aa34  jsr      0x00e22890.l         ; FIM_$CLEAR_TRACE_FAULT(as_id)
 *   00e0aa3a  move.w   D2w,D0w
 *   00e0aa3c  lsl.w    #0x2,D0w             ; D0w = as_id * 4
 *   00e0aa3e  move.w   D0w,D1w
 *   00e0aa40  add.w    D1w,D1w              ; D1w = as_id * 8
 *   00e0aa42  add.w    D1w,D0w              ; D0w = as_id * 12 (QUIT_EC stride)
 *   00e0aa44  move.w   D2w,D1w
 *   00e0aa46  movea.l  #0xe22002,A0         ; A0 = FIM_$QUIT_EC
 *   00e0aa4c  lsl.w    #0x2,D1w             ; D1w = as_id * 4 (QUIT_VALUE stride)
 *   00e0aa4e  movea.l  #0xe222ba,A1         ; A1 = FIM_$QUIT_VALUE
 *   00e0aa54  move.l   (0x0,A0,D0w*0x1),(0x0,A1,D1w*0x1)
 *   00e0aa5a  movea.l  #0xe2248a,A1         ; A1 = FIM_$QUIT_INH
 *   00e0aa60  st       (0x0,A1,D2w*0x1)     ; FIM_$WIRED_DATA.quit_inh[as_id] = true (0xFF)
 *   00e0aa64  move.l   (-0x14,A6),D2
 *   00e0aa68  unlk     A6
 *   00e0aa6a  rts
 *
 * Note that both index computations are done in 16 bits and the scaled value
 * is then used as a *signed* word index ("D0w*0x1" sign-extends), so an
 * as_id outside 0..FIM_AS_COUNT-1 would index wild memory.  The original
 * performs no bounds check; neither does this transcription.
 *
 * The counterpart is FIM_$FREE_ASID (0x00E0AA6C), which clears
 * FIM_$DATA.user_fim_addr[as_id] and re-asserts the same quit inhibit.
 * Note the asymmetry that both functions set FIM_$QUIT_INH: quits stay
 * inhibited for the AS until FIM_$INSTALL (0x00E0A9C2) puts the first user
 * fault handler in place, or FIM_$ACKNOWLEDGE (0x00E0A96C) clears it.
 */

#include "fim/fim_internal.h"

void FIM_$INIT_ASID(int16_t *as_id_p)
{
    int16_t as_id;

    /* 0x00E0AA30: the word is loaded once and kept in D2 across the call. */
    as_id = *as_id_p;

    FIM_$CLEAR_TRACE_FAULT(as_id);

    /* 0x00E0AA54: the head longword of the 12-byte eventcount is its value. */
    FIM_$WIRED_DATA.quit_value[as_id] = (uint32_t)FIM_$WIRED_DATA.quit_ec[as_id].value;

    /* 0x00E0AA60: "st" stores 0xFF -- Pascal true. */
    FIM_$WIRED_DATA.quit_inh[as_id] = -1;
}
