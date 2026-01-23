/*
 * svc/svc_tables.c - System Call Dispatch Tables
 *
 * This file contains the syscall handler tables for all TRAP dispatchers.
 * The tables are pure data (function pointer arrays) and are shared across
 * all SAU types. The actual trap dispatch code is architecture-specific
 * and lives in sau<N>/*.s files.
 *
 * Table addresses in original binary:
 *   SVC_$TRAP0_TABLE: 0x00e7b2de (32 entries)
 *   SVC_$TRAP1_TABLE: 0x00e7b360 (66 entries)
 *   SVC_$TRAP5_TABLE: 0x00e7baf2 (99 entries)
 */

#include "svc/svc.h"

/* Subsystem headers for syscall handlers */
#include "acl/acl.h"
#include "as/as.h"
#include "asknode/asknode.h"
#include "ast/ast.h"
#include "audit/audit.h"
#include "cache/cache.h"
#include "cal/cal.h"
#include "dir/dir.h"
#include "disk/disk.h"
#include "dtty/dtty.h"
#include "file/file.h"
#include "file/file_internal.h" // for FILE_$READ_LOCK_ENTRYI and FILE_$READ_LOCK_ENTRYUI
#include "fim/fim.h"
#include "gpu/gpu.h"
#include "hint/hint.h"
#include "log/log.h"
#include "mac/mac.h"
#include "misc/misc.h"
#include "msg/msg.h"
#include "mst/mst.h"
#include "name/name.h"
#include "net/net.h"
#include "netlog/netlog.h"
#include "network/network.h"
#include "os/os.h"
#include "osinfo/osinfo.h"
#include "pacct/pacct.h"
#include "pbu/pbu.h"
#include "pchist/pchist.h"
#include "peb/peb.h"
#include "proc2/proc2.h"
#include "ring/ring.h"
#include "ring/ringlog.h"
#include "rip/rip.h"
#include "route/route.h"
#include "sio/sio.h"
#include "smd/smd.h"
#include "term/term.h"
#include "tone/tone.h"
#include "tpad/tpad.h"
#include "tty/tty.h"
#include "volx/volx.h"
#include "vtoc/vtoc.h"
#include "xns/xns.h"
#include "xpd/xpd.h"
#include "vfmt/vfmt.h"
#include "stop/stop.h"

/*
 * Forward declarations for syscall handlers not yet in subsystem headers.
 * As headers are created for these subsystems, move includes above and
 * remove the corresponding extern declarations here.
 */

/* Error handlers (in svc/sau2/*.s) */
extern void SVC_$INVALID_SYSCALL(void);
extern void SVC_$UNIMPLEMENTED(void);

/* TRAP #0 handlers not yet in headers */
extern void FUN_00e0aa04(void);          /* TODO: identify - returns FIM addr */

/* TRAP #1 handlers not yet in headers */
extern void FUN_00e0a9c2(void);          /* TODO: identify - sets FIM user addr */

/*
 * ============================================================================
 * SVC_$TRAP0_TABLE - Simple syscall handlers (32 entries)
 * ============================================================================
 *
 * TRAP #0 syscalls take no arguments - handler is called directly.
 *
 * Original address: 0x00e7b2de
 */
void *SVC_$TRAP0_TABLE[SVC_TRAP0_TABLE_SIZE] = {
    /* 0x00 */ PROC2_$DELETE,
    /* 0x01 */ FUN_00e0aa04,              /* TODO: returns FIM addr for AS */
    /* 0x02 */ SVC_$INVALID_SYSCALL,
    /* 0x03 */ DTTY_$RELOAD_FONT,
    /* 0x04 */ FILE_$UNLOCK_ALL,
    /* 0x05 */ PEB_$ASSOC,
    /* 0x06 */ PEB_$DISSOC,
    /* 0x07 */ PROC2_$MY_PID,
    /* 0x08 */ SMD_$OP_WAIT_U,
    /* 0x09 */ TPAD_$RE_RANGE,
    /* 0x0A */ SVC_$INVALID_SYSCALL,
    /* 0x0B */ SVC_$UNIMPLEMENTED,
    /* 0x0C */ SVC_$INVALID_SYSCALL,
    /* 0x0D */ ACL_$UP,
    /* 0x0E */ ACL_$DOWN,
    /* 0x0F */ SVC_$UNIMPLEMENTED,
    /* 0x10 */ TPAD_$INQ_DTYPE,
    /* 0x11 */ SVC_$INVALID_SYSCALL,
    /* 0x12 */ CACHE_$CLEAR,
    /* 0x13 */ RIP_$ANNOUNCE_NS,
    /* 0x14 */ SVC_$UNIMPLEMENTED,
    /* 0x15 */ SVC_$UNIMPLEMENTED,
    /* 0x16 */ SVC_$UNIMPLEMENTED,
    /* 0x17 */ SVC_$INVALID_SYSCALL,
    /* 0x18 */ PROC2_$DELIVER_PENDING,
    /* 0x19 */ PROC2_$COMPLETE_FORK,
    /* 0x1A */ PACCT_$STOP,
    /* 0x1B */ PACCT_$ON,
    /* 0x1C */ ACL_$GET_LOCAL_LOCKSMITH,
    /* 0x1D */ ACL_$IS_SUSER,
    /* 0x1E */ SVC_$INVALID_SYSCALL,
    /* 0x1F */ SMD_$N_DEVICES,
};

/*
 * ============================================================================
 * SVC_$TRAP1_TABLE - 1-argument syscall handlers (66 entries)
 * ============================================================================
 *
 * TRAP #1 syscalls take 1 argument via user stack at (USP+0x04).
 * The dispatcher validates USP and the argument pointer < 0xCC0000.
 *
 * Original address: 0x00e7b360
 */
