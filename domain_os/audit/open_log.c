/*
 * open_log.c - audit_$open_log (the SAU2 map spells it AUDIT_$OPEN_LOG)
 *
 * Opens or creates the audit log file, locks it and maps a write buffer.
 *
 * Original address: 0x00E716CC, 370 bytes (0x00E716CC-0x00E7183D); the
 * constant pool that follows it runs 0x00E7183E-0x00E7185D.
 *
 * Frame (link.w A6,-0xcc):
 *   A6-0xCA  word   FILE_$PRIV_LOCK's granted-rights output (0x00E7177A)
 *   A6-0xC0  long   FILE_$PRIV_UNLOCK's dtv output          (0x00E717E0)
 *   A6-0xB8  0x90 B the attribute record                    (0x00E7174C)
 *   A6-0x28  0x20 B the object-location record              (0x00E71750)
 */

#include "audit/audit_internal.h"
#include "name/name.h"
#include "file/file.h" // we use FILE_$PRIV_LOCK/UNLOCK
#include "ast/ast.h"   /* AST_ATTR_REC_SIZE, AST_ATTR_OFF_LENGTH */
#include "mst/mst.h"
#include "uid/uid.h"

/*
 * 0x00E70C20: longword 0.  The compiler emits `pea (-0xb66,PC)` at
 * 0x00E71784 to hand FILE_$PRIV_LOCK the address of this cell as its
 * ACL-context argument.
 */
static void *audit_$open_log_nil_acl_ctx = NULL;

/*
 * The constant cells at 0x00E7183E-0x00E7185D, with their image bytes:
 *
 *   00e7183e  00 1a                    the path length, 26
 *   00e71840  00 04                    FILE_$GET_ATTRIBUTES' request word
 *   00e71842  00 90                    FILE_$GET_ATTRIBUTES' buffer size
 *   00e71844  60 6e 6f 64 65 ...       "`node_data/audit/audit_log"
 *
 * The path starts with a BACKQUOTE, Domain/OS' shorthand for the calling
 * node's own //node_data, not with "//".  26 characters, no terminator.
 */
static const char log_path[] = "`node_data/audit/audit_log";        /* 0x00E71844 */
static const int16_t log_path_len = 0x001A;                         /* 0x00E7183E */

/* Request bit 2 = FILE_GET_ATTR_REQ_NO_PROBE: do not run the delete probe. */
static const int16_t attr_request = 0x0004;                         /* 0x00E71840 */
static const int16_t attr_buf_size = AST_ATTR_REC_SIZE;             /* 0x00E71842 */

/* UNSTRUCT_$UID (file type UID for unstructured files) comes from uid/uid.h */

