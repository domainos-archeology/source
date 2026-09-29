/*
 * FIM_$INSTALL - Install a user-mode fault handler for the current AS
 *
 * TRAP #1 subcode 0x02.  The single argument is a pointer to the new
 * handler address (a Pascal `var`/by-reference longword); the previous
 * handler address is returned in D0.  The first handler installed for an
 * address space also lifts the quit inhibit for that AS: until a fault
 * handler exists there is nobody to deliver a quit to.
 *
 * Original address: 0x00e0a9c2
 * Size: 66 bytes
 *
 * Assembly (0x00e0a9c2):
 *   link.w   A6,-0x8
 *   movem.l  {A5 D2},-(SP)
 *   lea      (0xe2126c).l,A5           ; A5 = FIM module data base
 *   move.w   (0x00e2060a).l,D2w        ; D2w = PROC1_$AS_ID
 *   movea.l  (0x8,A6),A0               ; A0 = new_addr (by reference)
 *   lsl.w    #0x2,D2w                  ; byte offset = as_id * 4
 *   move.l   (0x3c,A5,D2w*0x1),D1      ; D1 = old FIM_$DATA.user_fim_addr[as]
 *   move.l   D1,D0                     ; result = old handler
 *   move.l   (A0),(0x3c,A5,D2w*0x1)    ; FIM_$DATA.user_fim_addr[as] = *new_addr
 *   tst.l    D1
 *   bne.b    0x00e0a9fa                ; had a handler already: done
 *   move.w   (0x00e2060a).l,D2w        ; re-read the AS id, unscaled
 *   movea.l  #0xe2248a,A1
 *   clr.b    (0x0,A1,D2w*0x1)          ; FIM_$WIRED_DATA.quit_inh[as] = 0
 *   movem.l  (-0x10,A6),{D2 A5}
 *   unlk     A6
 *   rts
 *
 * Note the second load of PROC1_$AS_ID at 0x00e0a9ea: the scaled copy in
 * D2w cannot be reused as a byte index, so the global is re-read rather
 * than shifted back.  It is reproduced here for fidelity.
 */

#include "fim/fim_internal.h"
#include "proc1/proc1.h"

void *FIM_$INSTALL(void **new_addr)
{
    uint16_t as_id;
    void *old_addr;

    as_id = PROC1_$AS_ID;
    old_addr = FIM_$DATA.user_fim_addr[as_id];
    FIM_$DATA.user_fim_addr[as_id] = *new_addr;

    if (old_addr == NULL) {
        /* 0x00e0a9ea: PROC1_$AS_ID is re-read here in the original. */
        FIM_$WIRED_DATA.quit_inh[PROC1_$AS_ID] = 0;
    }

    return old_addr;
}
