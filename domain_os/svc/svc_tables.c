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
#include "asknode/asknode.h"
#include "audit/audit.h"
#include "cache/cache.h"
#include "disk/disk.h"
#include "dtty/dtty.h"
#include "file/file.h"
#include "netlog/netlog.h"
#include "os/os.h"
#include "smd/smd.h"
#include "tty/tty.h"
#include "vtoc/vtoc.h"

/*
 * Forward declarations for syscall handlers not yet in subsystem headers.
 * As headers are created for these subsystems, move includes above and
 * remove the corresponding extern declarations here.
 */

/* Error handlers (in svc/sau2/*.s) */
extern void SVC_$INVALID_SYSCALL(void);
extern void SVC_$UNIMPLEMENTED(void);

/* TRAP #0 handlers not yet in headers */
extern void PROC2_$DELETE(void);
extern void FUN_00e0aa04(void);          /* TODO: identify - returns FIM addr */
extern void FILE_$UNLOCK_ALL(void);
extern void PEB_$ASSOC(void);
extern void PEB_$DISSOC(void);
extern void PROC2_$MY_PID(void);
extern void SMD_$OP_WAIT_U(void);
extern void TPAD_$RE_RANGE(void);
extern void TPAD_$INQ_DTYPE(void);
extern void RIP_$ANNOUNCE_NS(void);
extern void PROC2_$DELIVER_PENDING(void);
extern void PROC2_$COMPLETE_FORK(void);
extern void PACCT_$STOP(void);
extern void PACCT_$ON(void);

/* TRAP #1 handlers not yet in headers */
extern void FUN_00e0a9c2(void);          /* TODO: identify - sets FIM user addr */
extern void NETWORK_$READ_SERVICE(void);
extern void PROC1_$GET_CPUT(void);
extern void SET_LITES_LOC(void);
extern void TIME_$CLOCK(void);
extern void ASKNODE_$READ_FAILURE_REC(void);
extern void CAL_$APPLY_LOCAL_OFFSET(void);
extern void CAL_$GET_INFO(void);
extern void CAL_$GET_LOCAL_TIME(void);
extern void CAL_$REMOVE_LOCAL_OFFSET(void);
extern void CAL_$SET_DRIFT(void);
extern void DISK_$GET_ERROR_INFO(void);
extern void MSG_$CLOSE(void);
extern void PROC2_$WHO_AM_I(void);
extern void SMD_$CLEAR_KBD_CURSOR(void);
extern void SMD_$SEND_RESPONSE(void);
extern void SMD_$STOP_TP_CURSOR(void);
extern void SMD_$UNMAP_DISPLAY_U(void);
extern void UID_$GEN(void);
extern void TONE_$TIME(void);
extern void SMD_$INQ_DISP_TYPE(void);
extern void SMD_$INVERT_S(void);
extern void SMD_$INQ_MM_BLT(void);
extern void TPAD_$SET_CURSOR(void);
extern void SMD_$EOF_WAIT(void);
extern void NAME_$GET_WDIR_UID(void);
extern void NAME_$GET_NDIR_UID(void);
extern void NAME_$GET_ROOT_UID(void);
extern void NAME_$GET_NODE_UID(void);
extern void NAME_$GET_NODE_DATA_UID(void);
extern void NAME_$GET_CANNED_ROOT_UID(void);
extern void MSG_$GET_MY_NET(void);
extern void MSG_$GET_MY_NODE(void);
extern void GPU_$INIT(void);
extern void SMD_$INIT_STATE(void);
extern void SMD_$CLR_TRK_RECT(void);
extern void PROC2_$GET_SIG_MASK(void);
extern void FIM_$FRESTORE(void);
extern void TIME_$GET_TIME_OF_DAY(void);
extern void PROC1_$GET_LOADAV(void);
extern void PROC2_$GET_BOOT_FLAGS(void);
extern void PROC2_$SET_TTY(void);
extern void OS_$SHUTDOWN(void);
extern void PBU_$FAULTED_UNITS(void);
extern void PROC2_$GET_CPU_USAGE(void);
extern void TIME_$GET_ADJUST(void);

