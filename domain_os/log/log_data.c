/*
 * LOG Subsystem - Global Data Definitions
 *
 * Original M68K addresses:
 *   LOG_$STATE               0x00E2B280  `D E2B280 LOG_ size = 1C`
 *   CRASH_$RECORD            0x00E00000  `D E00000 CRASH_RECORD size = C`
 *   LOG_$LAST_ENTRY          0x00E0000C
 *   LOG_$VFMT_NO_ARG         0x00E2FFFC  constant cells in the LOG_ code
 *   log_$logfile_path        0x00E30020  segment at 0x00E2FF7C, image bytes
 *   log_$logfile_path_len_l  0x00E30044  from `gsk read`
 *   log_$logfile_path_len    0x00E3022A
 *   log_$lock_index          0x00E30238
 *   log_$lock_mode           0x00E3023A
 *   log_$lock_rights         0x00E3023C
 */

#include "log/log_internal.h"

/* 0x00E2B280 */
log_state_t LOG_$STATE;

/* 0x00E00000 / 0x00E0000C: at the very start of the image so that they
 * survive a reboot for LOG_$INIT to find. */
crash_$record_t   CRASH_$RECORD;
log_$last_entry_t LOG_$LAST_ENTRY;

/* 0x00E2FFFC: 00 00 00 00 */
uint32_t LOG_$VFMT_NO_ARG = 0;

/* 0x00E30020: 60 6e 6f 64 ... 6c 6f 67 00 */
char log_$logfile_path[] = "`node_data/system_logs/sys_error_log";

/* 0x00E30044: 00 00 00 24 */
int32_t log_$logfile_path_len_l = 36;

/* 0x00E3022A: 00 24 */
int16_t log_$logfile_path_len = 36;

/* 0x00E30238: 00 00 | 0x00E3023A: 00 04 | 0x00E3023C: 00 00 */
uint16_t log_$lock_index = 0x0000;
uint16_t log_$lock_mode = 0x0004;
uint8_t log_$lock_rights = 0x00;
