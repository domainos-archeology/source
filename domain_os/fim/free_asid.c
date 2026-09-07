/*
 * FIM_$FREE_ASID - release the FIM per-address-space state for a process
 *
 * Called by PROC2_$DELETE_CLEANUP (0x00E743CE) at 0x00E749DA with
 * `pea (-0xb0,A6)`, a word local that PROC2_$DELETE_CLEANUP loads from the
 * process record at 0x00E74408/0x00E74410 -- so, as with FIM_$INIT_ASID, the
 * argument is an address-space id passed by reference (Pascal `var`/`in`
 * parameter), a word; see fim/init_asid.c on the signedness of the
 * prototype.
 *
 * Original address: 0x00E0AA6C
 * Size: 58 bytes
 *
 * Assembly (0x00E0AA6C):
 *   00e0aa6c  link.w   A6,-0x4
 *   00e0aa70  movem.l  {A5 D2},-(SP)
 *   00e0aa74  lea      (0xe2126c).l,A5      ; A5 = FIM module data base
 *   00e0aa7a  movea.l  (0x8,A6),A0          ; A0 = &as_id
 *   00e0aa7e  subq.l   #0x2,SP              ; word argument in a longword slot
 *   00e0aa80  move.w   (A0),D2w             ; D2w = as_id
 *   00e0aa82  move.w   D2w,-(SP)
 *   00e0aa84  jsr      0x00e22890.l         ; FIM_$CLEAR_TRACE_FAULT(as_id)
 *   00e0aa8a  move.w   D2w,D0w
 *   00e0aa8c  lsl.w    #0x2,D0w             ; D0w = as_id * 4
 *   00e0aa8e  clr.l    (0x3c,A5,D0w*0x1)    ; FIM_$USER_FIM_ADDR[as_id] = nil
 *   00e0aa92  movea.l  #0xe2248a,A0         ; A0 = FIM_$QUIT_INH
 *   00e0aa98  st       (0x0,A0,D2w*0x1)     ; FIM_$QUIT_INH[as_id] = true (0xFF)
 *   00e0aa9c  movem.l  (-0xc,A6),{D2 A5}
 *   00e0aaa2  unlk     A6
 *   00e0aaa4  rts
 *
 * FIM_DATA_BASE + 0x3C is FIM_$USER_FIM_ADDR (0x00E212A8): the same cell
 * FIM_$INSTALL (0x00E0A9C2) writes and FIM_$GET_FIM_ADDR (0x00E0AA04)
 * reads.  Dropping it here is what makes the next FIM_$INSTALL in that
 * address space look like a first install.
 *
 * As in FIM_$INIT_ASID the scaling is 16-bit and the scaled value is used as
 * a signed word index; the original performs no bounds check.
 */

#include "fim/fim_internal.h"

void FIM_$FREE_ASID(int16_t *as_id_p)
{
    int16_t as_id;

    /* 0x00E0AA80: the word is loaded once and kept in D2 across the call. */
    as_id = *as_id_p;

    FIM_$CLEAR_TRACE_FAULT(as_id);

    /* 0x00E0AA8E */
    FIM_$USER_FIM_ADDR[as_id] = NULL;

    /* 0x00E0AA98: "st" stores 0xFF -- Pascal true. */
    FIM_$QUIT_INH[as_id] = -1;
}
