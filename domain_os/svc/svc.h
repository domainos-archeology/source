/*
 * svc/svc.h - System Call (SVC) Subsystem Public API
 *
 * The SVC subsystem implements the Domain/OS system call interface.
 * Multiple TRAP instructions are used for different syscall categories:
 *
 * TRAP #0 - Simple syscalls (0-31):
 *   - No argument validation or copying
 *   - Handler called directly
 *   - Used for no-arg calls like PROC2_$MY_PID, CACHE_$CLEAR
 *
 * TRAP #1 - Medium syscalls (0-65):
 *   - Validates USP and 1 argument
 *   - Arguments copied from user stack
 *
 * TRAP #2 - Extended syscalls (0-132):
 *   - Validates USP and 2 arguments
 *   - Arguments copied from user stack
 *
 * TRAP #3 - Full syscalls (0-154):
 *   - Validates USP and 3 arguments
 *
 * TRAP #4 - Extended syscalls (0-130):
 *   - Validates USP and 4 arguments
 *
 * TRAP #5 - Complex syscalls (0-98):
 *   - Full validation of USP and 5 arguments
 *   - Most comprehensive protection
 *
 * TRAP #6 - Extended syscalls (0-58):
 *   - Validates USP and 6 arguments
 *
 * TRAP #7 - Variable-argument syscalls (0-55):
 *   - Uses lookup table for argument count per syscall
 *   - Validates USP and variable number of arguments (6-17)
 *   - Creates stack frame with LINK instruction
 *
 * Address Space Protection (TRAP 1-7):
 *   - User stack pointer (USP) must be < 0xCC0000
 *   - All argument pointers must be < 0xCC0000
 *   - Violation triggers protection boundary fault
 *
 * Original addresses:
 *   - SVC_$TRAP0: 0x00e7b044 (simple dispatcher, 32 entries)
 *   - SVC_$TRAP0_TABLE: 0x00e7b2de
 *   - SVC_$TRAP1: 0x00e7b05c (1-arg dispatcher, 66 entries)
 *   - SVC_$TRAP1_TABLE: 0x00e7b360
 *   - SVC_$TRAP2: 0x00e7b094 (2-arg dispatcher, 133 entries)
 *   - SVC_$TRAP2_TABLE: 0x00e7b466
 *   - SVC_$TRAP3: 0x00e7b0d8 (3-arg dispatcher, 155 entries)
 *   - SVC_$TRAP3_TABLE: 0x00e7b67a
 *   - SVC_$TRAP4: 0x00e7b120 (4-arg dispatcher, 131 entries)
 *   - SVC_$TRAP4_TABLE: 0x00e7b8e6
 *   - SVC_$TRAP5: 0x00e7b17c (5-arg dispatcher, 99 entries)
 *   - SVC_$TRAP5_TABLE: 0x00e7baf2
 *   - SVC_$TRAP6: 0x00e7b1d8 (6-arg dispatcher, 59 entries)
 *   - SVC_$TRAP6_TABLE: 0x00e7bc7e
 *   - SVC_$TRAP7: 0x00e7b240 (variable-arg dispatcher, 56 entries)
 *   - SVC_$TRAP7_TABLE: 0x00e7bd6a
 *   - SVC_$TRAP7_ARGCOUNT: 0x00e7be4a
 */

#ifndef SVC_H
#define SVC_H

#include "base/base.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* TRAP #0 constants */
#define SVC_TRAP0_MAX_SYSCALL   0x1F    /* 31 decimal */
#define SVC_TRAP0_TABLE_SIZE    32

/* TRAP #1 constants */
#define SVC_TRAP1_MAX_SYSCALL   0x41    /* 65 decimal */
#define SVC_TRAP1_TABLE_SIZE    66

/* TRAP #2 constants */
#define SVC_TRAP2_MAX_SYSCALL   0x84    /* 132 decimal */
#define SVC_TRAP2_TABLE_SIZE    133

/* TRAP #3 constants */
#define SVC_TRAP3_MAX_SYSCALL   0x9A    /* 154 decimal */
#define SVC_TRAP3_TABLE_SIZE    155

/* TRAP #4 constants */
#define SVC_TRAP4_MAX_SYSCALL   0x82    /* 130 decimal */
#define SVC_TRAP4_TABLE_SIZE    131

/* TRAP #5 constants */
#define SVC_TRAP5_MAX_SYSCALL   0x62    /* 98 decimal */
#define SVC_TRAP5_TABLE_SIZE    99

/* TRAP #6 constants */
#define SVC_TRAP6_MAX_SYSCALL   0x3A    /* 58 decimal */
#define SVC_TRAP6_TABLE_SIZE    59

