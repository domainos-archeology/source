/*
 * DIR_$DO_OP - Core directory operation RPC dispatcher
 *
 * Dispatches directory operations to either remote nodes via
 * REM_FILE_$RN_DO_OP or to local handlers via a switch on the
 * operation code.
 *
 * Original address: 0x00E4C02C
 * Original size: 2386 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$DO_OP - Core directory operation RPC dispatcher
 *
 * Based on the Ghidra decompilation at 0x00E4C02C.
 *
 * This function is the central dispatcher for all directory operations.
 * It first attempts to route the request to a remote node via hints.
 * If the directory is local (NODE_$ME), it dispatches to the appropriate
 * local handler based on the operation code in the request byte.
 *
 * The operation code is at request[3] (byte offset 3).
 * The UID is at request[4..11] (offsets 4-11).
 *
 * Parameters:
 *   request   - Request buffer
 *   req_size  - Request size
 *   resp_size - Response size
 *   response  - Response buffer (Dir_$OpResponse)
 *   resp_buf  - Extra parameter / request pointer
 */
void DIR_$DO_OP(void *request, int16_t req_size, int16_t resp_size,
                void *response, void *resp_buf)
{
    uint8_t *req = (uint8_t *)request;
    Dir_$OpResponse *resp = (Dir_$OpResponse *)response;
    uid_t local_uid;
    uint8_t op_code;
    uint8_t op_half;      /* op_code >> 1, used as index */
    status_$t status;
    int8_t is_server_proc;
    int16_t hint_count;
    uint32_t hints[16];   /* Hint buffer: pairs of (node, extra) */
    int16_t hint_idx;
    int16_t retry_count;
    uint8_t result_buf[8];
    int16_t next_idx;

    /* Extract UID from request */
    local_uid.high = *((uint32_t *)(req + 4));
    local_uid.low = *((uint32_t *)(req + 8));

    /* Extract operation code */
    op_code = req[3];
    op_half = op_code >> 1;

    /* Initialize */
    status = 0x000F0001; /* file_$not_found - default status */
    resp->f12 = 0;

    /* Check if this is a server process (type 9) */
    is_server_proc = (((uint16_t *)PROC1_$TYPE)[(int16_t)(PROC1_$CURRENT)] == 9)
                     ? (int8_t)-1 : 0;

    if (is_server_proc < 0) {
        /* Server process - use local node directly */
        hint_count = 1;
        hints[1] = NODE_$ME;
        hints[0] = 0;
    } else {
        /* Normal process - get hints for this UID */
        *((uint16_t *)(req + 0x20)) = 2;
        hint_count = HINT_$GET_HINTS(&local_uid, &hints[0]);
        *((uint16_t *)(req + 0x12)) = *((uint16_t *)(&DAT_00e7fb9c + op_half * 8));
    }

    /* Store in per-process slot */
    /* TODO: Verify per-process slot addressing */

    retry_count = 0;
    hint_idx = 0;

    /* Main dispatch loop - try each hint */
    while (hint_idx < hint_count) {
        next_idx = hint_idx + 1;

        /* Check if this is the local node */
        if (hints[hint_idx * 2 + 1] != NODE_$ME) {
            /* Remote node - send via REM_FILE */
            *((uint16_t *)(req + 0x0c)) = 1;
            *((uint16_t *)(req + 0x10)) = 0;

            REM_FILE_$RN_DO_OP(&hints[hint_idx * 2],
                               request,
                               req_size + 0x8e,
                               resp_size,
                               response,
                               resp_buf);

            if (resp->status == status_$ok) {
                /* Success - validate response version */
                int16_t resp_ver = *((int16_t *)&resp->f18[0]);
                if (resp_ver > 0 ||
                    resp_ver < *((int16_t *)(&DAT_00e7fb9c + op_half * 8))) {
                    resp->status = file_$bad_reply_received_from_remote_node;
                    return;
                }

                /* Update hints if not first try */
                if (next_idx != 1) {
                    HINT_$ADDI(&local_uid, &hints[next_idx * 2 - 1]);
                }

                /* Set the "remote" flag */
                resp->f13 |= 1;

                /* Check redirect flag (f14 in original naming = offset 0x16 from resp base) */
                if (resp->_22_4_ == 0) {
                    return;
                }

                /* Handle redirect */
                if (((local_uid.low & 0xFFFFF) == (resp->f1a & 0xFFFFF)) ||
                    op_code != 0x58) {
                    /* Various early return conditions */
                    return;
                }

                /* Update hint after redirect */
                FUN_00e4bc76(&local_uid, &hints[next_idx * 2 - 1],
                             (int16_t)hints[next_idx * 2],
                             (uint8_t *)resp + 0x16, 0);
                return;
            }

            if (resp->status == status_$naming_ran_out_of_address_space) {
                /* Retry with same hint */
                retry_count++;
                hint_count = next_idx;
                if (retry_count > 0x13) {
                    return;
                }
                continue;
            }

            /* Check if error is retryable */
            if (FUN_00e4bc26((int16_t)resp->status) >= 0) {
                /* Not retryable */
                return;
            }

            /* Retryable - try next hint */
            hint_idx = next_idx;
            if (resp->status == file_$bad_reply_received_from_remote_node) {
                status = file_$bad_reply_received_from_remote_node;
            }
            continue;
        }

        /* Local node - dispatch based on operation code */
        /* Set response header fields */
        *((int16_t *)resp_buf) = *((int16_t *)(&DAT_00e7fba0 + op_half * 8)) + 0x14;
        *((uint16_t *)&resp->f18[2]) =
            *((uint16_t *)(&DAT_00e7fb9c + op_half * 8));
        *((uint16_t *)&resp->f18[0]) = 0;

        switch (op_code) {

        case 0x2A: /* Add entry */
            if (*((uint32_t *)(req + 0x98)) == 0) {
                /* Simple add */
                FUN_00e5044a(&local_uid, req + 0x9c,
                             *((uint16_t *)(req + 0x8e)),
                             (uid_t *)(req + 0x90),
                             0, &resp->status);
            } else {
                /* Root add (with replace) */
                FUN_00e4fef2(&local_uid, 2, req + 0x9c,
                             *((uint16_t *)(req + 0x8e)),
                             3, *((uint32_t *)(req + 0x98)),
                             req + 0x90, 0, (uint32_t)(uintptr_t)FUN_00e4c9e4,
                             result_buf, &resp->status);
            }
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4be16(0x12, resp->status, &local_uid,
                             (uid_t *)(req + 0x90),
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x9c);
            }
            break;

        case 0x2C: /* Add hard link */
            FUN_00e5044a(&local_uid, req + 0x98,
                         *((uint16_t *)(req + 0x8e)),
                         (uid_t *)(req + 0x90),
                         0xFF, &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4be16(0x1F, resp->status, &local_uid,
                             (uid_t *)(req + 0x90),
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x98);
            }
            break;

        case 0x2E: /* Delete file (with flags) */
            FUN_00e5125e(&local_uid, req + 0x92,
                         *((uint16_t *)(req + 0x8e)),
                         -((req[0x91] & 1) != 0),
                         0xFF, 0xFF,
                         result_buf, (uid_t *)&resp->_22_4_,
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4be16(0x20, resp->status, &local_uid,
                             (uid_t *)&resp->_22_4_,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x92);
            }
            break;

        case 0x30: /* Drop hard link */
            FUN_00e5125e(&local_uid, req + 0x90,
                         *((uint16_t *)(req + 0x8e)),
                         0xFF, 0xFF, 0xFF,
                         result_buf, (uid_t *)&resp->_22_4_,
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4be16(0x13, resp->status, &local_uid,
                             (uid_t *)&resp->_22_4_,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x90);
            }
            break;

        case 0x32: /* Change name (rename) */
            {
                uint16_t old_name_len = *((uint16_t *)(req + 0x8e));
                int16_t new_name_offset = old_name_len + DAT_00e7fc66;
                uint8_t *new_name = req + 0x8e + new_name_offset;

                FUN_00e518bc(&local_uid,
                             (uint32_t)*((uint16_t *)(req + 0x0e)) << 16,
                             (int16_t)(uintptr_t)(req + 0x92),
                             ((uint32_t)old_name_len << 16) | (uint16_t)(uintptr_t)new_name,
                             (int16_t)(uintptr_t)new_name,
                             (uint32_t)*((uint16_t *)(req + 0x90)) << 16);
            }
            if ((int8_t)AUDIT_$ENABLED < 0) {
                uint16_t old_name_len2 = *((uint16_t *)(req + 0x8e));
                int16_t new_name_offset2 = old_name_len2 + DAT_00e7fc66;
                FUN_00e4bec2(0x18, resp->status, &local_uid,
                             old_name_len2,
                             *((uint16_t *)(req + 0x90)),
                             req + 0x92,
                             req + 0x8e + new_name_offset2);
            }
            break;

        case 0x34: /* Create directory */
            FUN_00e50832(&local_uid,
                         *((uint16_t *)(req + 0x0e)),
                         req + 0x98,
                         *((uint16_t *)(req + 0x8e)),
                         req + 0x90,
                         (uid_t *)&resp->_22_4_,
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4be16(0x19, resp->status, &local_uid,
                             (uid_t *)(req + 0x90),
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x98);
            }
            break;

        case 0x36: /* Delete file (simple) */
            FUN_00e5125e(&local_uid, req + 0x92,
                         *((uint16_t *)(req + 0x8e)),
                         req[0x90],
                         (uint16_t)req[0x91], 0,
                         result_buf, (uid_t *)&resp->_22_4_,
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4be16(0x13, resp->status, &local_uid,
                             (uid_t *)&resp->_22_4_,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x92);
            }
            break;

        case 0x38: /* Read link */
            FUN_00e52576(&local_uid, req + 0x90,
                         *((uint16_t *)(req + 0x8e)),
                         &resp->_22_4_, &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4be16(0x16, resp->status, &local_uid,
                             (uid_t *)&resp->_22_4_,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x90);
            }
            break;

        case 0x3A: /* Drop link */
            FUN_00e52744(&local_uid, req + 0x90,
                         *((uint16_t *)(req + 0x8e)),
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4be16(0x17, resp->status, &local_uid,
                             &local_uid,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x90);
            }
            break;

        case 0x3C: /* Add link */
            FUN_00e4fef2(&local_uid, 2, req + 0x96,
                         *((uint16_t *)(req + 0x8e)),
                         4, 0, &DAT_00e4b33c,
                         *((uint16_t *)(req + 0x90)),
                         *((uint32_t *)(req + 0x92)),
                         result_buf, &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4bd48(0x1A, resp->status, &local_uid,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x96,
                             *((uint16_t *)(req + 0x90)),
                             *((uint32_t *)(req + 0x92)));
            }
            break;

        case 0x3E: /* Read link (extended) */
            FUN_00e4d5b4(&local_uid, req + 0x96,
                         *((uint16_t *)(req + 0x8e)),
                         *((uint16_t *)(req + 0x90)),
                         *((uint32_t *)(req + 0x92)),
                         &resp->_22_4_, (uid_t *)&resp->f1a,
                         &resp->status);
            break;

        case 0x40: /* Create directory (extended) */
            FUN_00e511da(&local_uid, 2, req + 0x90,
                         *((uint16_t *)(req + 0x8e)),
                         4, (void *)&resp->_22_4_,
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4be16(0x1B, resp->status, &local_uid,
                             (uid_t *)&resp->_22_4_,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x90);
            }
            break;

        case 0x42: /* Directory read */
            {
                uint32_t max_size;
                uint8_t *resp_bytes = (uint8_t *)resp;

                if (is_server_proc < 0) {
                    max_size = 0x400;
                    if (*((uint32_t *)(req + 0x96)) < max_size) {
                        max_size = *((uint32_t *)(req + 0x96));
                    }
                    if (max_size > 0x400) {
                        max_size = 0x400;
                    }
                } else {
                    max_size = *((uint32_t *)(req + 0x96));
                }

                /* Clamp response version */
                if (*((uint16_t *)(req + 0x12)) <
                    *((uint16_t *)&resp->f18[2])) {
                    *((uint16_t *)&resp->f18[2]) =
                        *((uint16_t *)(req + 0x12));
                }

                /* Copy continuation */
                resp->_22_4_ = *((uint32_t *)(req + 0x8e));

                FUN_00e4d954(&local_uid,
                             *((int16_t *)&resp->f18[2]),
                             req + 0xa0,
                             *((uint16_t *)(req + 0x9e)),
                             &resp->_22_4_,
                             *((uint32_t *)(req + 0x92)),
                             max_size,
                             *((uint32_t *)(req + 0x9a)),
                             resp_bytes + 0x18,
                             resp_bytes + 0x1c,
                             resp_bytes + 0x20,
                             &resp->status);
            }
            break;

        case 0x44: /* Get entry */
            FUN_00e4cffa(&local_uid, req + 0x90,
                         *((uint16_t *)(req + 0x8e)),
                         &resp->_22_4_, &resp->f1a,
                         (uint8_t *)resp + 0x1e,
                         &resp->status);
            break;

        case 0x46: /* Get next entry */
            FUN_00e4e41a(&local_uid, req + 0x8e,
                         req[0x96],
                         &resp->_22_4_, &resp->f1a,
                         (uint8_t *)resp + 0x1a,
                         &resp->status);
            break;

        case 0x48: /* Fix directory */
            FUN_00e53a18(&local_uid, &resp->status);
            break;

        case 0x4A: /* Set ACL */
            DIR_$SET_ACL(&local_uid, req + 0x8e, &resp->status);
            break;

        case 0x4C: /* Set default ACL */
            FUN_00e52fa6(&local_uid, req + 0x96,
                         req + 0x8e, &resp->status);
            break;

        case 0x4E: /* Get default ACL */
            FUN_00e53128(&local_uid, (uid_t *)(req + 0x8e),
                         (uid_t *)&resp->_22_4_, &resp->status);
            break;

        case 0x50: /* Validate name */
            FUN_00e501d2(req + 0x90,
                         *((uint16_t *)(req + 0x8e)),
                         &resp->status);
            break;

        case 0x52: /* Set protection */
            FUN_00e5216a(&local_uid, req + 0xba,
                         *((int16_t *)(req + 0xc2)));
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4af28(resp->status, &local_uid,
                             req + 0x8e, (uid_t *)(req + 0xba),
                             req + 0xc2, 4);
            }
            break;

        case 0x54: /* Set protection (extended) */
            FUN_00e52044(&local_uid, req + 0x96,
                         req + 0x8e, req + 0xc2,
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4af28(resp->status, &local_uid,
                             req + 0x96, (uid_t *)(req + 0x8e),
                             req + 0xc2, 4);
            }
            break;

        case 0x56: /* Get protection */
            FUN_00e51cf6(&local_uid, req + 0x8e,
                         &resp->_22_4_, (uint8_t *)resp + 0x40,
                         &resp->status);
            break;

        case 0x58: /* Resolve path */
            {
                uint8_t *resp_bytes = (uint8_t *)resp;
                /* Copy 24 bytes from req+0x94 to resp+0x16 */
                int16_t j;
                for (j = 0; j < 24; j++) {
                    resp_bytes[0x16 + j] = req[0x94 + j];
                }

                FUN_00e4d0e2(*((uint32_t *)(req + 0x8e)),
                             *((uint16_t *)(req + 0x92)),
                             resp_bytes + 0x16,
                             &resp->_22_4_,
                             resp_bytes + 0x15,
                             resp_bytes + 0x26,
                             resp_bytes + 0x28,
                             resp_bytes + 0x2a,
                             resp_bytes + 0x2c,
                             resp_bytes + 0x2e,
                             resp_bytes + 0x30,
                             *((uint32_t *)&resp->f18[2]),
                             resp_bytes + 0x1e,
                             &resp->status);

                /* Audit if enabled */
                if ((int8_t)AUDIT_$ENABLED < 0) {
                    uint8_t flags_byte = resp_bytes[0x14];
                    uint8_t loop_byte = resp_bytes[0x15];
                    if ((~loop_byte & flags_byte) != 0 &&
                        resp->status == status_$ok &&
                        *((uint16_t *)(resp_bytes + 0x2e)) == 0) {
                        FUN_00e4bf92(*((uint32_t *)(req + 0x8e)),
                                    *((uint16_t *)(req + 0x92)),
                                    resp_bytes + 0x16,
                                    resp->status);
                    }
                }
            }
            break;

        case 0x5A: /* Mount */
            FUN_00e5325e(&local_uid, req + 0x8e,
                         *((uint32_t *)(req + 0x96)),
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4bce0(0x1C, resp->status, &local_uid,
                             req + 0x8e,
                             *((uint32_t *)(req + 0x96)));
            }
            break;

        case 0x5C: /* Drop mount */
            FUN_00e533e6(req + 0x8e,
                         *((uint32_t *)(req + 0x96)),
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                FUN_00e4bce0(0x1D, resp->status, &local_uid,
                             req + 0x8e,
                             *((uint32_t *)(req + 0x96)));
            }
            break;

        default:
            CRASH_SYSTEM((const status_$t *)&Naming_bad_request_header_ver_err);
            break;
        }

        /* Post-operation: check status and handle retries */
        if (resp->status == status_$ok) {
            /* Success - update hints if not first hint */
            if (next_idx != 1) {
                HINT_$ADDI(&local_uid, &hints[next_idx * 2 - 1]);
            }
            return;
        }

        /* Check if this is a retryable "stale" error (0x000E0033) */
        if (is_server_proc >= 0 && resp->status == 0x000E0033) {
            if (hint_idx < hint_count) {
                resp->status = status;
                continue;
            }
        }

        /* Done */
        resp->status = status;
        return;
    }

    /* Exhausted all hints */
    resp->status = status;
}