void audit_$open_log(status_$t *status_ret)
{
    uint8_t attr_buffer[AST_ATTR_REC_SIZE];   /* A6-0xB8, 0x90 bytes */
    file_$obj_loc_t obj_loc;                  /* A6-0x28, 0x20 bytes */
    uint32_t file_size;
    /* A6-0xCA: FILE_$PRIV_LOCK's granted-rights word (0x00E7177A) */
    uint16_t lock_rights;
    /* A6-0xC0: FILE_$PRIV_UNLOCK's dtv output longword (0x00E717E0) */
    uint32_t unlock_dtv;

    /* 0x00E716D6-0x00E716EC: a non-NIL UID means the log is already open. */
    if (AUDIT_$DATA.log_file_uid.high != UID_$NIL.high ||
        AUDIT_$DATA.log_file_uid.low != UID_$NIL.low) {
        /* Already open */
        *status_ret = status_$ok;
        return;
    }

    /* 0x00E716F0-0x00E71704 */
    NAME_$RESOLVE((char *)log_path, (int16_t *)&log_path_len,
                  &AUDIT_$DATA.log_file_uid, status_ret);

    /* 0x00E71708-0x00E71724: `cmpi.l #0xe0007` */
    if (*status_ret == status_$naming_name_not_found) {
        NAME_$CR_FILE((char *)log_path, (int16_t *)&log_path_len,
                      &AUDIT_$DATA.log_file_uid, status_ret);
    }

    if (*status_ret != status_$ok) {
        goto error;
    }

    /* 0x00E7172E-0x00E71740: `move.l #0xe173c4` is UNSTRUCT_$UID by address. */
    FILE_$SET_TYPE(&AUDIT_$DATA.log_file_uid, &UNSTRUCT_$UID, status_ret);
    if (*status_ret != status_$ok) {
        goto error;
    }

    /*
     * 0x00E7174A-0x00E71766.  The two `pea (d,PC)` cells are the request
     * word (0x0004) and the buffer size (0x0090); the routine also wants a
     * 0x20-byte object-location record and a 0x90-byte attribute buffer.
     */
    FILE_$GET_ATTRIBUTES(&AUDIT_$DATA.log_file_uid,
                         (void *)&attr_request,
                         (int16_t *)&attr_buf_size,
                         &obj_loc, attr_buffer, status_ret);
    if (*status_ret != status_$ok) {
        goto error;
    }

    /*
     * 0x00E71770: `move.l (-0xa4,A6),(0x98,A5)`.  A6-0xA4 is A6-0xB8 + 0x14,
     * i.e. the attribute record's length field.
     */
    file_size = *(const uint32_t *)(const void *)(attr_buffer + AST_ATTR_OFF_LENGTH);
    AUDIT_$DATA.file_offset = file_size;

    /* 0x00E71776-0x00E717A4.  `pea (0x1).w` fills asid = 0 / side = 1 and
     * `move.l #0x40000` fills lock_mode = 4 / local_only = 0.
     * 0x00E71784 `pea (-0xb66,PC)` = the NIL longword at 0x00E70C20; the
     * compiler passes its address, not a null pointer. */
    FILE_$PRIV_LOCK(&AUDIT_$DATA.log_file_uid, 0, 1, 4, 0, 0, 0, 0, 0, 0,
                    &audit_$open_log_nil_acl_ctx, 0,
                    (uint32_t *)&AUDIT_$DATA.lock_id, &lock_rights,
                    status_ret);
    if (*status_ret != status_$ok) {
        goto error;
    }

    /* 0x00E717AC-0x00E717D8 */
    AUDIT_$DATA.buffer_base = MST_$MAPS(
        0,                          /* asid (0 = current) */
        (int8_t)-1,                 /* flags */
        &AUDIT_$DATA.log_file_uid,
        AUDIT_$DATA.file_offset,
        AUDIT_BUFFER_MAP_SIZE,
        0x16,                       /* protection */
        0,                          /* unused */
        (int8_t)-1,                 /* writable */
        &AUDIT_$DATA.buffer_size,
        status_ret
    );

    if (*status_ret != status_$ok) {
        /* 0x00E717DE-0x00E71802: mapping failed, drop the lock.
         * `move.l (0x19c,A5)` slot, `move.l #0x40000` = mode word 4 + asid
         * word 0, then three `clr.l` for by_key/key, rem_key, rem_node. */
        (void)FILE_$PRIV_UNLOCK(&AUDIT_$DATA.log_file_uid,
                                (int32_t)AUDIT_$DATA.lock_id, 4, 0,
                                0, 0, 0, 0, &unlock_dtv, status_ret);
        goto error;
    }

    /* 0x00E71804-0x00E71812 */
    AUDIT_$DATA.write_ptr = AUDIT_$DATA.buffer_base;
    AUDIT_$DATA.bytes_remaining = AUDIT_$DATA.buffer_size;
    AUDIT_$DATA.dirty = 0;

    return;

error:
    /* 0x00E71818-0x00E71834 */
    AUDIT_$DATA.log_file_uid.high = UID_$NIL.high;
    AUDIT_$DATA.log_file_uid.low = UID_$NIL.low;
    AUDIT_$DATA.write_ptr = NULL;
    AUDIT_$DATA.file_offset = 0;
    AUDIT_$DATA.bytes_remaining = 0;
    AUDIT_$DATA.dirty = 0;
}
