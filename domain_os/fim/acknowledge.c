/*
 * FIM_$ACKNOWLEDGE - Acknowledge a quit for the current address space
 * Original address: 0x00e0a96c (86 bytes)
 *
 * Re-emitted from the disassembly.  The per-AS tables are indexed by
 * PROC1_$AS_ID (0xE2060A): FIM_$QUIT_EC (0xE22002, 12-byte eventcounts),
 * FIM_$QUIT_VALUE (0xE222BA, longwords), FIM_$QUIT_INH (0xE2248A, bytes),
 * FIM_$DELIV_EC (0xE224C4, 12-byte eventcounts).
 *
 * 0x00E0A96C  link.w A6,0x0 / move.l D2,-(SP)
 * 0x00E0A972  D2 = AS_ID * 12                         (lsl #2; D0 = D2*2; add)
 * 0x00E0A980  D0 = AS_ID * 4
 * 0x00E0A994  FIM_$WIRED_DATA.quit_value[as] = FIM_$WIRED_DATA.quit_ec[as].value
 *             (`move.l (0,A0,D2),(0,A1,D0)' - the eventcount's first
 *             longword)
 * 0x00E0A99A  D0 = AS_ID; FIM_$WIRED_DATA.quit_inh[as] = 0       (clr.b)
 * 0x00E0A9AA  EC_$ADVANCE(&FIM_$WIRED_DATA.deliv_ec[as])         (`pea (0,A1,D2)';
 *             jsr 0x00E206EE; no cleanup: unlk)
 * 0x00E0A9BA  move.l (-0x4,A6),D2 / unlk / rts
 *
 * PROC1_$AS_ID is re-read for every table (three loads), exactly as the
 * compiler emitted it.  Callers: 0x00E3EF1A and 0x00E3F3BC (PROC2 signal
 * acknowledge / delivery).
 *
 * The name comes from the SR10.4 link maps, whose FIM_ module lists
 * BUILD_DF, ACKNOWLEDGE, INSTALL, GET_FIM_ADDR and INIT_PID consecutively
 * in address order; in the SAU2 image FIM_$BUILD_DF is 0x00E0A458 and this
 * is the next entry (bead source-y6s0).
 */

#include "fim/fim_internal.h"
#include "proc1/proc1.h"
#include "ec/ec.h"

void FIM_$ACKNOWLEDGE(void)
{
    uint16_t as_id;

    /* 0x00E0A972..0x00E0A994 */
    as_id = PROC1_$AS_ID;
    FIM_$WIRED_DATA.quit_value[as_id] = (uint32_t)FIM_$WIRED_DATA.quit_ec[as_id].value;

    /* 0x00E0A99A..0x00E0A9A6 */
    as_id = PROC1_$AS_ID;
    FIM_$WIRED_DATA.quit_inh[as_id] = 0;

    /* 0x00E0A9AA..0x00E0A9B4 */
    EC_$ADVANCE(&FIM_$WIRED_DATA.deliv_ec[as_id]);
}
