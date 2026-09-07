/*
 * rem_file/rem_file_data.c - REM_FILE module data
 *
 * The REM_FILE module data block starts at 0x00E823FC; every REM_FILE_
 * routine establishes it with `lea (0xe823fc).l,A5` (REM_FILE_$SERVER at
 * 0x00E6358E, REM_FILE_$RN_DO_OP at 0x00E61540, and so on).
 * REM_FILE_$SEND_REQUEST itself never loads A5 -- it inherits it from its
 * callers, all of which use that same base -- so the two cells below are
 * ordinary module globals, not per-process data.
 *
 * `gsk read 0xe823fc 16`:
 *   00e823fc  00 00 00 00 00 00 00 00  00 14 00 00 00 10 00 02
 *
 * so A5+0x00 and A5+0x04 start at zero and A5+0x08 ships as 0x0014.
 */

#include "rem_file/rem_file_internal.h"

/*
 * REM_FILE_$STALE_LINK_COUNT - stale-directory-entry replies the server has
 * sent (`addq.l #1,(A5)` at 0x00E63E9E).
 * Address: 0x00E823FC (A5+0x00), the first longword of the map segment
 * "D E823FC REM_FILE size = C" (0x00E823FC..0x00E82408).
 */
uint32_t REM_FILE_$STALE_LINK_COUNT;

/*
 * REM_FILE_$BUSY_RETRY_COUNT - "server busy" replies retried
 * Address: 0x00E82400 (A5+0x04)
 */
uint32_t REM_FILE_$BUSY_RETRY_COUNT;

/*
 * REM_FILE_$COMPLETION_TIME - base response allowance, in clock ticks
 * Address: 0x00E82404 (A5+0x08)
 */
uint16_t REM_FILE_$COMPLETION_TIME = 20;

/*
 * ============================================================================
 * REM_FILE_$DATA - the wired data segment at 0x00E2E380
 * ============================================================================
 *
 * Map: "D33 E2E380 REM_FILE_$DATA loaded at 12FB80, size = 7C", i.e.
 * 0x00E2E380..0x00E2E3FC, with the next segment (SMD_$WIRED_DATA) following.
 * It holds exactly three objects: two 0x1E-byte packet-info templates and the
 * 0x40-byte crash message.  `gsk read 0x00E2E380 0x7C`.
 */

/*
 * DAT_00e2e380 - packet-info template for the client side, 0x00E2E380,
 * 0x1E bytes (REM_FILE_$SERVER_PKT_INFO starts at 0x00E2E39E).  It differs
 * from the server template only in its first word.
 */
uint8_t DAT_00e2e380[0x1E] = {
    0x00, 0x10, 0x00, 0x02, 0x00, 0x02, 0x80, 0x31,
    0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/*
 * REM_FILE_$SERVER_PKT_INFO - packet-info template REM_FILE_$SERVER hands
 * PKT_$SEND_INTERNET (0x00E64236).  0x00E2E39E, 0x1E bytes
 * (REM_FILE_$DISKLESS_CRASH_MSG starts at 0x00E2E3BC).
 */
uint8_t REM_FILE_$SERVER_PKT_INFO[0x1E] = {
    0x00, 0x20, 0x00, 0x02, 0x00, 0x02, 0x80, 0x31,
    0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/*
 * REM_FILE_$DISKLESS_CRASH_MSG - 0x00E2E3BC, the last 0x40 bytes of
 * REM_FILE_$DATA (0x00E2E3BC..0x00E2E3FC).  A Domain format string; the
 * trailing '%' is the VFMT end-of-format marker, so there is no NUL and the
 * array is sized exactly.
 */
char REM_FILE_$DISKLESS_CRASH_MSG[0x40] =
    "*** diskless partner node has crashed and rebooted - so must we%";

/*
 * ============================================================================
 * Literal cells in the REM_FILE code region
 * ============================================================================
 *
 * Domain Pascal passes VAR and const parameters by address, so each literal
 * argument becomes a cell in the code region whose address is pushed with
 * `pea (d,PC)`.  All four sit inside "I E60FD8 REM_FILE size = 35C0"; the map
 * exports no symbol for them.
 */

/* 0x00E61718: the word 8, immediately after an `rts` at 0x00E61714.
 * Image bytes: 00 08. */
uint16_t REM_FILE_$MAX_PROJ_LIST = 8;

/* 0x00E62D48: the word 0x20, immediately after an `rts` at 0x00E62D44.
 * Image bytes: 00 20. */
uint16_t REM_FILE_$MAX_NAME_LEN = 0x20;

/* 0x00E61D18: the longword 0, between an `rts` at 0x00E61D14 and
 * REM_FILE_$UNLOCK at 0x00E61D1C.  Image bytes: 00 00 00 00. */
uint32_t REM_FILE_$NIL_CONST = 0;

/* 0x00E64592: the status 0x000F0004, after an `rts` at 0x00E64590 and just
 * before the ASKNODE segment at 0x00E64598.  Ghidra labels it
 * File_Comms_Problem_With_Remote_Node_Err.  Image bytes: 00 0F 00 04. */
status_$t REM_FILE_$COMMS_PROBLEM_STATUS = 0x000F0004;

/*
 * ============================================================================
 * Socket lock
 * ============================================================================
 */

/*
 * REM_FILE_$SOCK_LOCK - 0x00E24B3C, inside the map segment
 * "D E248FC NETWORK size = 364"; the map names it explicitly and the next
 * symbol is NETWORK_$PAGING_BACKLOG at 0x00E24BAC.  An ml_$exclusion_t is
 * 0x12 bytes and the image holds zeroes for all of them (the eventcount pair
 * whose self-pointers appear at 0x00E24B54 and 0x00E24B64 is separate,
 * unnamed NETWORK data).
 */
ml_$exclusion_t REM_FILE_$SOCK_LOCK = { 0 };

