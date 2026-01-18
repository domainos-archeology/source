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

/* Early log buffers - these exist at fixed addresses for crash recovery */
extern early_log_t          EARLY_LOG;          /* 0x00e00000 */
extern early_log_extended_t EARLY_LOG_EXTENDED; /* 0x00e0000c */

/* Timestamp constant - referenced but not used in init message */
extern uint32_t DAT_00e2fffc;

/* Status code for name not found */
#define status_$naming_name_not_found 0x000e0007

void LOG_$INIT(void)
{
    status_$t status;
    uid_t logfile_uid;
    int32_t file_size;
    uint8_t out_attrs[8];
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
        if (log_$check_op_status("create  ") < 0) {
            return;
        }
    }

    if (log_$check_op_status("resolve ") < 0) {
        return;
    }

    /* Copy UID and clear a flag bit */
    logfile_uid.high = LOG_$LOGFILE_UID.high;
    logfile_uid.low = LOG_$LOGFILE_UID.low;

    /* Get file attributes to check size */
    AST_$GET_COMMON_ATTRIBUTES(&LOG_$LOGFILE_UID, 2, out_attrs, &status);
    if (log_$check_op_status("get_attributes  ") < 0) {
        return;
    }

    /* Extract file size from attributes - file_size is at beginning */
    file_size = *(int32_t *)out_attrs;
    is_new_file = (file_size == 0) ? (int8_t)-1 : 0;

    /* Map the log file into memory
     * mode=0, flags=0xff00, offset=0, length=0x400, prot=0x16, hint=0
     */
    vpn = (int16_t *)MST_$MAPS(0, (int16_t)0xff00, &LOG_$LOGFILE_UID, 0,
                                LOG_BUFFER_SIZE, 0x16, 0, is_new_file,
                                out_attrs, &status);
    if (log_$check_op_status("map     ") < 0) {
        return;
    }

    /* Lock the file for exclusive access
     * The original code passes fixed addresses for lock parameters
     */
    lock_index = 0;
    lock_mode = 0;
    lock_rights = 0;
    FILE_$LOCK(&LOG_$LOGFILE_UID, &lock_index, &lock_mode, &lock_rights, 0, &status);
    if (log_$check_op_status("lock    ") < 0) {
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
    if (log_$check_op_status("wire    ") < 0) {
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
    LOG_$ADD(LOG_TYPE_INIT, &DAT_00e2fffc, 0);
}
