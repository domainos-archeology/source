/*
 * LOG_$INIT - Initialize the logging subsystem
 *
 * Resolves or creates the log file at //node_data/system_logs/sys_error,
 * maps it to memory, locks it, and wires the memory for reliable access.
 * Also processes any early log entries that were queued before initialization.
 *
 * Original address: 00e30048
 * Original size: 482 bytes
 *
 * Assembly (key parts):
 *   00e30048    link.w A6,-0x58
 *   00e30062    jsr NAME_$RESOLVE
 *   00e30088    jsr NAME_$CR_FILE
 *   00e300d8    jsr AST_$GET_COMMON_ATTRIBUTES
 *   00e30118    jsr MST_$MAPS
 *   00e3014e    jsr FILE_$LOCK
 *   00e30190    jsr MST_$WIRE
 *   00e301d4    jsr LOG_$ADD (early entry)
 *   00e3020a    jsr LOG_$ADD (crash entry)
 *   00e3021a    jsr LOG_$ADD (init entry)
 */

#include "log/log_internal.h"
#include "name/name.h"
#include "ast/ast.h"
#include "mst/mst.h"
#include "file/file.h"

/* Status code for name not found */
#define status_$naming_name_not_found 0x000e0007

/* `move.w #0x2,-(SP)` at 0x00E300D0 - the AST_$GET_COMMON_ATTRIBUTES
 * selector LOG_$INIT uses. */
#define LOG_CATTR_SELECTOR      0x0002

void LOG_$INIT(void)
{
    status_$t status;
    /* A6-0x20: the object-location descriptor AST_$GET_ATTRIBUTES reads the
     * UID out of at +0x08 and overwrites in full on success. */
    file_$obj_loc_t desc;
    int32_t file_size;
    ast_$common_attr_t cattr;   /* A6-0x38, 0x18 bytes */
    uint32_t map_out;           /* A6-0x40, MST_$MAPS' output longword */
    uint8_t lock_out[4];
    int16_t *vpn;
    int8_t is_new_file;
    uint16_t lock_index;
    uint16_t lock_mode;
    uint8_t lock_rights;

    /* Try to resolve the log file path */
    NAME_$RESOLVE((char *)LOG_FILE_PATH, &LOG_FILE_PATH_LEN, &LOG_$LOGFILE_UID, &status);

    if (status == status_$naming_name_not_found) {
        /* File doesn't exist, create it */
        NAME_$CR_FILE((char *)LOG_FILE_PATH, &LOG_FILE_PATH_LEN, &LOG_$LOGFILE_UID, &status);
        if (log_$check_op_status("create%$", &status) < 0) {
            return;
        }
    }

    if (log_$check_op_status("resolve%$", &status) < 0) {
        return;
    }

    /* 0x00E300B8: the UID goes to descriptor+0x08, where
     * AST_$GET_ATTRIBUTES reads it - not at the head of the record. */
    desc.uid = LOG_$LOGFILE_UID;
    /* 0x00E300C0 `bclr.b #0x6,(-0x3,A6)` = descriptor+0x1D. */
    desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    /* Get file attributes to check size (0x00E300D8) */
    AST_$GET_COMMON_ATTRIBUTES((uid_t *)&desc, LOG_CATTR_SELECTOR, &cattr,
                               &status);
    if (log_$check_op_status("get_attributes%$", &status) < 0) {
        return;
    }

    /* 0x00E300F2 `tst.l (-0x34,A6)` with the record at A6-0x38: the object's
     * length, at +0x04.  `seq` makes is_new_file 0xFF when it is zero. */
    file_size = (int32_t)cattr.length;
    is_new_file = (file_size == 0) ? (int8_t)-1 : 0;

    /* Map the log file into memory
     * mode=0, flags=0xff00, offset=0, length=0x400, prot=0x16, hint=0
     */
    vpn = (int16_t *)MST_$MAPS(0, (int16_t)0xff00, &LOG_$LOGFILE_UID, 0,
                                LOG_BUFFER_SIZE, 0x16, 0, is_new_file,
                                &map_out, &status);   /* 0x00E300FC */
    if (log_$check_op_status("map%$", &status) < 0) {
        return;
    }

    /* Lock the file for exclusive access
     * The original code passes fixed addresses for lock parameters
     */
    lock_index = 0;
    lock_mode = 0;
    lock_rights = 0;
    FILE_$LOCK(&LOG_$LOGFILE_UID, &lock_index, &lock_mode, &lock_rights, 0, &status);
    if (log_$check_op_status("lock%$", &status) < 0) {
        return;
    }

    /* Initialize buffer header if new file or empty */
    if (is_new_file < 0 || (vpn[0] == 0 && vpn[1] == 0)) {
        vpn[0] = 0;         /* head = 0 */
        vpn[1] = 1;         /* tail = 1 (first entry slot) */
        LOG_$STATE.dirty_flag = (int8_t)-1;  /* Mark as modified */
    }

    /* Wire the log buffer page for reliable access */
    LOG_$STATE.wired_handle = MST_$WIRE((uint32_t)vpn, &status);
    if (log_$check_op_status("wire%$", &status) < 0) {
        return;
    }

    /* Store pointer to mapped buffer */
    LOG_$LOGFILE_PTR = vpn;

    /* Process any early log entries from before init */

    /* Check for extended early log entry at 0x00e0000c */
    if (EARLY_LOG_EXTENDED.magic == LOG_PENDING_MAGIC) {
        LOG_$ADD(EARLY_LOG_EXTENDED.type, EARLY_LOG_EXTENDED.data,
                 EARLY_LOG_EXTENDED.data_len);
        /* Copy timestamp to current entry */
        ((log_entry_header_t *)LOG_$STATE.current_entry_ptr)->timestamp =
            EARLY_LOG_EXTENDED.timestamp;
        EARLY_LOG_EXTENDED.magic = 0;  /* Clear the pending flag */
    }

    /* Check for crash log entry at 0x00e00000 */
    if (EARLY_LOG.magic == LOG_PENDING_MAGIC) {
        EARLY_LOG.magic = 0;  /* Clear first to avoid re-processing */
        LOG_$ADD(LOG_TYPE_CRASH, EARLY_LOG.data, 8);
    }

    /* Add initialization log entry */
    LOG_$ADD(LOG_TYPE_INIT, &LOG_$VFMT_NO_ARG, 0);
}
