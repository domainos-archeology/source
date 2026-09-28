/*
 * log/init.c - LOG_$INIT (0x00E30048, 482 bytes)
 *
 * Resolves (or creates) `node_data/system_logs/sys_error_log, maps its
 * first page, locks the file, initialises an empty page, wires it, and then
 * replays the two records that survive a reboot (LOG_$LAST_ENTRY and
 * CRASH_$RECORD) before logging the init entry.  Every step is followed by
 * log_$check_op_status, which prints a warning and abandons initialisation
 * (LOG_$LOGFILE_PTR stays NULL, so LOG_$ADD is a no-op) when the step's
 * status has a non-zero module half.
 *
 * Frame (link.w A6,-0x58; A2 D3 D2 saved):
 *   A6-0x48  4  lock_out    FILE_$LOCK's output
 *   A6-0x44  4  status      shared with the nested procedure
 *   A6-0x40  4  map_out     MST_$MAPS' output longword
 *   A6-0x38 24  cattr       AST_$GET_COMMON_ATTRIBUTES' record
 *   A6-0x20 32  desc        the object-location record (UID at +0x08)
 *   D2          is_empty    Domain boolean: the file's length is zero
 *   D3          page        MST_$MAPS' result (A0)
 *   A2          the low-memory records
 */

#include "log/log_internal.h"
#include "name/name.h"
#include "ast/ast.h"
#include "mst/mst.h"
#include "file/file.h"

/* 0x00E3022C "wire%$", 0x00E30232 "lock%$", 0x00E3023E "map%$" NUL,
 * 0x00E30244 "resolve%$" NUL, 0x00E30250 "create%$", 0x00E30258
 * "get_attributes%$" - the operation names, in address order. */
static const char log_$op_wire[] = "wire%$";
static const char log_$op_lock[] = "lock%$";
static const char log_$op_map[] = "map%$";
static const char log_$op_resolve[] = "resolve%$";
static const char log_$op_create[] = "create%$";
static const char log_$op_get_attributes[] = "get_attributes%$";

/* `move.w #0x2,-(SP)` at 0x00E300D0 - the attribute selector. */
#define LOG_CATTR_SELECTOR      0x0002

/* MST_$MAPS' arguments at 0x00E30102-0x00E30116 */
#define LOG_MAP_AREA_ID         0x16
#define LOG_MAP_LENGTH          0x400

