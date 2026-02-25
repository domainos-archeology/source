/*
 * dir_$find_uid_internal - Internal find UID helper
 *
 * Shared implementation for DIR_$FIND_UID (flag=0) and
 * DIR_$FIND_NET (flag=0xFF). Sends a DO_OP request with command
 * byte 0x46 ('F') and opcode 0x11A to try the new protocol first.
 *
 * Process:
 * 1. Construct request buffer with cmd=0x46, two UIDs, and flag byte
 * 2. Call DIR_$DO_OP with opcode 0x11A
 * 3. On error (file_$bad_reply or status_$naming_bad_directory):
 *    - flag < 0: call DIR_$OLD_FIND_NET (masked to 20-bit UID)
 *    - flag >= 0: call DIR_$OLD_FIND_UID to get name
 * 4. On success:
 *    - flag < 0: return 4-byte network value in *net_ret
 *    - flag >= 0: copy name from response, truncate if needed
 *      (returns status_$naming_leaf_truncated if buffer too small)
 *
 * Parameters:
 *   dir_uid      - UID of directory to search
 *   target_uid   - UID to find
 *   flag         - Negative for network search, non-negative for UID search
 *   name_buf_len - Max buffer length for name output
 *   name_buf     - Output: name buffer
 *   name_len_ret - Output: actual name length
 *   net_ret      - Output: network address (network mode only)
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E4E786
 * Size: 246 bytes
 */

#include "dir/dir_internal.h"

/*
 * DO_OP request buffer layout for find_uid operation (cmd 0x46).
 *
 * The standard DO_OP header occupies bytes 0x00-0x8D.
 * Operation-specific data starts at offset 0x8E.
 * The req_size parameter to DIR_$DO_OP (from per-process data at
 * A5+0x20b6) specifies the size of the operation-specific portion;
 * DIR_$DO_OP adds it to 0x8E for the total request length.
 */
typedef struct __attribute__((packed)) {
    /* Standard DO_OP header (0x8E bytes) */
    uint8_t   pad[3];           /* 0x00-0x02: unused padding */
    uint8_t   cmd;              /* 0x03: operation code (0x46 = 'F' for find) */
    uint32_t  dir_uid_high;     /* 0x04-0x07: directory UID high word */
    uint32_t  dir_uid_low;      /* 0x08-0x0B: directory UID low word */
    uint8_t   reserved[2];      /* 0x0C-0x0D: reserved */
    uint16_t  version;          /* 0x0E-0x0F: protocol version (per-process) */
    uint8_t   std_gap[0x7E];    /* 0x10-0x8D: remainder of standard header */

    /* Operation-specific data (at offset 0x8E) */
    uint32_t  target_uid_high;  /* 0x8E-0x91: target UID high word */
    uint32_t  target_uid_low;   /* 0x92-0x95: target UID low word */
    uint8_t   flag;             /* 0x96: search mode flag */
    uint8_t   tail_pad;         /* 0x97: padding byte */
} dir_$find_uid_request_t;

/*
 * DO_OP response buffer layout for find_uid operation.
 *
 * The Dir_$OpResponse struct in dir_internal.h has operation-generic
 * field names. The actual layout at runtime is confirmed by the
 * assembly at 0x00E4E786 (verified field offsets against machine code):
 *   offset 0x04: status (status_$t)
 *   offset 0x14: name_length (uint16_t) - returned name length
 *   offset 0x16: net_value (uint32_t) - returned network address
 *   offset 0x1A: name_data (uint8_t[]) - returned name bytes
 *
 * Note: This uses __attribute__((packed)) to ensure the uint32_t at
 * offset 0x16 is not padded to 4-byte alignment (which would break
 * the layout on little-endian/x86 hosts).
 */
typedef struct __attribute__((packed)) {
    uint8_t   header[4];        /* 0x00-0x03: response flags */
    status_$t status;           /* 0x04-0x07: operation status */
    uint8_t   reserved[12];     /* 0x08-0x13: operation-specific data */
    uint16_t  name_length;      /* 0x14-0x15: returned name length */
    uint32_t  net_value;        /* 0x16-0x19: returned network address */
    uint8_t   name_data[256];   /* 0x1A+: returned name bytes */
} dir_$find_uid_response_t;

void dir_$find_uid_internal(uid_t *dir_uid, uid_t *target_uid, int8_t flag,
                            int16_t name_buf_len, char *name_buf,
                            int16_t *name_len_ret, uint32_t *net_ret,
                            status_$t *status_ret)
{
    /* Per-process data base pointer (M68K A5 register) */
    char *a5 = (char *)__A5_BASE();

    dir_$find_uid_request_t request;
    dir_$find_uid_response_t op_resp;

    /* resp_buf: passed as 5th arg to DIR_$DO_OP.
     * In the original assembly this is a 2-byte area at A6-0x1BA,
     * just before the request buffer. DIR_$DO_OP may update it
     * (e.g., adding name_length on success for local ops), but
     * this function never reads it back. */
    uint16_t resp_buf;

    /* Masked UID for old-protocol network search fallback */
    uint32_t masked_uid;

    /* --- Build the DO_OP request --- */
    request.cmd = 0x46;                             /* 'F' = Find UID */
    request.dir_uid_high = dir_uid->high;
    request.dir_uid_low = dir_uid->low;
    request.version = *(uint16_t *)(a5 + 0x20b2);  /* Protocol version */
    request.target_uid_high = target_uid->high;
    request.target_uid_low = target_uid->low;
    request.flag = (uint8_t)flag;

    /* --- Call DIR_$DO_OP ---
     * req_size (from A5+0x20b6) is the size of the operation-specific
     * portion starting at offset 0x8E. DIR_$DO_OP adds 0x8E internally
     * for remote requests.
     * resp_size = 0x11A (282 bytes) is the response buffer size. */
    DIR_$DO_OP(&request,
               *(int16_t *)(a5 + 0x20b6),  /* req_size */
               0x11a,                       /* resp_size */
               &op_resp,
               &resp_buf);

    /* --- Process the response --- */

    if (op_resp.status == file_$bad_reply_received_from_remote_node ||
        op_resp.status == status_$naming_bad_directory) {
        /*
         * Remote node doesn't support the new protocol, or
         * directory is in old format. Fall back to old protocol.
         */
        if (flag < 0) {
            /* Network search: mask target UID to 20-bit node ID */
            masked_uid = target_uid->low & 0xFFFFF;
            *net_ret = DIR_$OLD_FIND_NET(dir_uid, &masked_uid);
        } else {
            /* UID search: delegate to old find_uid */
            DIR_$OLD_FIND_UID(dir_uid, target_uid, name_buf,
                              name_len_ret, status_ret);
        }
    } else {
        /* New protocol response received */
        *status_ret = op_resp.status;

        if (op_resp.status == status_$ok) {
            if (flag < 0) {
                /* Network search: return the network address */
                *net_ret = op_resp.net_value;
            } else {
                /* UID search: return the entry name */
                *name_len_ret = (int16_t)op_resp.name_length;

                /* Truncate if name exceeds buffer */
                if (name_buf_len < *name_len_ret) {
                    *status_ret = status_$naming_leaf_truncated;
                    *name_len_ret = name_buf_len;
                }

                /* Copy name bytes (1-based loop matching original assembly) */
                {
                    int16_t count = *name_len_ret - 1;
                    if (count >= 0) {
                        int16_t i = 1;
                        do {
                            name_buf[i - 1] = (char)op_resp.name_data[i - 1];
                            i++;
                            count--;
                        } while (count != -1);
                    }
                }
            }
        }
    }
}