/* TRAP #7 constants */
#define SVC_TRAP7_MAX_SYSCALL   0x37    /* 55 decimal */
#define SVC_TRAP7_TABLE_SIZE    56

/* Legacy aliases */
#define SVC_MAX_SYSCALL         SVC_TRAP5_MAX_SYSCALL
#define SVC_TABLE_SIZE          SVC_TRAP5_TABLE_SIZE

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
 * TRAP #0 Syscall Numbers (Simple, no-arg syscalls)
 * ============================================================================
 */

#define SVC0_PROC2_DELETE           0x00    /* Delete process */
#define SVC0_GET_FIM_ADDR           0x01    /* Get FIM address (FUN_00e0aa04) */
/* 0x02: Invalid */
#define SVC0_DTTY_RELOAD_FONT       0x03    /* Reload display font */
#define SVC0_FILE_UNLOCK_ALL        0x04    /* Unlock all files */
#define SVC0_PEB_ASSOC              0x05    /* Associate PEB */
#define SVC0_PEB_DISSOC             0x06    /* Dissociate PEB */
#define SVC0_PROC2_MY_PID           0x07    /* Get my PID */
#define SVC0_SMD_OP_WAIT_U          0x08    /* SMD operation wait (user) */
#define SVC0_TPAD_RE_RANGE          0x09    /* Tablet pad re-range */
/* 0x0A: Invalid */
/* 0x0B: Unimplemented */
/* 0x0C: Invalid */
#define SVC0_ACL_UP                 0x0D    /* ACL up (increase privilege) */
#define SVC0_ACL_DOWN               0x0E    /* ACL down (decrease privilege) */
/* 0x0F: Unimplemented */
#define SVC0_TPAD_INQ_DTYPE         0x10    /* Inquire tablet device type */
/* 0x11: Invalid */
#define SVC0_CACHE_CLEAR            0x12    /* Clear cache */
#define SVC0_RIP_ANNOUNCE_NS        0x13    /* RIP announce name server */
/* 0x14-0x16: Unimplemented */
/* 0x17: Invalid */
#define SVC0_PROC2_DELIVER_PENDING  0x18    /* Deliver pending signals */
#define SVC0_PROC2_COMPLETE_FORK    0x19    /* Complete fork operation */
#define SVC0_PACCT_STOP             0x1A    /* Stop process accounting */
#define SVC0_PACCT_ON               0x1B    /* Enable process accounting */
#define SVC0_ACL_GET_LOCAL_LOCKSMITH 0x1C   /* Get local locksmith SID */
#define SVC0_ACL_IS_SUSER           0x1D    /* Check if superuser */
/* 0x1E: Invalid */
#define SVC0_SMD_N_DEVICES          0x1F    /* Get number of SMD devices */

/*
 * ============================================================================
 * TRAP #5 Syscall Numbers (Complex, 5-arg syscalls)
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
 * SVC_$TRAP0_TABLE - Simple syscall handler table (TRAP #0)
 *
 * Array of 32 handler addresses for no-argument syscalls.
 * Handlers are called directly without argument validation.
 *
 * Original address: 0x00e7b2de
 */
extern void *SVC_$TRAP0_TABLE[SVC_TRAP0_TABLE_SIZE];

/*
 * SVC_$TRAP1_TABLE - 1-argument syscall handler table (TRAP #1)
 *
 * Array of 66 handler addresses for single-argument syscalls.
 * USP and argument pointer validated < 0xCC0000.
 *
 * Original address: 0x00e7b360
 */
extern void *SVC_$TRAP1_TABLE[SVC_TRAP1_TABLE_SIZE];

/*
 * SVC_$TRAP2_TABLE - 2-argument syscall handler table (TRAP #2)
 *
 * Array of 133 handler addresses for two-argument syscalls.
 * USP and both argument pointers validated < 0xCC0000.
 *
 * Original address: 0x00e7b466
 */
extern void *SVC_$TRAP2_TABLE[SVC_TRAP2_TABLE_SIZE];

/*
 * SVC_$TRAP3_TABLE - 3-argument syscall handler table (TRAP #3)
 *
 * Array of 155 handler addresses for three-argument syscalls.
 * USP and all three argument pointers validated < 0xCC0000.
 *
 * Original address: 0x00e7b67a
 */
extern void *SVC_$TRAP3_TABLE[SVC_TRAP3_TABLE_SIZE];