void LOG_$INIT(void)
{
    uint32_t lock_out;                  /* A6-0x48 */
    status_$t status;                   /* A6-0x44 */
    uint32_t map_out;                   /* A6-0x40 */
    ast_$common_attr_t cattr;           /* A6-0x38 */
    file_$obj_loc_t desc;               /* A6-0x20 */
    int8_t is_empty;                    /* D2 */
    int16_t *page;                      /* D3 */

    /* 0x00E30050-0x00E30068: the path cell at 0x00E30020 and its WORD
     * length at 0x00E3022A (pea (-0x40,PC) / pea (0x1ce,PC)). */
    NAME_$RESOLVE(log_$logfile_path, &log_$logfile_path_len,
                  &LOG_$LOGFILE_UID, &status);

    /* 0x00E3006C-0x00E3009E: not found -> create it, then check "create". */
    if (status == status_$naming_name_not_found) {
        NAME_$CR_FILE(log_$logfile_path, &log_$logfile_path_len,
                      &LOG_$LOGFILE_UID, &status);
        if (log_$check_op_status(log_$op_create, &status) < 0) {
            return;
        }
    }

    /* 0x00E300A2-0x00E300AE */
    if (log_$check_op_status(log_$op_resolve, &status) < 0) {
        return;
    }

    /* 0x00E300B2-0x00E300C0: the UID goes to desc+0x08, and bit 6 of the
     * flags byte at desc+0x1D is cleared; the rest of the record is left
     * uninitialised. */
    desc.uid = LOG_$LOGFILE_UID;
    desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    /* 0x00E300C6-0x00E300EE: a word result slot is reserved and discarded
     * (`subq.l #0x2,SP` ... `lea (0x10,SP),SP`). */
    AST_$GET_COMMON_ATTRIBUTES(&desc, LOG_CATTR_SELECTOR, &cattr, &status);
    if (log_$check_op_status(log_$op_get_attributes, &status) < 0) {
        return;
    }

    /* 0x00E300F2-0x00E300F6: `tst.l (-0x34,A6)` = cattr.length; `seq`. */
    is_empty = (cattr.length == 0) ? -1 : 0;

    /*
     * 0x00E300F8-0x00E30122: MST_$MAPS(asid 0, direction TRUE (`st -(SP)`),
     * &uid, start 0, length 0x400, area 0x16, size 0, rights = is_empty
     * (`move.b D2b,-(SP)`), &map_out, &status); the page address comes back
     * in A0.
     */
    page = (int16_t *)MST_$MAPS(0, true, &LOG_$LOGFILE_UID, 0, LOG_MAP_LENGTH,
                                LOG_MAP_AREA_ID, 0, is_empty, &map_out,
                                &status);
    if (log_$check_op_status(log_$op_map, &status) < 0) {
        return;
    }

    /* 0x00E30134-0x00E30164: FILE_$LOCK(&uid, index=&0 (0x00E30238),
     * mode=&4 (0x00E3023A), rights=&0 (0x00E3023C), &lock_out, &status). */
    FILE_$LOCK(&LOG_$LOGFILE_UID, &log_$lock_index, &log_$lock_mode,
               &log_$lock_rights, &lock_out, &status);
    if (log_$check_op_status(log_$op_lock, &status) < 0) {
        return;
    }

    /* 0x00E30168-0x00E30186: a zero-length file, or a page whose two index
     * words are both zero, is initialised to head 0 / tail 1 with ONE
     * longword store (`moveq #1` / `move.l D0,(A0)`) and the log is dirty. */
    if (is_empty < 0 || (page[0] == 0 && page[1] == 0)) {
        page[0] = 0;
        page[1] = 1;
        LOG_$STATE.dirty_flag = -1;
    }

    /* 0x00E3018A-0x00E301AE */
    LOG_$STATE.wired_handle = MST_$WIRE(ARCH_PTR_TO_VA(page), &status);
    if (log_$check_op_status(log_$op_wire, &status) < 0) {
        return;
    }

    /* 0x00E301B0-0x00E301B6: from here on LOG_$ADD writes to the page. */
    LOG_$LOGFILE_PTR = page;

    /* 0x00E301BA-0x00E301EC: the entry that was being written when the
     * system last went down is re-added with its original timestamp (the
     * new entry's timestamp word is overwritten through
     * current_entry_ptr), then the magic is cleared. */
    if (LOG_$LAST_ENTRY.magic == LOG_PENDING_MAGIC) {
        LOG_$ADD(LOG_$LAST_ENTRY.type, LOG_$LAST_ENTRY.data,
                 LOG_$LAST_ENTRY.size);
        ((log_entry_header_t *)LOG_$STATE.current_entry_ptr)->timestamp =
            LOG_$LAST_ENTRY.timestamp;
        LOG_$LAST_ENTRY.magic = 0;
    }

    /* 0x00E301EE-0x00E30210: the crash record's 8 bytes as a type-5 entry;
     * the magic is cleared BEFORE the add here. */
    if (CRASH_$RECORD.magic == LOG_PENDING_MAGIC) {
        CRASH_$RECORD.magic = 0;
        LOG_$ADD(LOG_TYPE_CRASH, CRASH_$RECORD.data, 8);
    }

    /* 0x00E30212-0x00E3021A: the init entry, type 0, no data (the zero cell
     * at 0x00E2FFFC, pea (-0x21a,PC)). */
    LOG_$ADD(LOG_TYPE_INIT, &LOG_$VFMT_NO_ARG, 0);
}