void *SVC_$TRAP1_TABLE[SVC_TRAP1_TABLE_SIZE] = {
    /* 0x00 */ SVC_$INVALID_SYSCALL,
    /* 0x01 */ SVC_$INVALID_SYSCALL,
    /* 0x02 */ FUN_00e0a9c2,              /* TODO: sets FIM user address */
    /* 0x03 */ NETWORK_$READ_SERVICE,
    /* 0x04 */ PROC1_$GET_CPUT,
    /* 0x05 */ SET_LITES_LOC,
    /* 0x06 */ TIME_$CLOCK,
    /* 0x07 */ ASKNODE_$READ_FAILURE_REC,
    /* 0x08 */ CAL_$APPLY_LOCAL_OFFSET,
    /* 0x09 */ CAL_$GET_INFO,
    /* 0x0A */ CAL_$GET_LOCAL_TIME,
    /* 0x0B */ CAL_$REMOVE_LOCAL_OFFSET,
    /* 0x0C */ CAL_$SET_DRIFT,
    /* 0x0D */ DISK_$GET_ERROR_INFO,
    /* 0x0E */ SVC_$UNIMPLEMENTED,
    /* 0x0F */ MSG_$CLOSE,
    /* 0x10 */ PROC2_$WHO_AM_I,
    /* 0x11 */ SMD_$CLEAR_KBD_CURSOR,
    /* 0x12 */ SVC_$INVALID_SYSCALL,
    /* 0x13 */ SVC_$INVALID_SYSCALL,
    /* 0x14 */ SMD_$SEND_RESPONSE,
    /* 0x15 */ SMD_$STOP_TP_CURSOR,
    /* 0x16 */ SVC_$INVALID_SYSCALL,
    /* 0x17 */ SVC_$INVALID_SYSCALL,
    /* 0x18 */ SMD_$UNMAP_DISPLAY_U,
    /* 0x19 */ UID_$GEN,
    /* 0x1A */ TONE_$TIME,
    /* 0x1B */ SMD_$INQ_DISP_TYPE,
    /* 0x1C */ SMD_$INVERT_S,
    /* 0x1D */ SMD_$INQ_MM_BLT,
    /* 0x1E */ SVC_$UNIMPLEMENTED,
    /* 0x1F */ TPAD_$SET_CURSOR,
    /* 0x20 */ SMD_$EOF_WAIT,
    /* 0x21 */ SVC_$UNIMPLEMENTED,
    /* 0x22 */ NAME_$GET_WDIR_UID,
    /* 0x23 */ NAME_$GET_NDIR_UID,
    /* 0x24 */ NAME_$GET_ROOT_UID,
    /* 0x25 */ NAME_$GET_NODE_UID,
    /* 0x26 */ NAME_$GET_NODE_DATA_UID,
    /* 0x27 */ NAME_$GET_CANNED_ROOT_UID,
    /* 0x28 */ MSG_$GET_MY_NET,
    /* 0x29 */ MSG_$GET_MY_NODE,
    /* 0x2A */ SVC_$INVALID_SYSCALL,
    /* 0x2B */ SVC_$INVALID_SYSCALL,
    /* 0x2C */ SVC_$UNIMPLEMENTED,
    /* 0x2D */ SVC_$INVALID_SYSCALL,
    /* 0x2E */ SVC_$UNIMPLEMENTED,
    /* 0x2F */ SVC_$UNIMPLEMENTED,
    /* 0x30 */ GPU_$INIT,
    /* 0x31 */ SVC_$UNIMPLEMENTED,
    /* 0x32 */ SMD_$INIT_STATE,
    /* 0x33 */ SMD_$CLR_TRK_RECT,
    /* 0x34 */ PROC2_$GET_SIG_MASK,
    /* 0x35 */ FIM_$FRESTORE,
    /* 0x36 */ TIME_$GET_TIME_OF_DAY,
    /* 0x37 */ PROC1_$GET_LOADAV,
    /* 0x38 */ PROC2_$GET_BOOT_FLAGS,
    /* 0x39 */ SVC_$INVALID_SYSCALL,
    /* 0x3A */ PROC2_$SET_TTY,
    /* 0x3B */ OS_$SHUTDOWN,
    /* 0x3C */ PBU_$FAULTED_UNITS,
    /* 0x3D */ PROC2_$GET_CPU_USAGE,
    /* 0x3E */ SVC_$INVALID_SYSCALL,
    /* 0x3F */ SVC_$INVALID_SYSCALL,
    /* 0x40 */ SVC_$INVALID_SYSCALL,
    /* 0x41 */ TIME_$GET_ADJUST,
};

/*
 * ============================================================================
 * SVC_$TRAP2_TABLE - 2-argument syscall handlers (133 entries)
 * ============================================================================
 *
 * TRAP #2 syscalls take 2 arguments via user stack at (USP+0x04) and (USP+0x08).
 * The dispatcher validates USP and both argument pointers < 0xCC0000.
 *
 * Original address: 0x00e7b466
 */
void *SVC_$TRAP2_TABLE[SVC_TRAP2_TABLE_SIZE] = {
    /* 0x00 */ SVC_$INVALID_SYSCALL,
    /* 0x01 */ SVC_$INVALID_SYSCALL,
    /* 0x02 */ FILE_$DELETE,
    /* 0x03 */ EC2_$WAKEUP,
    /* 0x04 */ SVC_$INVALID_SYSCALL,
    /* 0x05 */ ACL_$GET_SID,
    /* 0x06 */ FILE_$MK_PERMANENT,
    /* 0x07 */ FILE_$UNLOCK_VOL,
    /* 0x08 */ CAL_$READ_TIMEZONE,
    /* 0x09 */ CAL_$SEC_TO_CLOCK,
    /* 0x0A */ CAL_$WRITE_TIMEZONE,
    /* 0x0B */ DISK_$UNASSIGN,
    /* 0x0C */ FILE_$FORCE_UNLOCK,
    /* 0x0D */ FILE_$FW_FILE,
    /* 0x0E */ FILE_$MK_IMMUTABLE,
    /* 0x0F */ FILE_$PURIFY,
    /* 0x10 */ GET_BUILD_TIME,
    /* 0x11 */ SVC_$UNIMPLEMENTED,
    /* 0x12 */ SVC_$UNIMPLEMENTED,
    /* 0x13 */ MSG_$ALLOCATE,
    /* 0x14 */ MSG_$OPEN,
    /* 0x15 */ MSG_$WAIT,
    /* 0x16 */ SVC_$INVALID_SYSCALL,
    /* 0x17 */ SVC_$INVALID_SYSCALL,
    /* 0x18 */ SVC_$UNIMPLEMENTED,
    /* 0x19 */ SVC_$UNIMPLEMENTED,
    /* 0x1A */ SVC_$UNIMPLEMENTED,
    /* 0x1B */ SVC_$UNIMPLEMENTED,
    /* 0x1C */ PROC2_$QUIT,
    /* 0x1D */ PROC2_$RESUME,
    /* 0x1E */ PROC2_$SUSPEND,
    /* 0x1F */ SMD_$BLT_U,
    /* 0x20 */ SMD_$CLEAR_WINDOW,
    /* 0x21 */ SVC_$INVALID_SYSCALL,
    /* 0x22 */ SMD_$DRAW_BOX,
    /* 0x23 */ SVC_$INVALID_SYSCALL,
    /* 0x24 */ SMD_$LOAD_FONT,
    /* 0x25 */ SVC_$INVALID_SYSCALL,
    /* 0x26 */ SMD_$MAP_DISPLAY_U,
    /* 0x27 */ SMD_$MOVE_KBD_CURSOR,
    /* 0x28 */ SVC_$INVALID_SYSCALL,
    /* 0x29 */ SMD_$RETURN_DISPLAY,
    /* 0x2A */ SVC_$INVALID_SYSCALL,
    /* 0x2B */ SVC_$INVALID_SYSCALL,
    /* 0x2C */ SMD_$UNLOAD_FONT,
    /* 0x2D */ SVC_$INVALID_SYSCALL,
    /* 0x2E */ PEB_$GET_INFO,
    /* 0x2F */ SVC_$INVALID_SYSCALL,
    /* 0x30 */ EC2_$GET_VAL,
    /* 0x31 */ AST_$ADD_ASTES,
    /* 0x32 */ SVC_$UNIMPLEMENTED,
    /* 0x33 */ PROC2_$MAKE_ORPHAN,
    /* 0x34 */ FILE_$DELETE_FORCE,
    /* 0x35 */ FILE_$DELETE_WHEN_UNLOCKED,
    /* 0x36 */ FILE_$MK_TEMPORARY,
    /* 0x37 */ SVC_$INVALID_SYSCALL,
    /* 0x38 */ SVC_$INVALID_SYSCALL,
    /* 0x39 */ SVC_$INVALID_SYSCALL,
    /* 0x3A */ SVC_$INVALID_SYSCALL,
    /* 0x3B */ SMD_$INQ_KBD_CURSOR,
    /* 0x3C */ ACL_$ENTER_SUBS,
    /* 0x3D */ SVC_$INVALID_SYSCALL,
    /* 0x3E */ SVC_$UNIMPLEMENTED,
    /* 0x3F */ SVC_$UNIMPLEMENTED,
    /* 0x40 */ SVC_$UNIMPLEMENTED,
    /* 0x41 */ SVC_$UNIMPLEMENTED,
    /* 0x42 */ FILE_$DELETE_FORCE_WHEN_UNLOCKED,
    /* 0x43 */ SMD_$SET_CLIP_WINDOW,
    /* 0x44 */ SVC_$UNIMPLEMENTED,
    /* 0x45 */ HINT_$ADD,
    /* 0x46 */ DIR_$FIX_DIR,
    /* 0x47 */ NAME_$SET_WDIRUS,
    /* 0x48 */ NAME_$SET_NDIRUS,
    /* 0x49 */ SVC_$UNIMPLEMENTED,
    /* 0x4A */ SVC_$INVALID_SYSCALL,
    /* 0x4B */ MSG_$CLOSEI,
    /* 0x4C */ NETWORK_$ADD_PAGE_SERVERS,
    /* 0x4D */ NETWORK_$ADD_REQUEST_SERVERS,
    /* 0x4E */ ACL_$ADD_PROJ,
    /* 0x4F */ ACL_$DELETE_PROJ,
    /* 0x50 */ XPD_$GET_FP,
    /* 0x51 */ XPD_$PUT_FP,
    /* 0x52 */ SMD_$SET_TP_REPORTING,
    /* 0x53 */ SMD_$DISABLE_TRACKING,
    /* 0x54 */ SVC_$UNIMPLEMENTED,
    /* 0x55 */ SVC_$UNIMPLEMENTED,
    /* 0x56 */ SVC_$INVALID_SYSCALL,
    /* 0x57 */ SVC_$INVALID_SYSCALL,
    /* 0x58 */ HINT_$ADDI,
    /* 0x59 */ SVC_$UNIMPLEMENTED,
    /* 0x5A */ SVC_$UNIMPLEMENTED,
    /* 0x5B */ SVC_$UNIMPLEMENTED,
    /* 0x5C */ SMD_$SET_DISP_UNIT,
    /* 0x5D */ SMD_$VIDEO_CTL,
    /* 0x5E */ SMD_$SET_CURSOR_POS,
    /* 0x5F */ TERM_$SEND_KBD_STRING,
    /* 0x60 */ AUDIT_$CONTROL,
    /* 0x61 */ PROC2_$SIGBLOCK,
    /* 0x62 */ PROC2_$SIGSETMASK,
    /* 0x63 */ PROC2_$SIGPAUSE,
    /* 0x64 */ SVC_$INVALID_SYSCALL,
    /* 0x65 */ AS_$GET_ADDR,
    /* 0x66 */ PROC2_$GET_ASID,
    /* 0x67 */ TTY_$K_FLUSH_INPUT,
    /* 0x68 */ TTY_$K_FLUSH_OUTPUT,
    /* 0x69 */ TTY_$K_DRAIN_OUTPUT,
    /* 0x6A */ PROC2_$DEBUG,
    /* 0x6B */ PROC2_$UNDEBUG,
    /* 0x6C */ ACL_$DEF_ACLDATA,
    /* 0x6D */ PROC2_$OVERRIDE_DEBUG,
    /* 0x6E */ TIME_$SET_TIME_OF_DAY,
    /* 0x6F */ CAL_$DECODE_TIME,
    /* 0x70 */ ACL_$INHERIT_SUBSYS,
    /* 0x71 */ ACL_$SET_LOCAL_LOCKSMITH,
    /* 0x72 */ SVC_$INVALID_SYSCALL,
    /* 0x73 */ SMD_$DISSOC,
    /* 0x74 */ SMD_$BUSY_WAIT,
    /* 0x75 */ TTY_$K_RESET,
    /* 0x76 */ SVC_$INVALID_SYSCALL,
    /* 0x77 */ SVC_$INVALID_SYSCALL,
    /* 0x78 */ SVC_$INVALID_SYSCALL,
    /* 0x79 */ SVC_$UNIMPLEMENTED,
    /* 0x7A */ SVC_$UNIMPLEMENTED,
    /* 0x7B */ SVC_$UNIMPLEMENTED,
    /* 0x7C */ SVC_$INVALID_SYSCALL,
    /* 0x7D */ TPAD_$RE_RANGE_UNIT,
    /* 0x7E */ DISK_$FORMAT_WHOLE,
    /* 0x7F */ MAC_$CLOSE,
    /* 0x80 */ MAC_$NET_TO_PORT_NUM,
    /* 0x81 */ XNS_IDP_$OPEN,
    /* 0x82 */ XNS_IDP_$CLOSE,
    /* 0x83 */ XNS_IDP_$GET_STATS,
    /* 0x84 */ SVC_$INVALID_SYSCALL,
};

