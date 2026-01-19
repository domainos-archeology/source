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
