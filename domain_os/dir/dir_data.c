/*
 * DIR data storage - Global variables for the directory subsystem
 *
 * These are runtime-initialized data areas used by DIR_$INIT
 * and various directory operations.
 */

#include "dir/dir_internal.h"

/*
 * DIR_$WAIT_ECS - Array of 32 event counters for directory slot waiting
 *
 * Each slot has an associated event counter used to signal
 * completion of pending directory operations.
 */
ec_$eventcount_t DIR_$WAIT_ECS[32];

/*
 * DIR_$WT_FOR_HDNL_EC - Event counter for wait-for-handle
 *
 * Used when all directory handles are in use and a caller
 * must wait for one to become available.
 */
ec_$eventcount_t DIR_$WT_FOR_HDNL_EC;

/*
 * DIR_$MUTEX - Main directory exclusion mutex
 *
 * Protects access to the shared directory slot table and
 * associated data structures.
 */
ml_$exclusion_t DIR_$MUTEX;

/*
 * DIR_$LINK_BUF_MUTEX - Link buffer exclusion mutex
 *
 * Protects access to the shared link name buffer used
 * during directory link operations.
 */
ml_$exclusion_t DIR_$LINK_BUF_MUTEX;