/*
 * ============================================================================
 * SVC_$TRAP3_TABLE - 3-argument syscall handlers (155 entries)
 * ============================================================================
 *
 * TRAP #3 syscalls take 3 arguments via user stack at (USP+0x04), (USP+0x08),
 * and (USP+0x0C). The dispatcher validates USP and all argument pointers < 0xCC0000.
 *
 * Original address: 0x00e7b67a
 */
void *SVC_$TRAP3_TABLE[SVC_TRAP3_TABLE_SIZE] = {
    /* 0x00 */ SVC_$UNIMPLEMENTED,
    /* 0x01 */ SVC_$INVALID_SYSCALL,
    /* 0x02 */ FILE_$CREATE,
    /* 0x03 */ FILE_$UNLOCK,
    /* 0x04 */ FILE_$TRUNCATE,
    /* 0x05 */ MST_$UNMAPS,
    /* 0x06 */ ERROR_$PRINT,
    /* 0x07 */ FILE_$ATTRIBUTES,
    /* 0x08 */ FILE_$SET_LEN,
    /* 0x09 */ FILE_$SET_TYPE,
    /* 0x0A */ SVC_$INVALID_SYSCALL,
    /* 0x0B */ SVC_$INVALID_SYSCALL,
    /* 0x0C */ SVC_$INVALID_SYSCALL,
    /* 0x0D */ SVC_$INVALID_SYSCALL,
    /* 0x0E */ NETWORK_$SET_SERVICE,
    /* 0x0F */ ASKNODE_$WHO,
    /* 0x10 */ FILE_$ACT_ATTRIBUTES,
    /* 0x11 */ FILE_$LOCATE,
    /* 0x12 */ FILE_$NEIGHBORS,
    /* 0x13 */ FILE_$READ_LOCK_ENTRYU,
    /* 0x14 */ FILE_$SET_ACL,
    /* 0x15 */ FILE_$SET_DIRPTR,
    /* 0x16 */ FILE_$SET_TROUBLE,
    /* 0x17 */ SVC_$INVALID_SYSCALL,
    /* 0x18 */ SVC_$UNIMPLEMENTED,
    /* 0x19 */ SVC_$UNIMPLEMENTED,
    /* 0x1A */ SVC_$UNIMPLEMENTED,
    /* 0x1B */ SVC_$INVALID_SYSCALL,
    /* 0x1C */ SVC_$UNIMPLEMENTED,
    /* 0x1D */ SVC_$UNIMPLEMENTED,
    /* 0x1E */ SVC_$UNIMPLEMENTED,
    /* 0x1F */ PROC2_$LIST,
    /* 0x20 */ FIM_$SINGLE_STEP,
    /* 0x21 */ SMD_$ASSOC,
    /* 0x22 */ SMD_$BORROW_DISPLAY,
    /* 0x23 */ SMD_$CLEAR_CURSOR,
    /* 0x24 */ SMD_$DISPLAY_CURSOR,
    /* 0x25 */ SMD_$SET_TP_CURSOR,
    /* 0x26 */ TIME_$WAIT,
    /* 0x27 */ RINGLOG_$CNTL,
    /* 0x28 */ SMD_$ALLOC_HDM,
    /* 0x29 */ SMD_$FREE_HDM,
    /* 0x2A */ SVC_$INVALID_SYSCALL,
    /* 0x2B */ OS_$GET_EC,
    /* 0x2C */ TIME_$GET_EC,
    /* 0x2D */ SVC_$UNIMPLEMENTED,
    /* 0x2E */ PROC2_$UPID_TO_UID,
    /* 0x2F */ MSG_$GET_EC,
    /* 0x30 */ DISK_$AS_OPTIONS,
    /* 0x31 */ SMD_$GET_EC,
    /* 0x32 */ NAME_$SET_ACL,
    /* 0x33 */ FILE_$SET_REFCNT,
    /* 0x34 */ SVC_$INVALID_SYSCALL,
    /* 0x35 */ PROC1_$GET_INFO,
    /* 0x36 */ SVC_$INVALID_SYSCALL,
    /* 0x37 */ SVC_$INVALID_SYSCALL,
    /* 0x38 */ SVC_$INVALID_SYSCALL,
    /* 0x39 */ SVC_$INVALID_SYSCALL,
    /* 0x3A */ SVC_$INVALID_SYSCALL,
    /* 0x3B */ SVC_$UNIMPLEMENTED,
    /* 0x3C */ AS_$GET_INFO,
    /* 0x3D */ FILE_$SET_DTM,
    /* 0x3E */ FILE_$SET_DTU,
    /* 0x3F */ SVC_$INVALID_SYSCALL,
    /* 0x40 */ LOG_$READ,
    /* 0x41 */ PROC2_$SET_PGROUP,
    /* 0x42 */ SMD_$SET_BLANK_TIMEOUT,
    /* 0x43 */ SMD_$INQ_BLANK_TIMEOUT,
    /* 0x44 */ FILE_$REMOVE_WHEN_UNLOCKED,
    /* 0x45 */ SVC_$UNIMPLEMENTED,
    /* 0x46 */ PROC2_$UPGID_TO_UID,
    /* 0x47 */ TIME_$GET_ITIMER,
    /* 0x48 */ DIR_$SET_DAD,
    /* 0x49 */ XPD_$GET_EC,
    /* 0x4A */ XPD_$SET_DEBUGGER,
    /* 0x4B */ XPD_$POST_EVENT,
    /* 0x4C */ XPD_$SET_ENABLE,
    /* 0x4D */ SVC_$INVALID_SYSCALL,
    /* 0x4E */ XPD_$CONTINUE_PROC,
    /* 0x4F */ RIP_$TABLE,
    /* 0x50 */ FILE_$LOCATEI,
    /* 0x51 */ MSG_$OPENI,
    /* 0x52 */ MSG_$ALLOCATEI,
    /* 0x53 */ MSG_$WAITI,
    /* 0x54 */ ACL_$SET_PROJ_LIST,
    /* 0x55 */ ACL_$GET_RE_SIDS,
    /* 0x56 */ SVC_$INVALID_SYSCALL,
    /* 0x57 */ MSG_$SET_HPIPC,
    /* 0x58 */ DIR_$VALIDATE_ROOT_ENTRY,
    /* 0x59 */ SMD_$ENABLE_TRACKING,
    /* 0x5A */ FILE_$READ_LOCK_ENTRYUI,
    /* 0x5B */ SVC_$UNIMPLEMENTED,
    /* 0x5C */ ROUTE_$SERVICE,
    /* 0x5D */ SVC_$INVALID_SYSCALL,
    /* 0x5E */ SVC_$UNIMPLEMENTED,
    /* 0x5F */ SVC_$UNIMPLEMENTED,
    /* 0x60 */ SVC_$UNIMPLEMENTED,
    /* 0x61 */ SVC_$UNIMPLEMENTED,
    /* 0x62 */ XPD_$GET_EVENT_AND_DATA,
    /* 0x63 */ SMD_$GET_IDM_EVENT,
    /* 0x64 */ MSG_$TEST_FOR_MESSAGE,
    /* 0x65 */ SMD_$ADD_TRK_RECT,
    /* 0x66 */ SMD_$DEL_TRK_RECT,
    /* 0x67 */ SMD_$SET_KBD_TYPE,
    /* 0x68 */ SVC_$UNIMPLEMENTED,
    /* 0x69 */ FILE_$SET_AUDITED,
    /* 0x6A */ PROC2_$ACKNOWLEDGE,
    /* 0x6B */ PROC2_$GET_MY_UPIDS,
    /* 0x6C */ TTY_$K_INQ_INPUT_FLAGS,
    /* 0x6D */ TTY_$K_INQ_OUTPUT_FLAGS,
    /* 0x6E */ TTY_$K_INQ_ECHO_FLAGS,
    /* 0x6F */ TTY_$K_SET_INPUT_BREAK_MODE,
    /* 0x70 */ TTY_$K_INQ_INPUT_BREAK_MODE,
    /* 0x71 */ TTY_$K_SET_PGROUP,
    /* 0x72 */ TTY_$K_INQ_PGROUP,
    /* 0x73 */ TTY_$K_SIMULATE_TERMINAL_INPUT,
    /* 0x74 */ TTY_$K_INQ_FUNC_ENABLED,
    /* 0x75 */ SIO_$K_TIMED_BREAK,
    /* 0x76 */ FILE_$SET_DEVNO,
    /* 0x77 */ XPD_$SET_PTRACE_OPTS,
    /* 0x78 */ XPD_$INQ_PTRACE_OPTS,
    /* 0x79 */ FILE_$SET_MAND_LOCK,
    /* 0x7A */ TIME_$SET_CPU_LIMIT,
    /* 0x7B */ CAL_$WEEKDAY,
    /* 0x7C */ SIO_$K_SIGNAL_WAIT,
    /* 0x7D */ TERM_$SET_DISCIPLINE,
    /* 0x7E */ PROC2_$SET_SERVER,
    /* 0x7F */ PACCT_$START,
    /* 0x80 */ FILE_$SET_DTU_F,
    /* 0x81 */ PROC2_$PGUID_TO_UPGID,
    /* 0x82 */ TERM_$INQ_DISCIPLINE,
    /* 0x83 */ SVC_$UNIMPLEMENTED,
    /* 0x84 */ SVC_$INVALID_SYSCALL,
    /* 0x85 */ MST_$UNMAPS_AND_FREE_AREA,
    /* 0x86 */ SMD_$ASSOC_CSRS,
    /* 0x87 */ SMD_$INQ_DISP_INFO,
    /* 0x88 */ SMD_$INQ_DISP_UID,
    /* 0x89 */ SMD_$DISPLAY_LOGO,
    /* 0x8A */ TERM_$SET_REAL_LINE_DISCIPLINE,
    /* 0x8B */ TIME_$ADJUST_TIME_OF_DAY,
    /* 0x8C */ PROC2_$UID_TO_UPID,
    /* 0x8D */ PROC2_$SET_SESSION_ID,
    /* 0x8E */ SMD_$GET_UNIT_EVENT,
    /* 0x8F */ TPAD_$SET_UNIT_CURSOR,
    /* 0x90 */ TPAD_$SET_PUNCH_IMPACT,
    /* 0x91 */ TPAD_$INQ_PUNCH_IMPACT,
    /* 0x92 */ TTY_$K_INQ_SESSION_ID,
    /* 0x93 */ TTY_$K_SET_SESSION_ID,
    /* 0x94 */ MAC_$OPEN,
    /* 0x95 */ MAC_$RECEIVE,
    /* 0x96 */ XNS_IDP_$RECEIVE,
    /* 0x97 */ XNS_IDP_$GET_PORT_INFO,
    /* 0x98 */ SVC_$UNIMPLEMENTED,
    /* 0x99 */ SMD_$SET_UNIT_CURSOR_POS,
    /* 0x9A */ SMD_$CLR_AND_LOAD_TRK_RECT,
};

