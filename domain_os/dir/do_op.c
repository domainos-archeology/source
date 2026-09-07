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
        /* 0xE4C0BA: move.w (0x1f9c,A0),(0x12,A2) */
        *((uint16_t *)(req + 0x12)) = DIR_$OP_VERSION(op_half);
    }

    /* Store in per-process slot */
    /* TODO(source-qgq): Verify per-process slot addressing */

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
                uint32_t hint_extra;

                /*
                 * 0xE4C174-0xE4C196: the reply is rejected when its version
                 * word at +0x08 is greater than zero, or when the accepted
                 * version at +0x0A is GREATER than this operation's entry in
                 * the table at A5+0x1F9C (`cmp.w (0x1f9c,A0),D2w; ble` keeps
                 * the reply).
                 */
                if ((*((int16_t *)&resp->f18[0]) > 0) ||
                    (*((int16_t *)&resp->f18[2]) >
                     (int16_t)DIR_$OP_VERSION(op_half))) {
                    resp->status = file_$bad_reply_received_from_remote_node;
                    return;
                }

                /* 0xE4C19A: `cmpi.w #0x1,D3w; beq` - the first hint is
                 * already in the cache.  0xE4C1A6 passes &hints[k]. */
                if (next_idx != 1) {
                    HINT_$ADDI(&local_uid, &hints[hint_idx * 2]);
                }

                /* 0xE4C1B6: bset.b #0,(0x13,A3) - the flag byte is response
                 * offset 0x13, i.e. the last byte of f18, not f13. */
                resp->f18[DIR_RESP_REMOTE_FLAG_BYTE] |= DIR_RESP_REMOTE_FLAG;

                /* 0xE4C1BE: move.b (0x16,A3),D0b - only the TOP byte of the
                 * returned UID's high half is tested (the usual UID-nil test). */
                if ((resp->_22_4_ >> 24) == 0) {
                    return;
                }

                /*
                 * 0xE4C1C8-0xE4C1E6: a RESOLVE whose reply names a different
                 * node carries its redirect longword at reply+0x30.
                 */
                if (((local_uid.low & 0xFFFFF) != (resp->f1a & 0xFFFFF)) &&
                    (op_code == 0x58)) {
                    hint_extra = resp->resolve.redirect;    /* (0x30,A3) */
                } else {
                    /*
                     * 0xE4C1E8-0xE4C222: otherwise only a GET_ENTRYU (0x44)
                     * whose reply length word is 1 redirects, and its
                     * longword lives at reply+0x1E.  The `tst.w D0w` at
                     * 0xE4C1E8 re-tests the reply+0x16 byte that 0xE4C1C4
                     * already proved non-zero, so it can never branch.
                     */
                    if (((local_uid.high >> 24) & 0xFF) == 0) {
                        return;                             /* 0xE4C1F6 */
                    }
                    if ((local_uid.low & 0xFFFFF) == (resp->f1a & 0xFFFFF)) {
                        return;                             /* 0xE4C20C */
                    }
                    if (op_code != 0x44) {
                        return;                             /* 0xE4C214 */
                    }
                    if (resp->_20_2_ != 1) {
                        return;                             /* 0xE4C21E */
                    }
                    hint_extra = resp->_24_4_;              /* (0x1e,A3) */
                }

                /* 0xE4C226-0xE4C23C: both hint longwords go by value. */
                DIR_$UPDATE_HINT(&local_uid,
                                 hints[hint_idx * 2],       /* loc_info */
                                 hints[hint_idx * 2 + 1],   /* node */
                                 (uid_t *)((uint8_t *)resp + 0x16),
                                 hint_extra);
                return;
            }

            if (resp->status == status_$naming_directory_locked) {
                /* Retry with same hint */
                retry_count++;
                hint_count = next_idx;
                if (retry_count > 0x13) {
                    return;
                }
                continue;
            }

            /* 0xE4C150 `move.l D1,-(SP)`: the whole status longword goes on
             * the stack; the callee compares it against five longwords and
             * also tests its second byte (0x00E4BC66). */
            if (DIR_$IS_RETRYABLE_STATUS(resp->status) >= 0) {
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
        /* 0xE4C24E: moveq #0x14,D1; add.w (0x1fa0,A0),D1w; move.w D1w,(A1) */
        *((int16_t *)resp_buf) = (int16_t)(DIR_$OP_REPLY_SIZE(op_half) + 0x14);
        /* 0xE4C25A: move.w (0x1f9c,A0),(0xa,A3) */
        *((uint16_t *)&resp->f18[2]) = DIR_$OP_VERSION(op_half);
        *((uint16_t *)&resp->f18[0]) = 0;

        switch (op_code) {

        case 0x2A: /* Add entry */
            if (*((uint32_t *)(req + 0x98)) == 0) {
                /* Simple add */
                dir_$do_op_add_link(&local_uid, req + 0x9c,
                             *((uint16_t *)(req + 0x8e)),
                             (uid_t *)(req + 0x90),
                             0, &resp->status);
            } else {
                /* Root add (with replace) */
                dir_$do_op_add_entry(&local_uid, 2, req + 0x9c,
                             *((uint16_t *)(req + 0x8e)),
                             3, *((uint32_t *)(req + 0x98)),
                             req + 0x90, 0, (uint32_t)(uintptr_t)dir_$find_entry,
                             result_buf, &resp->status);
            }
            if ((int8_t)AUDIT_$ENABLED < 0) {
                AUDIT_$LOG_DIR_OP(0x12, resp->status, &local_uid,
                             (uid_t *)(req + 0x90),
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x9c);
            }
            break;

        case 0x2C: /* Add hard link */
            dir_$do_op_add_link(&local_uid, req + 0x98,
                         *((uint16_t *)(req + 0x8e)),
                         (uid_t *)(req + 0x90),
                         0xFF, &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                AUDIT_$LOG_DIR_OP(0x1F, resp->status, &local_uid,
                             (uid_t *)(req + 0x90),
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x98);
            }
            break;

        case 0x2E: /* Delete file (with flags) */
            dir_$do_op_delete(&local_uid, req + 0x92,
                         *((uint16_t *)(req + 0x8e)),
                         -((req[0x91] & 1) != 0),
                         0xFF, 0xFF,
                         result_buf, &resp->uid,     /* pea (0x14,A3) */
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                AUDIT_$LOG_DIR_OP(0x20, resp->status, &local_uid,
                             &resp->uid,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x92);
            }
            break;

        case 0x30: /* Drop hard link */
            dir_$do_op_delete(&local_uid, req + 0x90,
                         *((uint16_t *)(req + 0x8e)),
                         0xFF, 0xFF, 0xFF,
                         result_buf, &resp->uid,     /* pea (0x14,A3) */
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                AUDIT_$LOG_DIR_OP(0x13, resp->status, &local_uid,
                             &resp->uid,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x90);
            }
            break;

        case 0x32: /* Change name (rename) */
            {
                /*
                 * 0xE4C444-0xE4C46E, pushed right to left:
                 *   pea (0x4,A3)          status_ret
                 *   move.w (0x90,A2)      new_name_len
                 *   pea (0x8e,A4)         new_name, A4 = req + DAT_00e7fc66
                 *                         + old_name_len (0xE4C44E)
                 *   move.w (0x8e,A2)      old_name_len
                 *   pea (0x92,A2)         old_name
                 *   move.w (0xe,A2)       request version word
                 *   pea (-0x10,A6)        &local_uid
                 */
                uint16_t old_name_len = *((uint16_t *)(req + 0x8e));
                int16_t new_name_offset =
                    (int16_t)(DAT_00e7fc66 + old_name_len);
                uint8_t *new_name = req + new_name_offset + 0x8e;

                dir_$do_op_cname(&local_uid,
                             *((uint16_t *)(req + 0x0e)),
                             req + 0x92,
                             old_name_len,
                             new_name,
                             *((uint16_t *)(req + 0x90)),
                             &resp->status);
            }
            if ((int8_t)AUDIT_$ENABLED < 0) {
                uint16_t old_name_len2 = *((uint16_t *)(req + 0x8e));
                int16_t new_name_offset2 = old_name_len2 + DAT_00e7fc66;
                AUDIT_$LOG_CNAME_OP(0x18, resp->status, &local_uid,
                             old_name_len2,
                             *((uint16_t *)(req + 0x90)),
                             req + 0x92,
                             req + 0x8e + new_name_offset2);
            }
            break;

        case 0x34: /* Create directory */
            dir_$do_op_add_bak(&local_uid,
                         *((uint16_t *)(req + 0x0e)),
                         req + 0x98,
                         *((uint16_t *)(req + 0x8e)),
                         req + 0x90,
                         &resp->uid,                 /* pea (0x14,A3) */
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                AUDIT_$LOG_DIR_OP(0x19, resp->status, &local_uid,
                             (uid_t *)(req + 0x90),
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x98);
            }
            break;

        case 0x36: /* Delete file (simple) */
            dir_$do_op_delete(&local_uid, req + 0x92,
                         *((uint16_t *)(req + 0x8e)),
                         req[0x90],
                         (uint16_t)req[0x91], 0,
                         result_buf, &resp->uid,     /* pea (0x14,A3) */
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                AUDIT_$LOG_DIR_OP(0x13, resp->status, &local_uid,
                             &resp->uid,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x92);
            }
            break;

        case 0x38: /* Read link */
            dir_$do_op_create_dir(&local_uid, req + 0x90,
                         *((uint16_t *)(req + 0x8e)),
                         &resp->uid.high, &resp->status);   /* pea (0x14,A3) */
            if ((int8_t)AUDIT_$ENABLED < 0) {
                AUDIT_$LOG_DIR_OP(0x16, resp->status, &local_uid,
                             &resp->uid,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x90);
            }
            break;

        case 0x3A: /* Drop link */
            dir_$do_op_drop_dir(&local_uid, req + 0x90,
                         *((uint16_t *)(req + 0x8e)),
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                AUDIT_$LOG_DIR_OP(0x17, resp->status, &local_uid,
                             &local_uid,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x90);
            }
            break;

        case 0x3C: /* Add link */
            dir_$do_op_add_entry(&local_uid, 2, req + 0x96,
                         *((uint16_t *)(req + 0x8e)),
                         4, 0, &DAT_00e4b33c,
                         *((uint16_t *)(req + 0x90)),
                         *((uint32_t *)(req + 0x92)),
                         result_buf, &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                /* The longword at req+0x92 is pushed as-is (move.l (0x92,A2),-(SP))
                 * and used by AUDIT_$LOG_LINK_OP as the target-data pointer. */
                AUDIT_$LOG_LINK_OP(0x1A, resp->status, &local_uid,
                             *((uint16_t *)(req + 0x8e)),
                             req + 0x96,
                             *((uint16_t *)(req + 0x90)),
                             (void *)(uintptr_t)*((uint32_t *)(req + 0x92)));
            }
            break;

        case 0x3E: /* Read link (extended) */
            dir_$do_op_read_linku(&local_uid, req + 0x96,
                         *((uint16_t *)(req + 0x8e)),
                         *((uint16_t *)(req + 0x90)),
                         *((uint32_t *)(req + 0x92)),
                         &resp->_20_2_,               /* pea (0x14,A3): length */
                         (uid_t *)&resp->_22_4_,      /* pea (0x16,A3): UID */
                         &resp->status);
            break;

        case 0x40: /* Create directory (extended) */
            dir_$do_op_drop_entry(&local_uid, 2, req + 0x90,
                         *((uint16_t *)(req + 0x8e)),
                         4, (void *)&resp->uid,      /* pea (0x14,A3) */
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                AUDIT_$LOG_DIR_OP(0x1B, resp->status, &local_uid,
                             &resp->uid,
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

                /* 0xE4C6CA: move.l (0x8e,A2),(0x14,A3) */
                resp->cookie = *((uint32_t *)(req + 0x8e));

                dir_$do_op_dir_readu(&local_uid,
                             *((int16_t *)&resp->f18[2]),
                             req + 0xa0,
                             *((uint16_t *)(req + 0x9e)),
                             &resp->cookie,          /* pea (0x14,A3) */
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
            dir_$do_op_get_entryu(&local_uid, req + 0x90,
                         *((uint16_t *)(req + 0x8e)),
                         &resp->_20_2_,               /* pea (0x14,A3) */
                         &resp->resolve.start_uid,    /* pea (0x16,A3) */
                         &resp->_24_4_,               /* pea (0x1e,A3) */
                         &resp->status);
            break;

        case 0x46: /* Find UID (opcode 'F')
                    * Handler params: (uid, target_uid, flag, name_ret, len_ret, uid_ret, status)
                    * Response layout (from assembly, 0xE4C6D4-0xE4C6EA):
                    * name_len@0x14, net_val@0x16, name@0x1A */
            dir_$do_op_find_uid(&local_uid, (uid_t *)(req + 0x8e),
                         req[0x96],
                         (uint8_t *)resp + 0x1a,   /* name_ret */
                         (uint8_t *)resp + 0x14,   /* len_ret */
                         (uint8_t *)resp + 0x16,   /* uid_ret/net_ret */
                         &resp->status);
            break;

        case 0x48: /* Fix directory */
            dir_$do_op_fix_dir(&local_uid, &resp->status);
            break;

        case 0x4A: /* Set ACL */
            DIR_$SET_ACL(&local_uid, req + 0x8e, &resp->status);
            break;

        case 0x4C: /* Set default ACL */
            dir_$do_op_set_default_acl(&local_uid, req + 0x96,
                         req + 0x8e, &resp->status);
            break;

        case 0x4E: /* Get default ACL */
            dir_$do_op_get_default_acl(&local_uid, (uid_t *)(req + 0x8e),
                         &resp->uid, &resp->status);    /* pea (0x14,A3) */
            break;

        case 0x50: /* Validate name */
            dir_$do_op_validate_root_entry(req + 0x90,
                         *((uint16_t *)(req + 0x8e)),
                         &resp->status);
            break;

        case 0x52: /* Set protection */
            dir_$do_op_set_prot(&local_uid, req + 0x8e,
                         req + 0xba, *(int16_t *)(req + 0xc2),
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                audit_$log_prot_op(resp->status, &local_uid,
                             req + 0x8e, (uid_t *)(req + 0xba),
                             (uid_t *)(req + 0xc2), 4);  /* pea (0xc2,A2) */
            }
            break;

        case 0x54: /* Set protection (extended) */
            dir_$do_op_set_def_prot(&local_uid, req + 0x96,
                         req + 0x8e, req + 0xc2,
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                audit_$log_prot_op(resp->status, &local_uid,
                             req + 0x96, (uid_t *)(req + 0x8e),
                             (uid_t *)(req + 0xc2), 4);  /* pea (0xc2,A2) */
            }
            break;

        case 0x56: /* Get protection */
            dir_$do_op_get_def_prot(&local_uid, req + 0x8e,
                         &resp->cookie,                 /* pea (0x14,A3) */
                         (uint8_t *)resp + 0x40,        /* pea (0x40,A3) */
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

                /*
                 * 0xE4C894-0xE4C8CE, pushed right to left, and re-derived
                 * from dir_$do_op_resolve's own frame at 0x00E4D0E2
                 * (0x08 long path, 0x0C word len, 0x0E .. 0x3A):
                 *   move.l (0x8e,A2)   path
                 *   move.w (0x92,A2)   path_len
                 *   pea (0x16,A3)      result       (resolve.start_uid)
                 *   pea (0x30,A3)      extra_ret    (clr.l at 0x00E4D118)
                 *   pea (0x1e,A3)      parent_uid   (resolve.resolved_uid)
                 *   pea (0x14,A3)      flags1       (st at 0x00E4D0FE)
                 *   pea (0x15,A3)      flags2       (clr.b at 0x00E4D104)
                 *   pea (0x26,A3)      cont
                 *   pea (0x28,A3)      size
                 *   pea (0x2a,A3)      last_start
                 *   pea (0x2c,A3)      last_size
                 *   move.l (0xac,A2)   max
                 *   pea (0x2e,A3)      link_count   (clr.w at 0x00E4D10A)
                 *   pea (0x4,A3)       status_ret
                 */
                dir_$do_op_resolve(*((uint32_t *)(req + 0x8e)),
                             *((uint16_t *)(req + 0x92)),
                             resp_bytes + 0x16,
                             (uint32_t *)(resp_bytes + 0x30),
                             (uint32_t *)(resp_bytes + 0x1e),
                             resp_bytes + 0x14,
                             resp_bytes + 0x15,
                             (uint16_t *)(void *)(resp_bytes + 0x26),
                             (uint16_t *)(void *)(resp_bytes + 0x28),
                             (uint16_t *)(void *)(resp_bytes + 0x2a),
                             (uint16_t *)(void *)(resp_bytes + 0x2c),
                             *((uint32_t *)(req + 0xac)),
                             (uint16_t *)(void *)(resp_bytes + 0x2e),
                             &resp->status);

                /* Audit if enabled */
                if ((int8_t)AUDIT_$ENABLED < 0) {
                    /* 0xE4C8E0-0xE4C8F2: `move.b (0x15,A3); not.b; and.b
                     * (0x14,A3); seq on status; and.b; bpl` - three Domain
                     * booleans ANDed and tested for bit 7, not for != 0. */
                    int8_t flags_byte = (int8_t)resp_bytes[0x14];
                    int8_t loop_byte = (int8_t)resp_bytes[0x15];
                    if ((int8_t)(~loop_byte & flags_byte
                                 & (resp->status == status_$ok ? -1 : 0)) < 0 &&
                        *((uint16_t *)(void *)(resp_bytes + 0x2e)) == 0) {
                        audit_$log_resolve_op(*((uint32_t *)(req + 0x8e)),
                                    *((uint16_t *)(req + 0x92)),
                                    resp_bytes + 0x16,
                                    resp->status);
                    }
                }
            }
            break;

        case 0x5A: /* Mount */
            dir_$do_op_add_mount(&local_uid, (uid_t *)(req + 0x8e),
                         *((uint32_t *)(req + 0x96)),
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                audit_$log_mount_op(0x1C, resp->status, &local_uid,
                             (uid_t *)(req + 0x8e),  /* pea (0x8e,A2) */
                             *((uint32_t *)(req + 0x96)));
            }
            break;

        case 0x5C: /* Drop mount */
            dir_$do_op_drop_mount((uid_t *)(req + 0x8e),
                         *((uint32_t *)(req + 0x96)),
                         &resp->status);
            if ((int8_t)AUDIT_$ENABLED < 0) {
                audit_$log_mount_op(0x1D, resp->status, &local_uid,
                             (uid_t *)(req + 0x8e),  /* pea (0x8e,A2) */
                             *((uint32_t *)(req + 0x96)));
            }
            break;

        default:
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
            break;
        }

        /* 0xE4C99C: post-operation status handling. */
        if (resp->status == status_$ok) {
            /* 0xE4C9A2: the first hint is already cached. */
            if (next_idx != 1) {
                HINT_$ADDI(&local_uid, &hints[hint_idx * 2]);
            }
            return;
        }

        /* 0xE4C9BE `tst.b D4b; bmi`: a server process never falls back. */
        if (is_server_proc < 0) {
            return;
        }
        /* 0xE4C9C2: only "directory object not found" tries the next hint. */
        if (resp->status != status_$naming_directory_object_not_found) {
            return;
        }

        /* 0xE4C9CC: advance and re-test; when the hints run out the loop
         * exit at 0xE4C9D4 reports the saved status. */
        hint_idx = next_idx;
    }

    /* Exhausted all hints */
    resp->status = status;
}
