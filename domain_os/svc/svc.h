/*
 * svc/svc.h - System Call (SVC) Subsystem Public API
 *
 * The SVC subsystem implements the Domain/OS system call interface.
 * User programs invoke system calls via the M68K TRAP #5 instruction
 * with the syscall number in D0.w and arguments on the user stack.
 *
 * System Call Convention:
 *   - TRAP #5 instruction triggers supervisor mode
 *   - D0.w contains syscall number (0-98, 0x00-0x62)
 *   - Arguments passed on user stack (up to 5 longwords)
 *   - Handler address looked up from SVC_$TRAP5_TABLE
 *   - Arguments copied from USP to supervisor stack
 *   - Handler called, then RTE back to user mode
 *
 * Address Space Protection:
 *   - User stack pointer (USP) must be < 0xCC0000
 *   - All argument pointers must be < 0xCC0000
 *   - Violation triggers protection boundary fault
 *
 * Original addresses:
 *   - SVC_$TRAP5: 0x00e7b17c (syscall dispatcher)
 *   - SVC_$TRAP5_TABLE: 0x00e7baf2 (syscall handler table)
 */

#ifndef SVC_H
#define SVC_H

#include "base/base.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum syscall number (inclusive) */
#define SVC_MAX_SYSCALL         0x62    /* 98 decimal */

/* Number of syscall table entries */
#define SVC_TABLE_SIZE          99

/* User/kernel address space boundary */
#define SVC_USER_SPACE_LIMIT    0xCC0000

/* Maximum number of syscall arguments */
#define SVC_MAX_ARGS            5

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */

/* Invalid syscall number */
#define status_$fault_invalid_SVC_code              0x00120007

/* Protection boundary violation (user pointer in kernel space) */
#define status_$fault_protection_boundary_violation 0x0012000b

/* Unimplemented syscall */
#define status_$fault_unimplemented_SVC             0x0012001c

/*
 * ============================================================================
 * Syscall Number Definitions
 * ============================================================================
 *
 * Known syscall numbers and their handlers:
 */

/* Syscall 0: Reserved/Invalid */
#define SVC_RESERVED_0              0x00

/* Syscall 1: MST_$MAP_AREA - Map memory area */
#define SVC_MST_MAP_AREA            0x01

/* Syscall 5: ACL_$RIGHTS - Get ACL rights */
#define SVC_ACL_RIGHTS              0x05

/* Syscall 7: ASKNODE_$INFO - Query node information */
#define SVC_ASKNODE_INFO            0x07

/* Syscall 8: DISK_$AS_READ - Asynchronous disk read */
#define SVC_DISK_AS_READ            0x08

/* Syscall 9: DISK_$AS_WRITE - Asynchronous disk write */
#define SVC_DISK_AS_WRITE           0x09

/* Syscall 22 (0x16): TPAD_$INQUIRE - Query tablet pad mode */
#define SVC_TPAD_INQUIRE            0x16

/* Syscall 23 (0x17): TPAD_$SET_MODE - Set tablet pad mode */
#define SVC_TPAD_SET_MODE           0x17

/* Syscall 24 (0x18): VFMT_$MAIN - Volume format */
#define SVC_VFMT_MAIN               0x18

/* Syscall 25 (0x19): VOLX_$GET_INFO - Get volume info */
#define SVC_VOLX_GET_INFO           0x19

/* Syscall 26 (0x1a): VTOC_$GET_UID - Get VTOC UID */
#define SVC_VTOC_GET_UID            0x1a

/* Syscall 27 (0x1b): NETLOG_$CNTL - Network logging control */
#define SVC_NETLOG_CNTL             0x1b

/* Syscall 28 (0x1c): PROC2_$GET_UPIDS - Get user PIDs */
#define SVC_PROC2_GET_UPIDS         0x1c

/* Syscall 35 (0x23): MST_$GET_UID_ASID - Get UID/ASID */
#define SVC_MST_GET_UID_ASID        0x23

/* Syscall 36 (0x24): MST_$INVALIDATE - Invalidate MST entry */
#define SVC_MST_INVALIDATE          0x24

/* Syscall 37 (0x25): FILE_$INVALIDATE - Invalidate file cache */
#define SVC_FILE_INVALIDATE         0x25

/* Syscall 41 (0x29): MST_$SET_TOUCH_AHEAD_CNT */
#define SVC_MST_SET_TOUCH_AHEAD     0x29

/* Syscall 42 (0x2a): OS_$CHKSUM - Checksum calculation */
#define SVC_OS_CHKSUM               0x2a

/* Syscall 43 (0x2b): FILE_$GET_SEG_MAP */
#define SVC_FILE_GET_SEG_MAP        0x2b

/* Syscall 46 (0x2e): FILE_$UNLOCK_PROC */
#define SVC_FILE_UNLOCK_PROC        0x2e

/* Syscall 49 (0x31): DIR_$ADDU - Add directory entry (user) */
#define SVC_DIR_ADDU                0x31

/* Syscall 50 (0x32): DIR_$DROPU - Drop directory entry (user) */
#define SVC_DIR_DROPU               0x32

/* Syscall 51 (0x33): DIR_$CREATE_DIRU - Create directory (user) */
#define SVC_DIR_CREATE_DIRU         0x33

/* Syscall 52 (0x34): DIR_$ADD_BAKU - Add backup entry (user) */
#define SVC_DIR_ADD_BAKU            0x34

/* Syscall 56 (0x38): DIR_$ADD_HARD_LINKU */
#define SVC_DIR_ADD_HARD_LINKU      0x38

/* Syscall 58 (0x3a): RIP_$UPDATE - RIP update */
#define SVC_RIP_UPDATE              0x3a