/*
 * ============================================================================
 * SVC_$TRAP4_TABLE - 4-argument syscall handlers (131 entries)
 * ============================================================================
 *
 * TRAP #4 syscalls take 4 arguments via user stack at (USP+0x04), (USP+0x08),
 * (USP+0x0C), and (USP+0x10). The dispatcher validates USP and all four
 * argument pointers < 0xCC0000.
 *
 * Original address: 0x00e7b8e6
 */
void *SVC_$TRAP4_TABLE[SVC_TRAP4_TABLE_SIZE] = {
    /* 0x00 */ SVC_$INVALID_SYSCALL,
    /* 0x01 */ MST_$SET_GUARD,
    /* 0x02 */ MST_$UNMAP_GLOBAL,
    /* 0x03 */ MST_$GET_UID,
    /* 0x04 */ EC2_$WAIT,
    /* 0x05 */ FILE_$READ_LOCK_ENTRY,
    /* 0x06 */ MST_$UNMAP,
    /* 0x07 */ MST_$GROW_AREA,
    /* 0x08 */ SVC_$INVALID_SYSCALL,
    /* 0x09 */ SVC_$INVALID_SYSCALL,
    /* 0x0A */ SVC_$INVALID_SYSCALL,
    /* 0x0B */ SVC_$INVALID_SYSCALL,
    /* 0x0C */ TERM_$CONTROL,
    /* 0x0D */ TERM_$READ,
    /* 0x0E */ TERM_$WRITE,
    /* 0x0F */ DISK_$FORMAT,
    /* 0x10 */ DISK_$LV_ASSIGN,
    /* 0x11 */ FILE_$FW_PARTIAL,
    /* 0x12 */ SVC_$UNIMPLEMENTED,
    /* 0x13 */ SVC_$UNIMPLEMENTED,
    /* 0x14 */ SVC_$UNIMPLEMENTED,
    /* 0x15 */ SVC_$UNIMPLEMENTED,
    /* 0x16 */ SVC_$UNIMPLEMENTED,
    /* 0x17 */ SVC_$INVALID_SYSCALL,
    /* 0x18 */ SVC_$UNIMPLEMENTED,
    /* 0x19 */ SVC_$INVALID_SYSCALL,
    /* 0x1A */ SVC_$INVALID_SYSCALL,
    /* 0x1B */ SVC_$INVALID_SYSCALL,
    /* 0x1C */ SVC_$INVALID_SYSCALL,
    /* 0x1D */ SVC_$INVALID_SYSCALL,
    /* 0x1E */ SVC_$INVALID_SYSCALL,
    /* 0x1F */ SVC_$INVALID_SYSCALL,
    /* 0x20 */ SVC_$INVALID_SYSCALL,
    /* 0x21 */ SVC_$INVALID_SYSCALL,
    /* 0x22 */ SVC_$INVALID_SYSCALL,
    /* 0x23 */ SVC_$UNIMPLEMENTED,
    /* 0x24 */ SVC_$UNIMPLEMENTED,
    /* 0x25 */ PCHIST_$CNTL,
    /* 0x26 */ PROC2_$GET_INFO,
    /* 0x27 */ SMD_$BLT,
    /* 0x28 */ SVC_$INVALID_SYSCALL,
    /* 0x29 */ SVC_$INVALID_SYSCALL,
    /* 0x2A */ SMD_$SIGNAL,
    /* 0x2B */ SMD_$SOFT_SCROLL,
    /* 0x2C */ SVC_$INVALID_SYSCALL,
    /* 0x2D */ TERM_$INQUIRE,
    /* 0x2E */ SVC_$INVALID_SYSCALL,
    /* 0x2F */ SVC_$INVALID_SYSCALL,
    /* 0x30 */ SVC_$INVALID_SYSCALL,
    /* 0x31 */ TERM_$GET_EC,
    /* 0x32 */ SVC_$UNIMPLEMENTED,
    /* 0x33 */ SVC_$UNIMPLEMENTED,
    /* 0x34 */ SVC_$UNIMPLEMENTED,
    /* 0x35 */ SVC_$UNIMPLEMENTED,
    /* 0x36 */ SVC_$INVALID_SYSCALL,
    /* 0x37 */ SVC_$INVALID_SYSCALL,
    /* 0x38 */ TERM_$READ_COND,
    /* 0x39 */ SVC_$INVALID_SYSCALL,
    /* 0x3A */ SVC_$INVALID_SYSCALL,
    /* 0x3B */ SVC_$UNIMPLEMENTED,
    /* 0x3C */ PROC2_$SET_NAME,
    /* 0x3D */ PROC2_$SET_PRIORITY,
    /* 0x3E */ PROC2_$GET_EC,
    /* 0x3F */ PROC2_$LIST_PGROUP,
    /* 0x40 */ SVC_$INVALID_SYSCALL,
    /* 0x41 */ SVC_$INVALID_SYSCALL,
    /* 0x42 */ SVC_$INVALID_SYSCALL,
    /* 0x43 */ SVC_$UNIMPLEMENTED,
    /* 0x44 */ SVC_$UNIMPLEMENTED,
    /* 0x45 */ DIR_$DROP_DIRU,
    /* 0x46 */ DIR_$SET_DEFAULT_ACL,
    /* 0x47 */ DIR_$GET_DEFAULT_ACL,
    /* 0x48 */ NAME_$READ_DIRS_PS,
    /* 0x49 */ SVC_$INVALID_SYSCALL,
    /* 0x4A */ ACL_$GET_PROJ_LIST,
    /* 0x4B */ MST_$CHANGE_RIGHTS,
    /* 0x4C */ XPD_$GET_TARGET_INFO,
    /* 0x4D */ FILE_$READ_LOCK_ENTRYI,
    /* 0x4E */ ROUTE_$INCOMING,
    /* 0x4F */ SVC_$UNIMPLEMENTED,
    /* 0x50 */ SMD_$INQ_KBD_TYPE,
    /* 0x51 */ ROUTE_$GET_EC,
    /* 0x52 */ SVC_$INVALID_SYSCALL,
    /* 0x53 */ SMD_$DM_COND_EVENT_WAIT,
    /* 0x54 */ DISK_$READ_MFG_BADSPOTS,
    /* 0x55 */ DISK_$GET_MNT_INFO,
    /* 0x56 */ PROC2_$SET_SIG_MASK,
    /* 0x57 */ PROC2_$SIGRETURN,
    /* 0x58 */ PROC2_$WAIT,
    /* 0x59 */ PROC2_$SIGNAL,
    /* 0x5A */ PROC2_$SIGNAL_PGROUP,
    /* 0x5B */ PROC2_$GET_CR_REC,
    /* 0x5C */ TTY_$K_SET_FUNC_CHAR,
    /* 0x5D */ TTY_$K_INQ_FUNC_CHAR,
    /* 0x5E */ TTY_$K_SET_INPUT_FLAG,
    /* 0x5F */ TTY_$K_SET_OUTPUT_FLAG,
    /* 0x60 */ TTY_$K_SET_ECHO_FLAG,
    /* 0x61 */ TTY_$K_ENABLE_FUNC,
    /* 0x62 */ SIO_$K_SET_PARAM,
    /* 0x63 */ SIO_$K_INQ_PARAM,
    /* 0x64 */ FILE_$SET_MGR_ATTR,
    /* 0x65 */ XPD_$GET_REGISTERS,
    /* 0x66 */ XPD_$PUT_REGISTERS,
    /* 0x67 */ FILE_$RESERVE,
    /* 0x68 */ SVC_$INVALID_SYSCALL,
    /* 0x69 */ ACL_$GET_RES_SIDS,
    /* 0x6A */ FILE_$FW_PAGES,
    /* 0x6B */ PROC2_$SET_ACCT_INFO,
    /* 0x6C */ FILE_$IMPORT_LK,
    /* 0x6D */ FILE_$UNLOCK_D,
    /* 0x6E */ FILE_$SET_LEN_D,
    /* 0x6F */ FILE_$TRUNCATE_D,
    /* 0x70 */ FILE_$SET_DTM_F,
    /* 0x71 */ TTY_$K_SET_FLAG,
    /* 0x72 */ SVC_$INVALID_SYSCALL,
    /* 0x73 */ MST_$UNMAP_AND_FREE_AREA,
    /* 0x74 */ SVC_$INVALID_SYSCALL,
    /* 0x75 */ PROC2_$NAME_TO_UID,
    /* 0x76 */ SVC_$INVALID_SYSCALL,
    /* 0x77 */ SVC_$INVALID_SYSCALL,
    /* 0x78 */ SVC_$INVALID_SYSCALL,
    /* 0x79 */ SVC_$UNIMPLEMENTED,
    /* 0x7A */ SVC_$UNIMPLEMENTED,
    /* 0x7B */ SVC_$UNIMPLEMENTED,
    /* 0x7C */ SVC_$UNIMPLEMENTED,
    /* 0x7D */ MSG_$SHARE_SOCKET,
    /* 0x7E */ TTY_$K_INQ_DELAY,
    /* 0x7F */ TTY_$K_SET_DELAY,
    /* 0x80 */ MAC_$SEND,
    /* 0x81 */ XNS_IDP_$SEND,
    /* 0x82 */ PROC2_$PGROUP_INFO,
};

