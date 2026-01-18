/*
 * LOG Subsystem - Global Data Definitions
 *
 * This file defines the global state for the logging subsystem.
 */

#include "log/log_internal.h"

/* Global log state - address 0x00e2b280 */
log_state_t LOG_$STATE;

/* Path length for log file */
int16_t LOG_FILE_PATH_LEN = 24;
