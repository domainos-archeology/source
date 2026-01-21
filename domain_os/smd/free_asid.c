/*
 * smd/free_asid.c - SMD_$FREE_ASID implementation
 *
 * Frees all display resources associated with an address space ID.
 * Called when a process terminates or releases its display associations.
 *
 * Original address: 0x00E75250
 *
 * Assembly:
 *   00e75250    link.w A6,-0x10
 *   00e75254    movem.l {  A5 A2 D4 D3 D2},-(SP)
 *   00e75258    lea (0xe85718).l,A5      ; Alternate globals base (unused)
 *   00e7525e    move.w (0x8,A6),D2w      ; D2 = param ASID
 *   00e75262    clr.w D3w                ; D3 = 0 (loop counter)
 *   00e75264    movea.l #0xe2e3fc,A2     ; A2 = display units base
 *   00e7526a    moveq #0x1,D4            ; D4 = 1 (unit number)
 *   00e7526c    lea (0x10c,A2),A2        ; A2 = unit[1] base
 *   00e75270    movea.l A2,A0
 *   00e75272    cmp.w (-0xee,A0),D2w     ; Compare ASID with borrowed_asid
 *   00e75276    bne.b 0x00e7528c         ; Skip if not borrowed by this ASID
 *   00e75278    move.w D4w,(-0x6,A6)     ; Store unit number
 *   00e7527c    pea (-0x4,A6)            ; Push status_ret
 *   00e75280    pea (-0x6,A6)            ; Push &unit
 *   00e75284    jsr 0x00e6f700.l         ; Call SMD_$RETURN_DISPLAY
 *   00e7528a    addq.w #0x8,SP
 *   00e7528c    pea (-0x4,A6)            ; Push status_ret
 *   00e75290    jsr 0x00e6f97c.l         ; Call SMD_$UNMAP_DISPLAY_U
 *   00e75296    addq.w #0x4,SP
 *   00e75298    addq.w #0x1,D4w          ; unit++
 *   00e7529a    lea (0x10c,A2),A2        ; Next unit
 *   00e7529e    dbf D3w,0x00e75270       ; Loop (runs once with D3=0)
 *   00e752a2    move.w D2w,D0w           ; D0 = ASID
 *   00e752a4    movea.l #0xe82b8c,A0     ; SMD_GLOBALS base
 *   00e752aa    ext.l D0
 *   00e752ac    add.l D0,D0              ; D0 = ASID * 2
 *   00e752ae    clr.w (0x48,A0,D0*0x1)   ; Clear asid_to_unit[ASID]
 *   00e752b2    movem.l (-0x24,A6),{  D2 D3 D4 A2 A5}
 *   00e752b8    unlk A6
 *   00e752ba    rts
 *
 * This function only processes display unit 1 (loop runs once).
 * This may be a bug in the original code, or intentional if there's
 * only ever one display unit that can be borrowed.
 */

#include "smd/smd_internal.h"

/*
 * SMD_$FREE_ASID - Free display resources for an ASID
 *
 * Releases any borrowed display and unmaps display memory for the
 * specified address space ID. Called during process termination.
 *
 * Parameters:
 *   asid - Address space ID to free resources for
 *
 * Note: This function currently only checks display unit 1 for borrowed
 * status. This appears to be the behavior of the original code.
 */
void SMD_$FREE_ASID(int16_t asid)
{
    int16_t unit;
    status_$t status;
    smd_unit_aux_t *aux;

    /* Process unit 1 (loop runs once per original code) */
    unit = 1;
    aux = smd_get_unit_aux(unit);

    /* Check if this ASID has borrowed unit 1 */
    if (aux->borrowed_asid == asid) {
        /* Return the borrowed display */
        SMD_$RETURN_DISPLAY(&unit, &status);
    }

    /* Unmap display memory */
    SMD_$UNMAP_DISPLAY_U(&status);

    /* Clear the ASID to unit mapping */
    SMD_GLOBALS.asid_to_unit[asid] = 0;
}