/*
 * ============================================================================
 * SVC_$TRAP5_TABLE - Complex syscall handlers (99 entries)
 * ============================================================================
 *
 * TRAP #5 syscalls pass up to 5 arguments via user stack.
 * The dispatcher validates USP and all argument pointers < 0xCC0000.
 *
 * Original address: 0x00e7baf2
 */
void *SVC_$TRAP5_TABLE[SVC_TRAP5_TABLE_SIZE] = {
    /* 0x00 */ SVC_$INVALID_SYSCALL,      /* Reserved */
    /* 0x01 */ MST_$MAP_AREA,
    /* 0x02 */ SVC_$INVALID_SYSCALL,
    /* 0x03 */ SVC_$INVALID_SYSCALL,
    /* 0x04 */ SVC_$INVALID_SYSCALL,
    /* 0x05 */ ACL_$RIGHTS,
    /* 0x06 */ SVC_$INVALID_SYSCALL,
    /* 0x07 */ ASKNODE_$INFO,
    /* 0x08 */ DISK_$AS_READ,
    /* 0x09 */ DISK_$AS_WRITE,
    /* 0x0A */ SVC_$INVALID_SYSCALL,
    /* 0x0B */ SVC_$INVALID_SYSCALL,
    /* 0x0C */ SVC_$INVALID_SYSCALL,
    /* 0x0D */ SVC_$INVALID_SYSCALL,
    /* 0x0E */ SVC_$INVALID_SYSCALL,
    /* 0x0F */ SVC_$INVALID_SYSCALL,
    /* 0x10 */ SVC_$INVALID_SYSCALL,
    /* 0x11 */ SVC_$UNIMPLEMENTED,
    /* 0x12 */ SVC_$UNIMPLEMENTED,
    /* 0x13 */ SVC_$UNIMPLEMENTED,
    /* 0x14 */ SVC_$UNIMPLEMENTED,
    /* 0x15 */ SVC_$UNIMPLEMENTED,
    /* 0x16 */ TPAD_$INQUIRE,
    /* 0x17 */ TPAD_$SET_MODE,
    /* 0x18 */ VFMT_$MAIN,
    /* 0x19 */ VOLX_$GET_INFO,
    /* 0x1A */ VTOC_$GET_UID,
    /* 0x1B */ NETLOG_$CNTL,
    /* 0x1C */ PROC2_$GET_UPIDS,
    /* 0x1D */ SVC_$INVALID_SYSCALL,
    /* 0x1E */ SVC_$INVALID_SYSCALL,
    /* 0x1F */ SVC_$UNIMPLEMENTED,
    /* 0x20 */ SVC_$UNIMPLEMENTED,
    /* 0x21 */ SVC_$UNIMPLEMENTED,
    /* 0x22 */ SVC_$UNIMPLEMENTED,
    /* 0x23 */ MST_$GET_UID_ASID,
    /* 0x24 */ MST_$INVALIDATE,
    /* 0x25 */ FILE_$INVALIDATE,
    /* 0x26 */ SVC_$INVALID_SYSCALL,
    /* 0x27 */ SVC_$INVALID_SYSCALL,
    /* 0x28 */ SVC_$INVALID_SYSCALL,
    /* 0x29 */ MST_$SET_TOUCH_AHEAD_CNT,
    /* 0x2A */ OS_$CHKSUM,
    /* 0x2B */ FILE_$GET_SEG_MAP,
    /* 0x2C */ SVC_$INVALID_SYSCALL,
    /* 0x2D */ SVC_$UNIMPLEMENTED,
    /* 0x2E */ FILE_$UNLOCK_PROC,
    /* 0x2F */ SVC_$UNIMPLEMENTED,
    /* 0x30 */ SVC_$UNIMPLEMENTED,
    /* 0x31 */ DIR_$ADDU,
    /* 0x32 */ DIR_$DROPU,
    /* 0x33 */ DIR_$CREATE_DIRU,
    /* 0x34 */ DIR_$ADD_BAKU,
    /* 0x35 */ SVC_$INVALID_SYSCALL,
    /* 0x36 */ SVC_$INVALID_SYSCALL,
    /* 0x37 */ SVC_$INVALID_SYSCALL,
    /* 0x38 */ DIR_$ADD_HARD_LINKU,
    /* 0x39 */ SVC_$INVALID_SYSCALL,
    /* 0x3A */ RIP_$UPDATE,
    /* 0x3B */ DIR_$DROP_LINKU,
    /* 0x3C */ ACL_$CHECK_RIGHTS,
    /* 0x3D */ DIR_$DROP_HARD_LINKU,
    /* 0x3E */ ROUTE_$OUTGOING,
    /* 0x3F */ SVC_$INVALID_SYSCALL,
    /* 0x40 */ SVC_$UNIMPLEMENTED,
    /* 0x41 */ SVC_$INVALID_SYSCALL,
    /* 0x42 */ NET_$GET_INFO,
    /* 0x43 */ DIR_$GET_ENTRYU,
    /* 0x44 */ AUDIT_$LOG_EVENT,
    /* 0x45 */ FILE_$SET_PROT,
    /* 0x46 */ TTY_$K_GET,
    /* 0x47 */ TTY_$K_PUT,
    /* 0x48 */ PROC2_$ALIGN_CTL,
    /* 0x49 */ SVC_$INVALID_SYSCALL,
    /* 0x4A */ XPD_$READ_PROC,
    /* 0x4B */ XPD_$WRITE_PROC,
    /* 0x4C */ DIR_$SET_DEF_PROTECTION,
    /* 0x4D */ DIR_$GET_DEF_PROTECTION,
    /* 0x4E */ ACL_$COPY,
    /* 0x4F */ ACL_$CONVERT_FUNKY_ACL,
    /* 0x50 */ DIR_$SET_PROTECTION,
    /* 0x51 */ FILE_$OLD_AP,
    /* 0x52 */ ACL_$SET_RE_ALL_SIDS,
    /* 0x53 */ ACL_$GET_RE_ALL_SIDS,
    /* 0x54 */ FILE_$EXPORT_LK,
    /* 0x55 */ FILE_$CHANGE_LOCK_D,
    /* 0x56 */ XPD_$READ_PROC_ASYNC,
    /* 0x57 */ SVC_$INVALID_SYSCALL,
    /* 0x58 */ SMD_$MAP_DISPLAY_MEMORY,
    /* 0x59 */ SVC_$INVALID_SYSCALL,
    /* 0x5A */ SVC_$INVALID_SYSCALL,
    /* 0x5B */ SVC_$INVALID_SYSCALL,
    /* 0x5C */ SVC_$INVALID_SYSCALL,
    /* 0x5D */ SVC_$UNIMPLEMENTED,
    /* 0x5E */ SMD_$UNMAP_DISPLAY_MEMORY,
    /* 0x5F */ SVC_$UNIMPLEMENTED,
    /* 0x60 */ RIP_$TABLE_D,
    /* 0x61 */ XNS_ERROR_$SEND,
    /* 0x62 */ SVC_$UNIMPLEMENTED,
};

