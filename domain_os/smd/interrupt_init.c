/*
 * smd/interrupt_init.c - SMD_$INTERRUPT_INIT implementation
 *
 * Install the display interrupt handler.
 *
 * Original address: 0x00E27284 (36 bytes, 0x00E27284..0x00E272A7)
 *
 * Assembly:
 *   00e27284    lea (-0x366,PC),A0          ; A0 = SMD_$DISP1_INT (0x00E26F20)
 *   00e27288    lea (0xe24c78).l,A1         ; A1 = PEB global block
 *   00e2728e    tst.b (0x1a,A1)             ; PEB_GLOBALS.installed
 *   00e27292    beq.w 0x00e272a0
 *   00e27296    move.l A0,(0x00e24478).l    ; PEB_$DISP_INT_ADDR = handler
 *   00e2729c    bra.w 0x00e272a6
 *   00e272a0    move.l A0,(0x00000070).l    ; autovector 4 = handler
 *   00e272a6    rts
 *
 * The two raw literals are named objects, not anonymous addresses:
 *
 *   0x00E24C78 + 0x1A = 0x00E24C92 is peb_globals_t.installed, which
 *   peb/peb.h exports as PEB_$INSTALLED_FLAG.  Note the test is `tst.b` +
 *   `beq`, i.e. "== 0", not the usual Domain-boolean `< 0`.
 *
 *   0x00000070 is m68k exception vector 28.  With VBR = 0 the vector table
 *   is the first 1KB of memory, and vectors 25..31 (0x64..0x7C) are the
 *   level-1..7 autovectors, so 0x70 is the level-4 autovector -
 *   ARCH_AUTOVECTOR(4) (arch/arch.h).
 */

#include "smd/smd_internal.h"
#include "peb/peb.h"

/* The display interrupt runs on bus interrupt level 4 (vector 0x70/4 = 28). */
#define SMD_DISP_INT_LEVEL 4

/*
 * SMD_$INTERRUPT_INIT - Install the display interrupt handler
 *
 * When the PEB is installed the handler address goes into the PEB's own
 * display-interrupt cell, PEB_$DISP_INT_ADDR (0x00E24478), because the PEB's
 * level-4 handler (peb/sau2/int.s) dispatches through it.  Otherwise it goes
 * straight into the level-4 autovector.
 *
 * Called from SMD_$COLD_INIT (0x00E34E82).
 */
void SMD_$INTERRUPT_INIT(void)
{
    if (PEB_$INSTALLED_FLAG == 0) {
        /* 0x00e272a0 */
        ARCH_AUTOVECTOR(SMD_DISP_INT_LEVEL) = (void *)&SMD_$DISP1_INT;
    } else {
        /* 0x00e27296 */
        PEB_$DISP_INT_ADDR = (void **)&SMD_$DISP1_INT;
    }
}
