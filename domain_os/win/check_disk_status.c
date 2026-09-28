/*
 * win/check_disk_status.c - WIN_$CHECK_DISK_STATUS (0x00E19276, 404 bytes)
 *
 * Reads a unit's controller status word, turns its error bits into a
 * "primary" status (D3), then - when the word says the drive has general
 * status to report - fetches that (REPORT GENERAL STATUS, or the byte the
 * controller left in its command register), records it, lets
 * win_$sense_general_status refine it into a "secondary" status (D6),
 * issues the clearing command, and classifies the general-status byte.
 * The primary status wins when both are set (0x00E193F6-0x00E193FE).
 *
 * Frame (link.w A6,-0x20; A2 D6 D5 D4 D3 D2 saved), A5 = module base
 * (established by the caller):
 *   A6-0x1A  2  disk_status   the controller status word (regs +6);
 *                             0x00E192C0.. `btst.b #n,(-0x19,A6)` are bits
 *                             of its LOW byte
 *   A6-0x18  2  ext_status    the general-status byte in the HIGH byte
 *                             (`move.b (A2),(-0x18,A6)`; the ANSI output
 *                             byte lands there too); `btst.b #n,(-0x18,A6)`
 *                             at 0x00E193A2 / 0x00E193D0 read that byte
 *   A6-0x16  2  out           scratch output cell for the clearing commands
 *   A6-0x12  2  clear_cmd     win_$sense_general_status's output word
 *   A2          the unit's register block
 *   D2          the fallback clearing command (2 after REPORT GENERAL STATUS)
 *   D3          primary status
 *   D4          unit
 *   D5          the clearing command actually sent
 *   D6          secondary status
 */

#include "win/win_internal.h"
#include "misc/crash_system.h"

/*
 * The two CRASH_SYSTEM status cells, in the module's code region right
 * after WIN_ANSI_IN_PARAM (gsk read 0xe19408: 4e 75 | 00 00 | 00 08 00 04 |
 * 00 08 00 22).
 */
/* 0x00E1940C: pea (0x142,PC) at 0x00E192C8 (0x00E192CA + 0x142) */
static const status_$t win_$crash_controller_error = status_$disk_controller_error;
/* 0x00E19410: pea (0x24,PC) at 0x00E193EA (0x00E193EC + 0x24) */
static const status_$t win_$crash_driver_logic_error = status_$disk_driver_logic_error;

