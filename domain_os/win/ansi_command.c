/*
 * win/ansi_command.c - WIN_$ANSI_COMMAND (0x00E19128, 94 bytes)
 *
 * Issues one ANSI command to a Winchester unit's controller block and
 * waits for it.  Commands numbered 0x40 and above carry a parameter byte
 * in; those below 0x3F return one.
 *
 * Frame (link.w A6,-0x18; A2 D4 D3 D2 saved), A5 = the WIN module base
 * (0xE2B89C, established by the caller - this routine does not load it):
 *   (0x8,A6)   unit           word
 *   (0xa,A6)   ansi_cmd       word
 *   (0xc,A6)   ansi_in_param  address of the input byte
 *   (0x10,A6)  ansi_out_param address of the output byte
 *   D3         has_input      `scc` after `cmpi.w #0x40`: 0xFF when
 *                             ansi_cmd >= 0x40 (unsigned)
 *   A2         the unit's register block (unit record +4, unit*12)
 */

#include "win/win_internal.h"

status_$t WIN_$ANSI_COMMAND(uint16_t unit, uint16_t ansi_cmd,
                            char *ansi_in_param, char *ansi_out_param)
{
    volatile uint8_t *regs;             /* A2 */
    int8_t has_input;                   /* D3 */
    status_$t status;                   /* D0 */

    /* 0x00E19134-0x00E1913C */
    has_input = (ansi_cmd >= 0x40) ? -1 : 0;

    /* 0x00E1913E-0x00E19152: the register block, then the command byte. */
    regs = WIN_UNIT_REGS(unit);
    regs[WIN_REG_COMMAND] = (uint8_t)ansi_cmd;

    /* 0x00E19154-0x00E1915C */
    if (has_input < 0) {
        regs[WIN_REG_PARAM] = (uint8_t)*ansi_in_param;
    }

    /* 0x00E19160-0x00E1916E: go (5 = ANSI command), then wait.  The word
     * result slot is popped with the argument; D0 is the status. */
    regs[WIN_REG_GO] = 5;
    status = WAIT_FOR_CONTROLLER(unit);

    /* 0x00E19170-0x00E19178: the output byte, whatever the status. */
    if (has_input >= 0) {
        *ansi_out_param = (char)regs[WIN_REG_PARAM];
    }

    return status;
}