/*
 * SVC_$TRAP4_TABLE - 4-argument syscall handler table (TRAP #4)
 *
 * Array of 131 handler addresses for four-argument syscalls.
 * USP and all four argument pointers validated < 0xCC0000.
 *
 * Original address: 0x00e7b8e6
 */
extern void *SVC_$TRAP4_TABLE[SVC_TRAP4_TABLE_SIZE];

/*
 * SVC_$TRAP5_TABLE - Complex syscall handler table (TRAP #5)
 *
 * Array of 99 handler addresses indexed by syscall number.
 * Invalid/unimplemented syscalls point to error handlers.
 *
 * Original address: 0x00e7baf2
 */
extern void *SVC_$TRAP5_TABLE[SVC_TRAP5_TABLE_SIZE];

/*
 * SVC_$TRAP6_TABLE - 6-argument syscall handler table (TRAP #6)
 *
 * Array of 59 handler addresses for six-argument syscalls.
 * USP and all six argument pointers validated < 0xCC0000.
 *
 * Original address: 0x00e7bc7e
 */
extern void *SVC_$TRAP6_TABLE[SVC_TRAP6_TABLE_SIZE];

/*
 * SVC_$TRAP7_TABLE - Variable-argument syscall handler table (TRAP #7)
 *
 * Array of 56 handler addresses for variable-argument syscalls.
 * Unlike TRAP #1-6, TRAP #7 uses a lookup table (SVC_$TRAP7_ARGCOUNT)
 * to determine how many arguments each syscall expects.
 *
 * Original address: 0x00e7bd6a
 */
extern void *SVC_$TRAP7_TABLE[SVC_TRAP7_TABLE_SIZE];

/*
 * SVC_$TRAP7_ARGCOUNT - Argument count table for TRAP #7
 *
 * Array of 56 bytes, where each byte contains the number of 4-byte
 * arguments (longwords) that the corresponding syscall expects.
 * Negative values indicate syscalls that handle their own validation.
 *
 * Original address: 0x00e7be4a
 */
extern unsigned char SVC_$TRAP7_ARGCOUNT[SVC_TRAP7_TABLE_SIZE];

/*
 * ============================================================================
 * Entry Points (Assembly)
 * ============================================================================
 * These are assembly language routines, not C functions.
 */

/*
 * SVC_$TRAP0 - Simple TRAP #0 syscall dispatcher
 *
 * Entry point for simple (no-argument) system calls.
 * Does NOT validate user stack or copy arguments.
 * Handler is called directly and must handle its own arguments.
 *
 * Original address: 0x00e7b044
 */

/*
 * SVC_$TRAP2 - TRAP #2 syscall dispatcher
 *
 * Entry point for 2-argument system calls. Validates syscall number,
 * checks user stack pointer and both arguments, then dispatches
 * to the appropriate handler.
 *
 * Original address: 0x00e7b094
 */

/*
 * SVC_$TRAP3 - TRAP #3 syscall dispatcher
 *
 * Entry point for 3-argument system calls. Validates syscall number,
 * checks user stack pointer and all three arguments, then dispatches
 * to the appropriate handler.
 *
 * Original address: 0x00e7b0d8
 */

/*
 * SVC_$TRAP4 - TRAP #4 syscall dispatcher
 *
 * Entry point for 4-argument system calls. Validates syscall number,
 * checks user stack pointer and all four arguments, then dispatches
 * to the appropriate handler.
 *
 * Original address: 0x00e7b120
 */

/*
 * SVC_$TRAP5 - Main TRAP #5 syscall dispatcher
 *
 * Entry point for 5-argument system calls. Validates syscall number,
 * checks user stack pointer and all five arguments, then dispatches
 * to the appropriate handler.
 *
 * Original address: 0x00e7b17c
 */

/*
 * SVC_$TRAP6 - Main TRAP #6 syscall dispatcher
 *
 * Entry point for 6-argument system calls. Validates syscall number,
 * checks user stack pointer and all six arguments, then dispatches
 * to the appropriate handler.
 *
 * Original address: 0x00e7b1d8
 */

/*
 * SVC_$TRAP7 - Variable-argument syscall dispatcher
 *
 * Entry point for variable-argument system calls. Unlike TRAP #1-6,
 * TRAP #7 looks up the argument count for each syscall from
 * SVC_$TRAP7_ARGCOUNT table, then validates and copies that many
 * arguments from the user stack.
 *
 * Features:
 *   - Variable argument count per syscall (6-17 args observed)
 *   - Uses LINK to create stack frame
 *   - Loop-based argument copying and validation
 *   - Negative argcount values skip validation (special handling)
 *
 * Original address: 0x00e7b240
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
