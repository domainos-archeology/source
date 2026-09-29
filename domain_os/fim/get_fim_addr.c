/*
 * FIM_$GET_FIM_ADDR - Return the calling address space's user fault handler
 *
 * TRAP #0 subcode 0x01 (SVC0_GET_FIM_ADDR).  Takes no arguments and
 * returns the handler address that FIM_$INSTALL last stored for the
 * current address space, or NULL if none has been installed.
 *
 * Original address: 0x00e0aa04
 * Size: 32 bytes
 *
 * Assembly (0x00e0aa04):
 *   link.w  A6,-0x4
 *   pea     (A5)                       ; save caller's module base
 *   lea     (0xe2126c).l,A5            ; A5 = FIM module data base
 *   move.w  (0x00e2060a).l,D0w         ; D0w = PROC1_$AS_ID
 *   lsl.w   #0x2,D0w                   ; byte offset = as_id * 4
 *   move.l  (0x3c,A5,D0w*0x1),D0       ; D0 = FIM_$DATA.user_fim_addr[as_id]
 *   movea.l (-0x8,A6),A5
 *   unlk    A6
 *   rts
 *
 * The result is left in D0, which is how the SVC dispatcher returns a
 * value to the user.
 */

#include "fim/fim_internal.h"
#include "proc1/proc1.h"

void *FIM_$GET_FIM_ADDR(void)
{
    return FIM_$DATA.user_fim_addr[PROC1_$AS_ID];
}