/* Syscall 59 (0x3b): DIR_$DROP_LINKU */
#define SVC_DIR_DROP_LINKU          0x3b

/* Syscall 60 (0x3c): ACL_$CHECK_RIGHTS */
#define SVC_ACL_CHECK_RIGHTS        0x3c

/* Syscall 61 (0x3d): DIR_$DROP_HARD_LINKU */
#define SVC_DIR_DROP_HARD_LINKU     0x3d

/* Syscall 62 (0x3e): ROUTE_$OUTGOING */
#define SVC_ROUTE_OUTGOING          0x3e

/* Syscall 66 (0x42): NET_$GET_INFO */
#define SVC_NET_GET_INFO            0x42

/* Syscall 67 (0x43): DIR_$GET_ENTRYU */
#define SVC_DIR_GET_ENTRYU          0x43

/* Syscall 68 (0x44): AUDIT_$LOG_EVENT */
#define SVC_AUDIT_LOG_EVENT         0x44

/* Syscall 69 (0x45): FILE_$SET_PROT */
#define SVC_FILE_SET_PROT           0x45

/* Syscall 70 (0x46): TTY_$K_GET - TTY kernel get */
#define SVC_TTY_K_GET               0x46

/* Syscall 71 (0x47): TTY_$K_PUT - TTY kernel put */
#define SVC_TTY_K_PUT               0x47

/* Syscall 72 (0x48): PROC2_$ALIGN_CTL */
#define SVC_PROC2_ALIGN_CTL         0x48

/* Syscall 74 (0x4a): XPD_$READ_PROC */
#define SVC_XPD_READ_PROC           0x4a

/* Syscall 75 (0x4b): XPD_$WRITE_PROC */
#define SVC_XPD_WRITE_PROC          0x4b

/* Syscall 76 (0x4c): DIR_$SET_DEF_PROTECTION */
#define SVC_DIR_SET_DEF_PROT        0x4c

/* Syscall 77 (0x4d): DIR_$GET_DEF_PROTECTION */
#define SVC_DIR_GET_DEF_PROT        0x4d

/* Syscall 78 (0x4e): ACL_$COPY */
#define SVC_ACL_COPY                0x4e

/* Syscall 79 (0x4f): ACL_$CONVERT_FUNKY_ACL */
#define SVC_ACL_CONVERT_FUNKY       0x4f

/* Syscall 80 (0x50): DIR_$SET_PROTECTION */
#define SVC_DIR_SET_PROTECTION      0x50

/* Syscall 81 (0x51): FILE_$OLD_AP */
#define SVC_FILE_OLD_AP             0x51

/* Syscall 82 (0x52): ACL_$SET_RE_ALL_SIDS */
#define SVC_ACL_SET_RE_ALL_SIDS     0x52

/* Syscall 83 (0x53): ACL_$GET_RE_ALL_SIDS */
#define SVC_ACL_GET_RE_ALL_SIDS     0x53

/* Syscall 84 (0x54): FILE_$EXPORT_LK */
#define SVC_FILE_EXPORT_LK          0x54

/* Syscall 85 (0x55): FILE_$CHANGE_LOCK_D */
#define SVC_FILE_CHANGE_LOCK_D      0x55

/* Syscall 86 (0x56): XPD_$READ_PROC_ASYNC */
#define SVC_XPD_READ_PROC_ASYNC     0x56

/* Syscall 88 (0x58): SMD_$MAP_DISPLAY_MEMORY */
#define SVC_SMD_MAP_DISPLAY         0x58

/* Syscall 94 (0x5e): SMD_$UNMAP_DISPLAY_MEMORY */
#define SVC_SMD_UNMAP_DISPLAY       0x5e

/* Syscall 96 (0x60): RIP_$TABLE_D */
#define SVC_RIP_TABLE_D             0x60

/* Syscall 97 (0x61): XNS_ERROR_$SEND */
#define SVC_XNS_ERROR_SEND          0x61

/*
 * ============================================================================
 * External References
 * ============================================================================
 */

/*
 * SVC_$TRAP5_TABLE - Syscall handler dispatch table
 *
 * Array of 99 function pointers indexed by syscall number.
 * Invalid/unimplemented syscalls point to error handlers.
 *
 * Original address: 0x00e7baf2
 */
extern void (*SVC_$TRAP5_TABLE[SVC_TABLE_SIZE])(void);

/*
 * ============================================================================
 * Entry Points (Assembly)
 * ============================================================================
 * These are assembly language routines, not C functions.
 */

/*
 * SVC_$TRAP5 - Main TRAP #5 syscall dispatcher
 *
 * Entry point for all system calls. Validates syscall number,
 * checks user stack pointer and arguments, then dispatches
 * to the appropriate handler.
 *
 * Original address: 0x00e7b17c
 */

/*
 * SVC_$INVALID_SYSCALL - Invalid syscall number handler
 *
 * Called when D0.w >= 0x63. Generates fault with
 * status_$fault_invalid_SVC_code.
 *
 * Original address: 0x00e7b28e
 */

/*
 * SVC_$BAD_USER_PTR - Bad user pointer handler
 *
 * Called when a syscall argument pointer >= 0xCC0000.
 * Generates fault with status_$fault_protection_boundary_violation.
 *
 * Original address: 0x00e7b2a0
 */

/*
 * SVC_$UNIMPLEMENTED - Unimplemented syscall handler
 *
 * Called for syscalls that are not yet implemented.
 * Generates fault with status_$fault_unimplemented_SVC.
 *
 * Original address: 0x00e7b2cc
 */

#endif /* SVC_H */