/*
 * ============================================================================
 * SVC_$TRAP7_TABLE - 6-argument syscall handlers (59 entries)
 * ============================================================================
 *
 * TRAP #7 syscalls take 6 arguments via user stack at (USP+0x04) through
 * (USP+0x18). The dispatcher validates USP and all six argument pointers
 * < 0xCC0000.
 *
 * Note: TRAP #6 is not used for SVC calls (points to FIM_$UNDEF_TRAP).
 *
 * Original address: 0x00e7bc7e
 */
void *SVC_$TRAP7_TABLE[SVC_TRAP7_TABLE_SIZE] = {
    /* 0x00 */ FILE_$LOCK,
    /* 0x01 */ ERROR_$PRINT,
    /* 0x02 */ MST_$MAP_AREA_AT,
    /* 0x03 */ SVC_$INVALID_SYSCALL,
    /* 0x04 */ SVC_$INVALID_SYSCALL,
    /* 0x05 */ SVC_$INVALID_SYSCALL,
    /* 0x06 */ SVC_$UNIMPLEMENTED,
    /* 0x07 */ SVC_$UNIMPLEMENTED,
    /* 0x08 */ SVC_$UNIMPLEMENTED,
    /* 0x09 */ SVC_$INVALID_SYSCALL,
    /* 0x0A */ SVC_$INVALID_SYSCALL,
    /* 0x0B */ SMD_$WRITE_STRING,
    /* 0x0C */ SVC_$INVALID_SYSCALL,
    /* 0x0D */ VFMT_$FORMATN,
    /* 0x0E */ VFMT_$FORMATN,             /* Same handler as 0x0D */
    /* 0x0F */ STOP_$WATCH,
    /* 0x10 */ SVC_$INVALID_SYSCALL,
    /* 0x11 */ SVC_$INVALID_SYSCALL,
    /* 0x12 */ ASKNODE_$GET_INFO,
    /* 0x13 */ DISK_$DIAG_IO,
    /* 0x14 */ SVC_$INVALID_SYSCALL,
    /* 0x15 */ SVC_$INVALID_SYSCALL,
    /* 0x16 */ SMD_$WRITE_STR_CLIP,
    /* 0x17 */ SVC_$UNIMPLEMENTED,
    /* 0x18 */ TIME_$SET_ITIMER,
    /* 0x19 */ OSINFO_$GET_SEG_TABLE,
    /* 0x1A */ DIR_$CNAMEU,
    /* 0x1B */ DIR_$DELETE_FILEU,
    /* 0x1C */ DIR_$ADD_LINKU,
    /* 0x1D */ SVC_$INVALID_SYSCALL,
    /* 0x1E */ SVC_$INVALID_SYSCALL,
    /* 0x1F */ SVC_$INVALID_SYSCALL,
    /* 0x20 */ SVC_$INVALID_SYSCALL,
    /* 0x21 */ ASKNODE_$WHO_REMOTE,
    /* 0x22 */ MST_$REMAP,
    /* 0x23 */ DIR_$ROOT_ADDU,
    /* 0x24 */ SVC_$UNIMPLEMENTED,
    /* 0x25 */ SVC_$UNIMPLEMENTED,
    /* 0x26 */ SVC_$INVALID_SYSCALL,
    /* 0x27 */ SVC_$UNIMPLEMENTED,
    /* 0x28 */ ASKNODE_$WHO_NOTOPO,
    /* 0x29 */ NET_$OPEN,
    /* 0x2A */ NET_$CLOSE,
    /* 0x2B */ NET_$IOCTL,
    /* 0x2C */ DIR_$FIND_UID,
    /* 0x2D */ FILE_$GET_ATTRIBUTES,
    /* 0x2E */ SVC_$INVALID_SYSCALL,
    /* 0x2F */ PCHIST_$UNIX_PROFIL_CNTL,
    /* 0x30 */ XPD_$RESTART,
    /* 0x31 */ FILE_$GET_ATTR_INFO,
    /* 0x32 */ ACL_$PRIM_CREATE,
    /* 0x33 */ PROC2_$GET_REGS,
    /* 0x34 */ ACL_$CONVERT_TO_9ACL,
    /* 0x35 */ ACL_$SET_RES_ALL_SIDS,
    /* 0x36 */ ACL_$GET_RES_ALL_SIDS,
    /* 0x37 */ FILE_$LOCK_D,
    /* 0x38 */ FILE_$CREATE_IT,
    /* 0x39 */ ACL_$RIGHTS_CHECK,
    /* 0x3A */ RIP_$UPDATE_D,
};

