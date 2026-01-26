/*
 * DIR_$ADD_ENTRY_INTERNAL - Internal add entry implementation
 *
 * Shared implementation for DIR_$ADDU and DIR_$ROOT_ADDU.
 * Validates name (1-255 chars) and calls DIR_$DO_OP for remote operations.
 * Falls back to legacy DIR_$OLD_* functions if the new protocol fails.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name for the new entry
 *   name_len   - Name length (must be 1-255)
 *   file_uid   - UID of file to add
 *   flags      - Flags for root entries (0 for normal ADDU)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E500B8
 * Original size: 232 bytes
 */

#include "dir/dir_internal.h"

#if defined(M68K)
#include "arch/m68k/arch.h"
#endif

/* Status codes */
#define status_$naming_invalid_leaf        0x000E000B
#define file_$bad_reply_received           0x000F0003
#define status_$naming_bad_directory       0x000E000D

/* Directory operation codes */
#define DIR_OP_ADDU  0x2A

void DIR_$ADD_ENTRY_INTERNAL(uid_t *dir_uid, char *name, int16_t name_len,
                              uid_t *file_uid, uint32_t flags,
                              status_$t *status_ret)
{
    struct {
        uint8_t  pad[3];
        uint8_t  op;           /* Operation code */
        uint32_t uid_high;     /* Directory UID high */
        uint32_t uid_low;      /* Directory UID low */
        uint16_t type_field;   /* Type field from A5+0x2042 */
        uint8_t  name_data[128]; /* Name buffer - will hold: file_uid + flags + name */
    } request;
    Dir_$OpResponse response;
    status_$t local_status;
    int16_t i;
    int16_t req_len;

    /* Validate name length (1-255 chars) */
    if (name_len == 0 || name_len > 255) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Copy name into request buffer */
    /* Name goes at offset after file_uid and flags (at request.name_data offset) */
    for (i = 0; i < name_len; i++) {
        request.name_data[12 + i] = name[i];  /* After file_uid (8) + flags (4) */
    }

    /* Build request */
    request.op = DIR_OP_ADDU;
    request.uid_high = dir_uid->high;
    request.uid_low = dir_uid->low;

    /*
     * Type field from global at A5+0x2042
     * This appears to be a node type indicator
     */
#if defined(M68K)
    request.type_field = *(uint16_t *)((char *)__A5_BASE() + 0x2042);
#else
    extern uint16_t DAT_a5_2042;
    request.type_field = DAT_a5_2042;
#endif

    /* Copy file UID and flags into name_data area */
    *(uint32_t *)&request.name_data[0] = file_uid->high;
    *(uint32_t *)&request.name_data[4] = file_uid->low;
    *(uint32_t *)&request.name_data[8] = flags;

    /* Calculate request length */
#if defined(M68K)
    req_len = name_len + *(int16_t *)((char *)__A5_BASE() + 0x2046);
#else
    extern int16_t DAT_a5_2046;
    req_len = name_len + DAT_a5_2046;
#endif

    /* Perform directory operation */
    DIR_$DO_OP(&request.op, req_len, 0x14, &response, &request.pad);

    local_status = response.status;

    /*
     * If new protocol failed with specific errors, fall back to legacy
     */
    if (local_status == file_$bad_reply_received ||
        local_status == status_$naming_bad_directory) {

        if (flags == 0) {
            /* Normal ADDU - use legacy function */
            DIR_$OLD_ADDU(dir_uid, name, &name_len, file_uid, status_ret);
        } else {
            /* Root ADDU - use legacy function with flags */
            DIR_$OLD_ROOT_ADDU(dir_uid, name, &name_len, file_uid, &flags, status_ret);
        }
    } else {
        *status_ret = local_status;
    }
}
