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
 * REM_FILE_$BUSY_RETRY_COUNT - "server busy" replies retried
 * Address: 0x00E82400 (A5+0x04)
 */
uint32_t REM_FILE_$BUSY_RETRY_COUNT;

/*
 * REM_FILE_$COMPLETION_TIME - base response allowance, in clock ticks
 * Address: 0x00E82404 (A5+0x08)
 */
uint16_t REM_FILE_$COMPLETION_TIME = 20;