/*
 * ============================================================================
 * SVC_$TRAP8_TABLE - Variable-argument syscall handlers (56 entries)
 * ============================================================================
 *
 * TRAP #8 syscalls use a variable argument count lookup mechanism.
 * The argument count for each syscall is stored in SVC_$TRAP8_ARGCOUNT.
 * This allows syscalls with different argument counts to share one trap.
 *
 * Note: TRAP #6 is not used for SVC calls (points to FIM_$UNDEF_TRAP).
 *
 * Original address: 0x00e7bd6a
 */
void *SVC_$TRAP8_TABLE[SVC_TRAP8_TABLE_SIZE] = {
    /* 0x00 */ MST_$MAP,                    /* 7 args */
    /* 0x01 */ MST_$MAP_AT,                 /* 8 args */
    /* 0x02 */ MST_$MAP_GLOBAL,             /* 7 args */
    /* 0x03 */ SVC_$INVALID_SYSCALL,        /* 10 args (invalid) */
    /* 0x04 */ VOLX_$DISMOUNT,              /* 6 args */
    /* 0x05 */ VOLX_$GET_UIDS,              /* 6 args */
    /* 0x06 */ SVC_$INVALID_SYSCALL,        /* 7 args (invalid) */
    /* 0x07 */ DISK_$PV_ASSIGN,             /* 7 args */
    /* 0x08 */ MSG_$RCV,                    /* 11 args */
    /* 0x09 */ MSG_$SAR,                    /* 16 args */
    /* 0x0A */ MSG_$SEND,                   /* 10 args */
    /* 0x0B */ SVC_$INVALID_SYSCALL,        /* 6 args (invalid) */
    /* 0x0C */ SMD_$LOAD_CRSR_BITMAP,       /* 7 args */
    /* 0x0D */ SMD_$READ_CRSR_BITMAP,       /* 7 args */
    /* 0x0E */ SVC_$INVALID_SYSCALL,        /* 13 args (invalid) */
    /* 0x0F */ SVC_$INVALID_SYSCALL,        /* 8 args (invalid) */
    /* 0x10 */ SVC_$INVALID_SYSCALL,        /* 13 args (invalid) */
    /* 0x11 */ SVC_$INVALID_SYSCALL,        /* 8 args (invalid) */
    /* 0x12 */ OSINFO_$GET_MMAP,            /* 6 args */
    /* 0x13 */ ASKNODE_$INTERNET_INFO,      /* 8 args */
    /* 0x14 */ MST_$GET_VA_INFO,            /* 6 args */
    /* 0x15 */ SVC_$INVALID_SYSCALL,        /* 6 args (invalid) */
    /* 0x16 */ MSG_$SENDI,                  /* 7 args */
    /* 0x17 */ SVC_$INVALID_SYSCALL,        /* 13 args (invalid) */
    /* 0x18 */ MSG_$RCVI,                   /* 15 args */
    /* 0x19 */ MSG_$RCV_CONTIGI,            /* 12 args */
    /* 0x1A */ MSG_$SARI,                   /* 17 args */
    /* 0x1B */ MST_$GET_VA_INFO,            /* 6 args (duplicate?) */
    /* 0x1C */ SVC_$UNIMPLEMENTED,          /* 14 args */
    /* 0x1D */ MSG_$SEND_HW,                /* 7 args */
    /* 0x1E */ SVC_$INVALID_SYSCALL,        /* 6 args (invalid) */
    /* 0x1F */ MST_$MAP_TOP,                /* 8 args */
    /* 0x20 */ NET_$SEND,                   /* 7 args */
    /* 0x21 */ NET_$RCV,                    /* 8 args */
    /* 0x22 */ DIR_$DIR_READU,              /* 8 args */
    /* 0x23 */ DIR_$READ_LINKU,             /* 8 args */
    /* 0x24 */ SVC_$UNIMPLEMENTED,          /* 7 args */
    /* 0x25 */ SVC_$UNIMPLEMENTED,          /* 7 args */
    /* 0x26 */ PROC2_$COMPLETE_VFORK,       /* 9 args */
    /* 0x27 */ DIR_$RESOLVE,                /* 9 args */
    /* 0x28 */ SVC_$INVALID_SYSCALL,        /* 7 args (invalid) */
    /* 0x29 */ SVC_$UNIMPLEMENTED,          /* 7 args */
    /* 0x2A */ VOLX_$MOUNT,                 /* 6 args */
    /* 0x2B */ ACL_$IMAGE,                  /* 10 args */
    /* 0x2C */ DISK_$PV_ASSIGN_N,           /* 8 args */
    /* 0x2D */ DISK_$AS_XFER_MULTI,         /* 6 args */
    /* 0x2E */ SVC_$UNIMPLEMENTED,          /* 9 args */
    /* 0x2F */ PROC2_$FORK,                 /* 7 args */
    /* 0x30 */ SVC_$UNIMPLEMENTED,          /* 7 args */
    /* 0x31 */ PROC2_$CREATE,               /* 6 args */
    /* 0x32 */ SVC_$UNIMPLEMENTED,          /* 7 args */
    /* 0x33 */ SVC_$UNIMPLEMENTED,          /* 10 args */
    /* 0x34 */ TPAD_$SET_UNIT_MODE,         /* 6 args */
    /* 0x35 */ TPAD_$INQUIRE_UNIT,          /* 6 args */
    /* 0x36 */ SVC_$UNIMPLEMENTED,          /* 11 args */
    /* 0x37 */ MSG_$RCV_HW,                 /* 13 args */
};

