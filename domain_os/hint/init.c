/*
 * HINT_$INIT - Initialize the hint subsystem
 *
 * Opens or creates the hint file (//node_data/hint_file), maps it into
 * memory, and initializes the hint data structures if needed.
 *
 * Original address: 0x00E3122C
 */

#include "hint/hint_internal.h"

/* Static path data for hint file */
static char hint_file_path[] = "`node_data/hint_file";
static int16_t hint_file_path_length = 20;

/* Lock mode parameters for FILE_$LOCK */
static uint16_t hint_lock_index = 0;
static uint16_t hint_lock_mode = 0;
static uint16_t hint_lock_rights = 0;

void HINT_$INIT(void)
{
    int8_t retry_flag;
    uid_t hintfile_uid;
    status_$t status;
    uint8_t map_result[12];  /* Result from MST_$MAPS */
    uint8_t lock_result[4];  /* Result from FILE_$LOCK */
    hint_file_t *mapped_ptr;

    retry_flag = 0;
    HINT_$HINTFILE_PTR = NULL;

    /* Initialize HINT_$HINTFILE_UID to NIL */
    HINT_$HINTFILE_UID.high = UID_$NIL.high;
    HINT_$HINTFILE_UID.low = UID_$NIL.low;

    /* Main initialization loop - retries on failure */
    for (;;) {
        /* Try to resolve the hint file path */
        NAME_$RESOLVE(hint_file_path, &hint_file_path_length, &hintfile_uid, &status);

        if (status != status_$ok) {
            /* File doesn't exist - try to create it */
            NAME_$CR_FILE(hint_file_path, &hint_file_path_length, &hintfile_uid, &status);

            if (status != status_$ok) {
                goto handle_failure;
            }
        }

        /* Map the hint file into memory
         * Parameters:
         *   0: ASID (0 = current)
         *   0xFF00: Flags (FF = read/write)
         *   &hintfile_uid: File UID to map
         *   0: Start offset
         *   0x7FFF: Maximum size (32KB)
         *   0x16: Area ID / protection
         *   0: Hint address
         *   0xFF: Create flag
         *   map_result: Output buffer
         *   &status: Status return
         */
        mapped_ptr = (hint_file_t *)MST_$MAPS(0, (int16_t)0xFF00, &hintfile_uid, 0,
                                               0x7FFF, 0x16, 0, (int8_t)0xFF,
                                               map_result, &status);

        if (status != status_$ok) {
            goto handle_failure;
        }

        /* Lock the hint file */
        FILE_$LOCK(&hintfile_uid, &hint_lock_index, &hint_lock_mode,
                   (uint8_t *)&hint_lock_rights, 0, &status);

        /* Check if this is an uninitialized file (version == 1) */
        if (HINT_$HINTFILE_PTR->header.version == HINT_FILE_UNINIT) {
            /* File already exists but uninitialized - return without saving state */
            return;
        }

        /* Save the hint file state */
        HINT_$HINTFILE_PTR = mapped_ptr;
        HINT_$HINTFILE_UID.high = hintfile_uid.high;
        HINT_$HINTFILE_UID.low = hintfile_uid.low;

        /* If version is not 7 (fully initialized), clear and reinitialize */
        if (HINT_$HINTFILE_PTR->header.version != HINT_FILE_VERSION) {
            HINT_$clear_hintfile();
        }

        /* Check if network info matches current node */
        {
            uint16_t *net_info = (uint16_t *)&HINT_$HINTFILE_PTR->header.net_info;
            uint16_t *route_info = (uint16_t *)(ROUTE_$PORTP + 0x2E);

            if (net_info[0] == route_info[0] && net_info[1] == route_info[1]) {
                /* Network matches - save the port */
                ROUTE_$PORT = HINT_$HINTFILE_PTR->header.net_port;
            } else {
                /* Network mismatch - clear the port */
                ROUTE_$PORT = 0;
            }
        }

        return;

handle_failure:
        if (retry_flag < 0) {
            /* Already retried once - give up */
            HINT_$HINTFILE_PTR = NULL;
            return;
        }

        /* First failure - try to clean up and retry */
        NAME_$DROP(hint_file_path, &hint_file_path_length, &hintfile_uid, &status);

        if (status == status_$ok) {
            FILE_$DELETE(&hintfile_uid, &status);
        }

        retry_flag = -1;  /* Mark that we've retried */
        /* Loop back and try again */
    }
}