status_$t WIN_$CHECK_DISK_STATUS(uint16_t unit)
{
    uint8_t *win_data = WIN_DATA_BASE;
    volatile uint8_t *regs;             /* A2 */
    uint16_t disk_status;               /* A6-0x1A */
    uint16_t ext_status;                /* A6-0x18 */
    char gen_status;                    /* the ANSI output byte, = A6-0x18 */
    char out[2];                        /* A6-0x16 */
    uint16_t clear_cmd;                 /* A6-0x12 */
    uint16_t fallback_cmd;              /* D2 */
    status_$t primary;                  /* D3 */
    uint16_t cmd;                       /* D5 */
    status_$t secondary;                /* D6 */
    uint16_t bits;                      /* D1 / D0 */

    /* 0x00E19282-0x00E192A4: the ext-status word at +0x6C is cleared as a
     * BYTE (`clr.b (0x6c,A5)` - its high byte), the status word read and
     * mirrored to +0x6E. */
    fallback_cmd = 0;
    primary = status_$ok;
    ext_status = 0;     /* the image leaves the low byte uninitialised */
    win_data[WIN_EXT_STATUS_OFFSET] = 0;
    regs = WIN_UNIT_REGS(unit);
    disk_status = *(volatile uint16_t *)(regs + WIN_REG_STATUS);
    *(uint16_t *)(win_data + WIN_DISK_STATUS_OFFSET) = disk_status;

    /* 0x00E192AA-0x00E19326: bit 11 = the command completed: idle the
     * controller and classify the error bits 0xFA of the low byte. */
    if ((disk_status & 0x0800) != 0) {
        regs[WIN_REG_GO] = 0;
        if ((disk_status & 0x00FA) != 0) {
            if ((disk_status & 0x0008) != 0) {
                CRASH_SYSTEM(&win_$crash_controller_error);     /* 0x00E192CC */
            } else if ((disk_status & 0x0010) != 0) {
                (*(uint16_t *)(win_data + 0x4E))++;
                primary = status_$disk_equipment_check;
            } else if ((disk_status & 0x0002) != 0) {
                primary = status_$memory_parity_error_during_disk_write;
            } else if ((disk_status & 0x0040) != 0) {
                (*(uint16_t *)(win_data + 0x54))++;
                primary = status_$DMA_overrun;
            } else if ((disk_status & 0x0020) != 0) {
                (*(uint16_t *)(win_data + 0x52))++;
                primary = status_$disk_data_check;
            } else {
                (*(uint16_t *)(win_data + 0x48))++;
                primary = status_$disk_not_ready;
            }
        }
    }

    /* 0x00E1932C-0x00E19364: where the general status comes from. */
    if ((disk_status & 0x2000) != 0) {
        /* bit 13: ask the drive; the output byte lands in ext_status's high
         * byte.  A failure goes straight to the final choice. */
        secondary = WIN_$ANSI_COMMAND(unit, ANSI_CMD_REPORT_GENERAL_STATUS,
                                      (char *)&WIN_ANSI_IN_PARAM, &gen_status);
        ext_status = (uint16_t)((ext_status & 0x00FF) | ((uint8_t)gen_status << 8));
        if (secondary != status_$ok) {
            goto choose;
        }
        fallback_cmd = 2;
    } else if ((disk_status & 0x1000) != 0) {
        /* bit 12: the controller's command register holds it. */
        ext_status = (uint16_t)((ext_status & 0x00FF) | (regs[WIN_REG_COMMAND] << 8));
    } else {
        secondary = status_$ok;
        goto choose;
    }

    /* 0x00E19368-0x00E1937E: record it, then refine. */
    *(uint16_t *)(win_data + WIN_EXT_STATUS_OFFSET) = ext_status;
    secondary = win_$sense_general_status(unit, (uint8_t)(ext_status >> 8),
                                          &clear_cmd);

    /* 0x00E19380-0x00E1939A: the clearing command - what the sense chose,
     * else the fallback, else none; its status is ignored. */
    cmd = clear_cmd;
    if (cmd == 0) {
        cmd = fallback_cmd;
    }
    if (cmd != 0) {
        (void)WIN_$ANSI_COMMAND(unit, cmd, (char *)&WIN_ANSI_IN_PARAM, out);
    }

    /* 0x00E1939E-0x00E193A0 */
    if (secondary != status_$ok) {
        goto choose;
    }

    /* 0x00E193A2-0x00E193F4: the general-status byte (high byte of the
     * word): bit 1 -> fault, cleared with CLEAR FAULT; 0x41 -> not ready;
     * 0x4C -> a state the driver cannot handle. */
    bits = (uint16_t)(ext_status >> 8);
    if ((bits & 0x02) != 0) {
        (void)WIN_$ANSI_COMMAND(unit, ANSI_CMD_CLEAR_FAULT,
                                (char *)&WIN_ANSI_IN_PARAM, out);
        (*(uint16_t *)(win_data + 0x4E))++;
        secondary = status_$disk_equipment_check;
    } else if ((bits & 0x41) != 0) {
        (*(uint16_t *)(win_data + 0x48))++;
        secondary = status_$disk_not_ready;
    } else if ((bits & 0x4C) != 0) {
        CRASH_SYSTEM(&win_$crash_driver_logic_error);          /* 0x00E193EE */
    }

choose:
    /* 0x00E193F6-0x00E193FE */
    if (primary != status_$ok) {
        return primary;
    }
    return secondary;
}
