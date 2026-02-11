/*
 * name_$map_dir - Map a directory for fast access
 *
 * Maps a directory into the address space for direct access.
 * First checks if the object exists via AST_$GET_LOCATION, then
 * maps it using MST_$MAPS.
 *
 * Parameters:
 *   dir_uid     - UID of directory to map
 *   asid        - Address space ID to map into
 *   mapped_info - Output: mapped info structure (16 bytes)
 *                 +0x00: valid flag (0xFF if mapped)
 *                 +0x02: reserved (set to 0)
 *                 +0x04: mapped address (from MST_$MAPS A0 return)
 *                 +0x0A: entry count (set to 1)
 *                 +0x0C: address + 0x8000 (half-page offset)
 *   status_ret  - Output: status code
 *
 * Returns:
 *   0xFF if mapping successful, 0 otherwise
 *
 * Original address: 0x00e58488
 * Size: 212 bytes
 */

#include "name/name_internal.h"
#include "mst/mst.h"
#include "ast/ast.h"

/* Error message for internal failure */
extern char Naming_Internal_Err[];

boolean name_$map_dir(uid_t *dir_uid, int16_t asid, void *mapped_info,
                      status_$t *status_ret)
{
    uid_t local_uid;
    uint8_t *info = (uint8_t *)mapped_info;
    int16_t location_type;
    uint8_t location_buf[4];
    status_$t local_status;
    int32_t map_result;

    /* Copy UID locally */
    local_uid.high = dir_uid->high;
    local_uid.low = dir_uid->low;

    /* Clear valid flag initially */
    info[0] = 0;

    /* Get object location */
    AST_$GET_LOCATION(&local_uid, 0, location_buf, &location_type, &local_status);

    if (local_status != status_$ok || location_type < 0) {
        /* Object not found or error */
        *status_ret = local_status;
        return 0;
    }

    /* Map the directory */
    {
        uint32_t mapped_addr;
        MST_$MAPS(asid, 0xFF00, &local_uid, 0, 0x10000, 0x16, 0, 0xFF,
                  &map_result, status_ret);

        /* Get A0 return value (mapped address) - stored by MST_$MAPS */
        mapped_addr = (uint32_t)(uintptr_t)map_result;  /* Simplified - actual A0 */
        *(uint32_t *)(info + 4) = mapped_addr;

        if ((*status_ret >> 16) != 0) {
            /* Mapping failed */
            return 0;
        }

        if (map_result != 0x10000) {
            /* Unexpected map size */
            CRASH_SYSTEM(&Naming_Internal_Err);
        }

        /* Set up mapped info structure */
        info[0] = 0xFF;                          /* Valid flag */
        *(uint16_t *)(info + 2) = 0;             /* Reserved */
        *(uint16_t *)(info + 10) = 1;            /* Entry count */
        *(uint32_t *)(info + 12) = mapped_addr + 0x8000;  /* Half-page offset */
    }

    return 0xFF;
}