/*
 * ============================================================================
 * SVC_$TRAP8_ARGCOUNT - Argument count table for TRAP #8 (56 entries)
 * ============================================================================
 *
 * Each byte contains the number of 4-byte arguments (in longwords) that
 * the corresponding syscall expects. The dispatcher uses this to determine
 * how many arguments to copy and validate from the user stack.
 *
 * Negative values indicate syscalls that handle their own argument validation.
 *
 * Original address: 0x00e7be4a
 */
unsigned char SVC_$TRAP8_ARGCOUNT[SVC_TRAP8_TABLE_SIZE] = {
    /* 0x00 */ 0x07,    /* MST_$MAP: 7 args */
    /* 0x01 */ 0x08,    /* MST_$MAP_AT: 8 args */
    /* 0x02 */ 0x07,    /* MST_$MAP_GLOBAL: 7 args */
    /* 0x03 */ 0x0a,    /* Invalid: 10 args */
    /* 0x04 */ 0x06,    /* VOLX_$DISMOUNT: 6 args */
    /* 0x05 */ 0x06,    /* VOLX_$GET_UIDS: 6 args */
    /* 0x06 */ 0x07,    /* Invalid: 7 args */
    /* 0x07 */ 0x07,    /* DISK_$PV_ASSIGN: 7 args */
    /* 0x08 */ 0x0b,    /* MSG_$RCV: 11 args */
    /* 0x09 */ 0x10,    /* MSG_$SAR: 16 args */
    /* 0x0A */ 0x0a,    /* MSG_$SEND: 10 args */
    /* 0x0B */ 0x06,    /* Invalid: 6 args */
    /* 0x0C */ 0x07,    /* SMD_$LOAD_CRSR_BITMAP: 7 args */
    /* 0x0D */ 0x07,    /* SMD_$READ_CRSR_BITMAP: 7 args */
    /* 0x0E */ 0x0d,    /* Invalid: 13 args */
    /* 0x0F */ 0x08,    /* Invalid: 8 args */
    /* 0x10 */ 0x0d,    /* Invalid: 13 args */
    /* 0x11 */ 0x08,    /* Invalid: 8 args */
    /* 0x12 */ 0x06,    /* OSINFO_$GET_MMAP: 6 args */
    /* 0x13 */ 0x08,    /* ASKNODE_$INTERNET_INFO: 8 args */
    /* 0x14 */ 0x06,    /* MST_$GET_VA_INFO: 6 args */
    /* 0x15 */ 0x06,    /* Invalid: 6 args */
    /* 0x16 */ 0x07,    /* MSG_$SENDI: 7 args */
    /* 0x17 */ 0x0d,    /* Invalid: 13 args */
    /* 0x18 */ 0x0f,    /* MSG_$RCVI: 15 args */
    /* 0x19 */ 0x0c,    /* MSG_$RCV_CONTIGI: 12 args */
    /* 0x1A */ 0x11,    /* MSG_$SARI: 17 args */
    /* 0x1B */ 0x06,    /* MST_$GET_VA_INFO: 6 args */
    /* 0x1C */ 0x0e,    /* Unimplemented: 14 args */
    /* 0x1D */ 0x07,    /* MSG_$SEND_HW: 7 args */
    /* 0x1E */ 0x06,    /* Invalid: 6 args */
    /* 0x1F */ 0x08,    /* MST_$MAP_TOP: 8 args */
    /* 0x20 */ 0x07,    /* NET_$SEND: 7 args */
    /* 0x21 */ 0x08,    /* NET_$RCV: 8 args */
    /* 0x22 */ 0x08,    /* DIR_$DIR_READU: 8 args */
    /* 0x23 */ 0x08,    /* DIR_$READ_LINKU: 8 args */
    /* 0x24 */ 0x07,    /* Unimplemented: 7 args */
    /* 0x25 */ 0x07,    /* Unimplemented: 7 args */
    /* 0x26 */ 0x09,    /* PROC2_$COMPLETE_VFORK: 9 args */
    /* 0x27 */ 0x09,    /* DIR_$RESOLVE: 9 args */
    /* 0x28 */ 0x07,    /* Invalid: 7 args */
    /* 0x29 */ 0x07,    /* Unimplemented: 7 args */
    /* 0x2A */ 0x06,    /* VOLX_$MOUNT: 6 args */
    /* 0x2B */ 0x0a,    /* ACL_$IMAGE: 10 args */
    /* 0x2C */ 0x08,    /* DISK_$PV_ASSIGN_N: 8 args */
    /* 0x2D */ 0x06,    /* DISK_$AS_XFER_MULTI: 6 args */
    /* 0x2E */ 0x09,    /* Unimplemented: 9 args */
    /* 0x2F */ 0x07,    /* PROC2_$FORK: 7 args */
    /* 0x30 */ 0x07,    /* Unimplemented: 7 args */
    /* 0x31 */ 0x06,    /* PROC2_$CREATE: 6 args */
    /* 0x32 */ 0x07,    /* Unimplemented: 7 args */
    /* 0x33 */ 0x0a,    /* Unimplemented: 10 args */
    /* 0x34 */ 0x06,    /* TPAD_$SET_UNIT_MODE: 6 args */
    /* 0x35 */ 0x06,    /* TPAD_$INQUIRE_UNIT: 6 args */
    /* 0x36 */ 0x0b,    /* Unimplemented: 11 args */
    /* 0x37 */ 0x0d,    /* MSG_$RCV_HW: 13 args */
};