/* TRAP #2 handlers not yet in headers */
extern void FILE_$DELETE(void);
extern void EC2_$WAKEUP(void);
extern void FILE_$MK_PERMANENT(void);
extern void FILE_$UNLOCK_VOL(void);
extern void CAL_$READ_TIMEZONE(void);
extern void CAL_$SEC_TO_CLOCK(void);
extern void CAL_$WRITE_TIMEZONE(void);
extern void DISK_$UNASSIGN(void);
extern void FILE_$FORCE_UNLOCK(void);
extern void FILE_$FW_FILE(void);
extern void FILE_$MK_IMMUTABLE(void);
extern void FILE_$PURIFY(void);
extern void GET_BUILD_TIME(void);
extern void MSG_$ALLOCATE(void);
extern void MSG_$OPEN(void);
extern void MSG_$WAIT(void);
extern void PROC2_$QUIT(void);
extern void PROC2_$RESUME(void);
extern void PROC2_$SUSPEND(void);
extern void SMD_$BLT_U(void);
extern void SMD_$CLEAR_WINDOW(void);
extern void SMD_$DRAW_BOX(void);
extern void SMD_$LOAD_FONT(void);
extern void SMD_$MAP_DISPLAY_U(void);
extern void SMD_$MOVE_KBD_CURSOR(void);
extern void SMD_$RETURN_DISPLAY(void);
extern void SMD_$UNLOAD_FONT(void);
extern void PEB_$GET_INFO(void);
extern void EC2_$GET_VAL(void);
extern void AST_$ADD_ASTES(void);
extern void PROC2_$MAKE_ORPHAN(void);
extern void FILE_$DELETE_FORCE(void);
extern void FILE_$DELETE_WHEN_UNLOCKED(void);
extern void FILE_$MK_TEMPORARY(void);
extern void SMD_$INQ_KBD_CURSOR(void);
extern void ACL_$ENTER_SUBS(void);
extern void FILE_$DELETE_FORCE_WHEN_UNLOCKED(void);
extern void SMD_$SET_CLIP_WINDOW(void);
extern void HINT_$ADD(void);
extern void DIR_$FIX_DIR(void);
extern void NAME_$SET_WDIRUS(void);
extern void NAME_$SET_NDIRUS(void);
extern void MSG_$CLOSEI(void);
extern void NETWORK_$ADD_PAGE_SERVERS(void);
extern void NETWORK_$ADD_REQUEST_SERVERS(void);
extern void ACL_$ADD_PROJ(void);
extern void ACL_$DELETE_PROJ(void);
extern void XPD_$GET_FP(void);
extern void XPD_$PUT_FP(void);
extern void SMD_$SET_TP_REPORTING(void);
extern void SMD_$DISABLE_TRACKING(void);
extern void HINT_$ADDI(void);
extern void SMD_$SET_DISP_UNIT(void);
extern void SMD_$VIDEO_CTL(void);
extern void SMD_$SET_CURSOR_POS(void);
extern void TERM_$SEND_KBD_STRING(void);
extern void AUDIT_$CONTROL(void);
extern void PROC2_$SIGBLOCK(void);
extern void PROC2_$SIGSETMASK(void);
extern void PROC2_$SIGPAUSE(void);
extern void AS_$GET_ADDR(void);
extern void PROC2_$GET_ASID(void);
extern void TTY_$K_FLUSH_INPUT(void);
extern void TTY_$K_FLUSH_OUTPUT(void);
extern void TTY_$K_DRAIN_OUTPUT(void);
extern void PROC2_$DEBUG(void);
extern void PROC2_$UNDEBUG(void);
extern void ACL_$DEF_ACLDATA(void);
extern void PROC2_$OVERRIDE_DEBUG(void);
extern void TIME_$SET_TIME_OF_DAY(void);
extern void CAL_$DECODE_TIME(void);
extern void ACL_$INHERIT_SUBSYS(void);
extern void ACL_$SET_LOCAL_LOCKSMITH(void);
extern void SMD_$DISSOC(void);
extern void SMD_$BUSY_WAIT(void);
extern void TTY_$K_RESET(void);
extern void TPAD_$RE_RANGE_UNIT(void);
extern void DISK_$FORMAT_WHOLE(void);
extern void MAC_$CLOSE(void);
extern void MAC_$NET_TO_PORT_NUM(void);
extern void XNS_IDP_$OPEN(void);
extern void XNS_IDP_$CLOSE(void);
extern void XNS_IDP_$GET_STATS(void);

/* TRAP #5 handlers not yet in headers */
extern void MST_$MAP_AREA(void);
extern void TPAD_$INQUIRE(void);
extern void TPAD_$SET_MODE(void);
extern void VFMT_$MAIN(void);
extern void VOLX_$GET_INFO(void);
extern void PROC2_$GET_UPIDS(void);
extern void MST_$GET_UID_ASID(void);
extern void MST_$INVALIDATE(void);
extern void FILE_$INVALIDATE(void);
extern void MST_$SET_TOUCH_AHEAD_CNT(void);
extern void FILE_$GET_SEG_MAP(void);
extern void DIR_$ADDU(void);
extern void DIR_$DROPU(void);
extern void DIR_$CREATE_DIRU(void);
extern void DIR_$ADD_BAKU(void);
extern void DIR_$ADD_HARD_LINKU(void);
extern void RIP_$UPDATE(void);
extern void DIR_$DROP_LINKU(void);
extern void DIR_$DROP_HARD_LINKU(void);
extern void ROUTE_$OUTGOING(void);
extern void NET_$GET_INFO(void);
extern void DIR_$GET_ENTRYU(void);
extern void PROC2_$ALIGN_CTL(void);
extern void XPD_$READ_PROC(void);
extern void XPD_$WRITE_PROC(void);
extern void DIR_$SET_DEF_PROTECTION(void);
extern void DIR_$GET_DEF_PROTECTION(void);
extern void ACL_$COPY(void);
extern void DIR_$SET_PROTECTION(void);
extern void ACL_$SET_RE_ALL_SIDS(void);
extern void FILE_$EXPORT_LK(void);
extern void XPD_$READ_PROC_ASYNC(void);
extern void RIP_$TABLE_D(void);
extern void XNS_ERROR_$SEND(void);

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
