/*
 * LOG Subsystem - Global Data Definitions
 *
 * This file defines the global state for the logging subsystem.
 */

#include "log/log_internal.h"

/* Global log state - address 0x00e2b280 */
log_state_t LOG_$STATE;

/* Path length for log file: the word 0x0024 at 0x00e3022a */
int16_t LOG_FILE_PATH_LEN = 36;

/*
 * Early log buffers.  In the original these live at fixed addresses at
 * the very start of the kernel image (0x00e00000 and 0x00e0000c) so that
 * crash/boot information survives until LOG_$INIT runs.
 */
early_log_t          EARLY_LOG;           /* 0x00e00000 */
early_log_extended_t EARLY_LOG_EXTENDED;  /* 0x00e0000c */

/* Zero-argument sentinel longword (0x00e2fffc in the original) */
uint32_t LOG_$VFMT_NO_ARG = 0;
