/*
 * PROC2 - High-Level Process Management Module
 *
 * This module provides the higher-level process management API for Domain/OS.
 * It builds on top of PROC1 (the low-level process dispatcher) and provides:
 *
 * - Process creation, forking, and deletion
 * - Process naming and identification (UIDs, UPIDs)
 * - Signal handling (BSD-style signals)
 * - Process groups
 * - Process information queries
 * - Debug support
 *
 * Each process has a proc2_info_t structure (228 bytes) that tracks
 * higher-level state beyond what the PCB (proc1_t) contains.
 *
 * Memory layout (m68k):
 *   - Process info table: 0xEA551C (base for index 1)
 *   - Table entry size: 0xE4 (228) bytes
 *   - Max processes: ~70 (indices 1-69)
 *   - Allocation pointer: P2_INFO_ALLOC_PTR at 0xE7BE84 + 0x1E0
 */

#ifndef PROC2_H
#define PROC2_H

#include "../base/base.h"
#include "../proc1/proc1.h"

/*
 * Lock ID for PROC2 operations
 */
#define PROC2_LOCK_ID           4

/*
 * Status codes
 */
#define status_$proc2_uid_not_found     0x00190001
#define status_$proc2_zombie            0x0019000E
#define status_$proc2_invalid_signal    0x00190002
#define status_$proc2_bad_process_group 0x00190003

/*
 * Process flags (offset 0x2A in proc2_info_t)
 */
#define PROC2_FLAG_ZOMBIE       0x2000  /* Process is zombie */
#define PROC2_FLAG_VALID        0x0180  /* Process is valid */
#define PROC2_FLAG_INIT         0x8000  /* Initial flags value */

/*
 * Signal numbers (BSD-style)
 */
#define SIGHUP      1       /* Hangup */
#define SIGINT      2       /* Interrupt */
#define SIGQUIT     3       /* Quit */
#define SIGILL      4       /* Illegal instruction */
#define SIGTRAP     5       /* Trace trap */
#define SIGABRT     6       /* Abort */
#define SIGEMT      7       /* EMT instruction */
#define SIGFPE      8       /* Floating point exception */
#define SIGKILL     9       /* Kill (cannot be caught or ignored) */
#define SIGBUS      10      /* Bus error */
#define SIGSEGV     11      /* Segmentation violation */
#define SIGSYS      12      /* Bad system call */
#define SIGPIPE     13      /* Write on pipe with no reader */
#define SIGALRM     14      /* Alarm clock */
#define SIGTERM     15      /* Software termination */
#define SIGURG      16      /* Urgent condition on socket */
#define SIGSTOP     17      /* Stop (cannot be caught or ignored) */
#define SIGTSTP     18      /* Stop from terminal */
#define SIGCONT     19      /* Continue */
#define SIGCHLD     20      /* Child status changed */
#define SIGTTIN     21      /* Background read from tty */
#define SIGTTOU     22      /* Background write to tty */
#define SIGIO       23      /* I/O possible */
#define SIGXCPU     24      /* CPU time limit exceeded */
#define SIGXFSZ     25      /* File size limit exceeded */
#define SIGVTALRM   26      /* Virtual timer expired */
#define SIGPROF     27      /* Profiling timer expired */
#define SIGWINCH    28      /* Window size changed */
#define SIGUSR1     30      /* User-defined signal 1 */
#define SIGUSR2     31      /* User-defined signal 2 */

#define NSIG        32      /* Number of signals */

/*
 * Signal mask info structure (returned by GET_SIG_MASK)
 * Size: 0x1E (30) bytes
 */
typedef struct proc2_sig_mask_t {
    uint32_t    blocked_1;      /* 0x00: Blocked signals part 1 */
    uint32_t    blocked_2;      /* 0x04: Blocked signals part 2 */
    uint32_t    pending;        /* 0x08: Pending signals */
    uint32_t    mask_1;         /* 0x0C: Signal mask part 1 */
    uint32_t    mask_2;         /* 0x10: Signal mask part 2 */
    uint32_t    mask_3;         /* 0x14: Signal mask part 3 */
    uint32_t    mask_4;         /* 0x18: Signal mask part 4 */
    uint8_t     flag_1;         /* 0x1C: Signal flag (from flags bit 10) */
    uint8_t     flag_2;         /* 0x1D: Signal flag (from flags bit 2) */
} proc2_sig_mask_t;

/*
 * Process information structure (proc2_info_t)
 * Size: 0xE4 (228) bytes
 *
 * Note: Offsets determined by analysis of FIND_INDEX and other functions.
 * The table is accessed as: base_addr + index * 0xE4
 * where base_addr is 0xEA5438 (table start - 0xE4 for 1-based indexing)
 */
typedef struct proc2_info_t {
    uid_$t      uid;            /* 0x00: Process UID */

    uint8_t     pad_08[0x08];   /* 0x08: Unknown */
    uint16_t    parent_idx;     /* 0x10: Parent process index (for upid lookup) */

    uint16_t    next_index;     /* 0x12: Next process index in list */
    uint16_t    pad_14;         /* 0x14: Unknown */
    uint16_t    upid;           /* 0x16: Unix-style PID (returned by GET_UPIDS) */
    uint16_t    pad_18[3];      /* 0x18: Unknown */
    uint16_t    pgroup_idx;     /* 0x1E: Process group index (for pgroup upid lookup) */
    uint16_t    pad_20[5];      /* 0x20: Unknown */

    uint16_t    flags;          /* 0x2A: Process flags */
                                /*   Bit 13 (0x2000): Zombie */
                                /*   Bit 11 (0x0800): Has alternate ASID */
                                /*   Bit 8-7 (0x0180): Valid */
                                /*   Bit 15 (0x8000): Init flag */

    uint8_t     pad_2c[0x20];   /* 0x2C: Unknown */

    uid_$t      pgroup_uid;     /* 0x4C: Process group UID */
    uint16_t    pgroup_uid_idx; /* 0x54: Process group UID index */
    uint8_t     pad_56[0x0A];   /* 0x56: Unknown */

    uid_$t      parent_uid;     /* 0x60: Parent process UID */
    uint32_t    cr_rec;         /* 0x68: Creation record pointer */
    uint32_t    cr_rec_2;       /* 0x6C: Creation record data */

    /* Signal handling data (0x70-0x8F) */
    uint32_t    sig_pending;    /* 0x70: Pending signals */
    uint32_t    sig_blocked_1;  /* 0x74: Blocked signals part 1 */
    uint32_t    sig_blocked_2;  /* 0x78: Blocked signals part 2 */
    uint32_t    sig_mask_3;     /* 0x7C: Signal mask part 3 */
    uint32_t    sig_mask_2;     /* 0x80: Signal mask part 2 */
    uint32_t    sig_mask_1;     /* 0x84: Signal mask part 1 */
    uint32_t    pad_88;         /* 0x88: Unknown */
    uint32_t    sig_mask_4;     /* 0x8C: Signal mask part 4 */

    uint16_t    pad_90;         /* 0x90: Unknown */
    uint16_t    pad_92;         /* 0x92: Unknown */
    uint16_t    pad_94;         /* 0x94: Unknown */

    uint16_t    asid;           /* 0x96: Address Space ID (returned by FIND_ASID) */
    uint16_t    asid_alt;       /* 0x98: Alternate ASID (when flag 0x800 set) */
    uint16_t    level1_pid;     /* 0x9A: PROC1 process ID */
    uint16_t    pad_9c;         /* 0x9C: Unknown */

    uint8_t     pad_9e[0x20];   /* 0x9E: Unknown */

    uint8_t     name_len;       /* 0xBE: Process name length */
    char        name[32];       /* 0xBF: Process name */

    uint8_t     pad_df[5];      /* 0xDF: Padding to 0xE4 */
} proc2_info_t;

/*
 * Global variables
 */
#if defined(M68K)
    /* Base address for index calculations (table_base - entry_size) */
    #define P2_INFO_TABLE_BASE      0xEA5438
    #define P2_INFO_TABLE           ((proc2_info_t*)(P2_INFO_TABLE_BASE + 0xE4))
    #define P2_INFO_ENTRY(idx)      ((proc2_info_t*)(P2_INFO_TABLE_BASE + (idx) * 0xE4))

    /* Allocation pointer (index of first allocated entry) */
    #define P2_INFO_ALLOC_PTR       (*(uint16_t*)0xE7C064)  /* 0xE7BE84 + 0x1E0 */

    /* Mapping table: PROC1 PID -> PROC2 index (at 0xEA551C + 0x3EB6) */
    #define P2_PID_TO_INDEX_TABLE   ((uint16_t*)(0xEA551C + 0x3EB6))
    #define P2_PID_TO_INDEX(pid)    (P2_PID_TO_INDEX_TABLE[(pid)])

    /* Parent UPID lookup table (8-byte entries at 0xEA551C + 0x3F34) */
    #define P2_PARENT_UPID_BASE     (0xEA551C + 0x3F34)
    #define P2_PARENT_UPID(idx)     (*(uint16_t*)(P2_PARENT_UPID_BASE + (idx) * 8))

    /* Process UID storage */
    #define PROC2_UID               (*(uid_$t*)0xE7BE8C)
#else
    extern proc2_info_t *p2_info_table;
    extern uint16_t p2_info_alloc_ptr;
    extern uint16_t *p2_pid_to_index_table;
    extern uint16_t *p2_parent_upid_table;  /* 8-byte entries, upid at offset 0 */
    extern uid_$t proc2_uid;
    #define P2_INFO_TABLE           p2_info_table
    #define P2_INFO_ENTRY(idx)      (&p2_info_table[(idx) - 1])
    #define P2_INFO_ALLOC_PTR       p2_info_alloc_ptr
    #define P2_PID_TO_INDEX_TABLE   p2_pid_to_index_table
    #define P2_PID_TO_INDEX(pid)    (p2_pid_to_index_table[(pid)])
    #define P2_PARENT_UPID_BASE     ((uintptr_t)p2_parent_upid_table)
    #define P2_PARENT_UPID(idx)     (*(uint16_t*)(P2_PARENT_UPID_BASE + (idx) * 8))
    #define PROC2_UID               proc2_uid
#endif

/*
 * External functions
 */
extern void ML_$LOCK(uint16_t lock_id);
extern void ML_$UNLOCK(uint16_t lock_id);

/*
 * ============================================================================
 * Initialization
 * ============================================================================
 */

/*
 * PROC2_$INIT - Initialize PROC2 subsystem
 * Original address: 0x00e303d8
 */
status_$t PROC2_$INIT(int param_1, status_$t *status_ret);

/*
 * ============================================================================
 * Process Identification
 * ============================================================================
 */

/*
 * PROC2_$MY_PID - Get current process ID
 * Returns: Current PROC1 PID
 * Original address: 0x00e40ca0
 */
uint16_t PROC2_$MY_PID(void);

/*
 * PROC2_$GET_PID - Get PID from UID
 * Original address: 0x00e40cba
 */
uint16_t PROC2_$GET_PID(uid_$t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$FIND_INDEX - Find process table index by UID
 * Returns: Index in process table, 0 if not found
 * Original address: 0x00e4068e
 */
int16_t PROC2_$FIND_INDEX(uid_$t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$GET_ASID - Get address space ID for process
 * Original address: 0x00e40702
 */
void PROC2_$GET_ASID(uid_$t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$FIND_ASID - Find process by UID and return ASID/UPID
 * Original address: 0x00e40724
 */
uint16_t PROC2_$FIND_ASID(uid_$t *proc_uid, int8_t *param_2, status_$t *status_ret);

/*
 * PROC2_$UPID_TO_UID - Convert Unix PID to UID
 * Original address: 0x00e40ece
 */
void PROC2_$UPID_TO_UID(uint16_t upid, uid_$t *uid_ret, status_$t *status_ret);

/*
 * PROC2_$UID_TO_UPID - Convert UID to Unix PID
 * Original address: 0x00e40f6c
 */
uint16_t PROC2_$UID_TO_UPID(uid_$t *proc_uid, status_$t *status_ret);

/*
 * ============================================================================
 * Process Naming
 * ============================================================================
 */

/*
 * PROC2_$SET_NAME - Set process name
 * Original address: 0x00e3ea2c
 */
void PROC2_$SET_NAME(char *name, int16_t *name_len, uid_$t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$NAME_TO_UID - Find process by name
 * Original address: 0x00e3ead0
 */
void PROC2_$NAME_TO_UID(char *name, uint16_t name_len, uid_$t *uid_ret, status_$t *status_ret);

/*
 * ============================================================================
 * Process Information
 * ============================================================================
 */

/*
 * PROC2_$INFO - Get process info by PID
 * Original address: 0x00e407b4
 */
void PROC2_$INFO(int16_t *pid, int16_t *offset, void *info, uint16_t *info_len,
                 status_$t *status_ret);

/*
 * PROC2_$GET_INFO - Get process info by UID
 * Original address: 0x00e4086e
 */
void PROC2_$GET_INFO(uid_$t *proc_uid, void *info, uint16_t *info_len, status_$t *status_ret);

/*
 * PROC2_$LIST - List processes
 * Original address: 0x00e402f0
 */
void PROC2_$LIST(void *list, uint16_t *count, status_$t *status_ret);

/*
 * PROC2_$LIST2 - List processes (extended)
 * Original address: 0x00e40402
 */
void PROC2_$LIST2(void *list, uint16_t *count, status_$t *status_ret);

/*
 * PROC2_$ZOMBIE_LIST - List zombie processes
 * Original address: 0x00e40548
 */
void PROC2_$ZOMBIE_LIST(void *list, uint16_t *count, status_$t *status_ret);

/*
 * PROC2_$GET_EC - Get process eventcount
 * Original address: 0x00e400c2
 */
void PROC2_$GET_EC(uid_$t *proc_uid, void *ec_ret, status_$t *status_ret);

/*
 * PROC2_$GET_CR_REC - Get creation record
 * Original address: 0x00e4015c
 */
void PROC2_$GET_CR_REC(uid_$t *proc_uid, void *cr_rec_ret, status_$t *status_ret);

/*
 * ============================================================================
 * Process Creation and Deletion
 * ============================================================================
 */

/*
 * PROC2_$CREATE - Create a new process
 * Original address: 0x00e726ec
 */
void PROC2_$CREATE(void *params, uid_$t *uid_ret, status_$t *status_ret);

/*
 * PROC2_$FORK - Fork current process
 * Original address: 0x00e72bce
 */
void PROC2_$FORK(void *params, uid_$t *uid_ret, status_$t *status_ret);

/*
 * PROC2_$COMPLETE_FORK - Complete fork in child
 * Original address: 0x00e735f8
 */
void PROC2_$COMPLETE_FORK(status_$t *status_ret);

/*
 * PROC2_$COMPLETE_VFORK - Complete vfork in child
 * Original address: 0x00e73638
 */
void PROC2_$COMPLETE_VFORK(status_$t *status_ret);

/*
 * PROC2_$DELETE - Delete a process
 * Original address: 0x00e74398
 */
void PROC2_$DELETE(uid_$t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$STARTUP - Process startup
 * Original address: 0x00e73454
 */
void PROC2_$STARTUP(void);

/*
 * PROC2_$SET_VALID - Mark process as valid
 * Original address: 0x00e73484
 */
void PROC2_$SET_VALID(uid_$t *proc_uid, status_$t *status_ret);

/*
 * ============================================================================
 * Signal Handling
 * ============================================================================
 */

/*
 * PROC2_$SIGNAL - Send signal to process
 * Original address: 0x00e3efa0
 */
void PROC2_$SIGNAL(uid_$t *proc_uid, uint16_t signal, status_$t *status_ret);

/*
 * PROC2_$SIGNAL_OS - Send signal (OS internal)
 * Original address: 0x00e3f0a6
 */
void PROC2_$SIGNAL_OS(uid_$t *proc_uid, uint16_t *signal, void *param, status_$t *status_ret);

/*
 * PROC2_$QUIT - Send quit signal
 * Original address: 0x00e3f130
 */
void PROC2_$QUIT(uid_$t *proc_uid, status_$t status, status_$t *status_ret);

/*
 * PROC2_$SIGNAL_PGROUP - Send signal to process group
 * Original address: 0x00e3f23e
 */
void PROC2_$SIGNAL_PGROUP(uid_$t *pgroup_uid, uint16_t signal, status_$t *status_ret);

/*
 * PROC2_$SIGNAL_PGROUP_OS - Send signal to process group (OS)
 * Original address: 0x00e3f2c2
 */
void PROC2_$SIGNAL_PGROUP_OS(uid_$t *pgroup_uid, uint16_t signal, void *param,
                             status_$t *status_ret);

/*
 * PROC2_$ACKNOWLEDGE - Acknowledge signal
 * Original address: 0x00e3f338
 */
void PROC2_$ACKNOWLEDGE(status_$t *status_ret);

/*
 * PROC2_$DELIVER_FIM - Deliver fault to process
 * Original address: 0x00e3edc0
 */
void PROC2_$DELIVER_FIM(uid_$t *proc_uid, void *fault_info, status_$t *status_ret);

/*
 * PROC2_$DELIVER_PENDING - Deliver pending signals
 * Original address: 0x00e3f520
 */
void PROC2_$DELIVER_PENDING(status_$t *status_ret);

/*
 * PROC2_$SIGRETURN - Return from signal handler
 * Original address: 0x00e3f582
 */
void PROC2_$SIGRETURN(void *context);

/*
 * PROC2_$SIGBLOCK - Block additional signals
 * ORs mask into blocked signals. Returns old mask.
 * result[0] = new mask, result[1] = flag
 * Original address: 0x00e3f63e
 */
uint32_t PROC2_$SIGBLOCK(uint32_t *mask_ptr, uint32_t *result);

/*
 * PROC2_$SIGSETMASK - Set signal mask
 * Sets blocked signals to mask. Returns old mask.
 * result[0] = new mask, result[1] = flag
 * Original address: 0x00e3f6c0
 */
uint32_t PROC2_$SIGSETMASK(uint32_t *mask_ptr, uint32_t *result);

/*
 * PROC2_$GET_SIG_MASK - Get current process signal mask info
 * Fills in signal mask structure for current process.
 * Original address: 0x00e3f75a
 */
void PROC2_$GET_SIG_MASK(proc2_sig_mask_t *mask_ret);

/*
 * PROC2_$SET_SIG_MASK - Set signal mask
 * Original address: 0x00e3f7de
 */
void PROC2_$SET_SIG_MASK(uint32_t mask);

/*
 * PROC2_$SIGPAUSE - Pause waiting for signal
 * Original address: 0x00e3fa10
 */
void PROC2_$SIGPAUSE(uint32_t mask);

/*
 * ============================================================================
 * Process Control
 * ============================================================================
 */

/*
 * PROC2_$SUSPEND - Suspend a process
 * Original address: 0x00e4126a
 */
void PROC2_$SUSPEND(uid_$t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$RESUME - Resume a process
 * Original address: 0x00e413da
 */
void PROC2_$RESUME(uid_$t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$WAIT - Wait for child process
 * Original address: 0x00e3fdd0
 */
void PROC2_$WAIT(void *status_ret, status_$t *st);

/*
 * PROC2_$MAKE_ORPHAN - Make process an orphan
 * Original address: 0x00e40d1a
 */
void PROC2_$MAKE_ORPHAN(uid_$t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$SHUTDOWN - Shutdown process
 * Original address: 0x00e415c2
 */
void PROC2_$SHUTDOWN(uid_$t *proc_uid, status_$t *status_ret);

/*
 * ============================================================================
 * Process Groups
 * ============================================================================
 */

/*
 * PROC2_$SET_PGROUP - Set process group
 * Original address: 0x00e410c8
 */
void PROC2_$SET_PGROUP(uid_$t *proc_uid, uid_$t *pgroup_uid, status_$t *status_ret);

/*
 * PROC2_$LIST_PGROUP - List process group members
 * Original address: 0x00e401ea
 */
void PROC2_$LIST_PGROUP(uid_$t *pgroup_uid, void *list, uint16_t *count, status_$t *status_ret);

/*
 * PROC2_$PGROUP_INFO - Get process group info
 * Original address: 0x00e41dbc
 */
void PROC2_$PGROUP_INFO(uid_$t *pgroup_uid, void *info, status_$t *status_ret);

/*
 * PROC2_$UPGID_TO_UID - Convert UPGID to UID
 * Original address: 0x00e4100c
 */
void PROC2_$UPGID_TO_UID(uint16_t upgid, uid_$t *uid_ret, status_$t *status_ret);

/*
 * PROC2_$PGUID_TO_UPGID - Convert process group UID to UPGID
 * Original address: 0x00e41072
 */
uint16_t PROC2_$PGUID_TO_UPGID(uid_$t *pgroup_uid, status_$t *status_ret);

/*
 * ============================================================================
 * Debug Support
 * ============================================================================
 */

/*
 * PROC2_$DEBUG - Start debugging a process
 * Original address: 0x00e41620
 */
void PROC2_$DEBUG(uid_$t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$OVERRIDE_DEBUG - Override debug settings
 * Original address: 0x00e41722
 */
void PROC2_$OVERRIDE_DEBUG(uid_$t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$UNDEBUG - Stop debugging a process
 * Original address: 0x00e41810
 */
void PROC2_$UNDEBUG(uid_$t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$GET_DEBUGGER_PID - Get debugger's PID
 * Original address: 0x00e41b72
 */
uint16_t PROC2_$GET_DEBUGGER_PID(void);

/*
 * PROC2_$GET_REGS - Get process registers
 * Original address: 0x00e41d9e
 */
void PROC2_$GET_REGS(uid_$t *proc_uid, void *regs, status_$t *status_ret);

/*
 * ============================================================================
 * Miscellaneous
 * ============================================================================
 */

/*
 * PROC2_$AWAKEN_GUARDIAN - Awaken guardian process
 * Original address: 0x00e3e960
 */
void PROC2_$AWAKEN_GUARDIAN(void);

/*
 * PROC2_$SET_SERVER - Set server process
 * Original address: 0x00e41468
 */
void PROC2_$SET_SERVER(uid_$t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$SET_STACK_TRUNC_SIZE - Set stack truncation size
 * Original address: 0x00e414dc
 */
void PROC2_$SET_STACK_TRUNC_SIZE(void);

/*
 * PROC2_$SET_PRIORITY - Set process priority
 * Original address: 0x00e414de
 */
void PROC2_$SET_PRIORITY(uid_$t *proc_uid, int16_t priority, status_$t *status_ret);

/*
 * PROC2_$SET_CLEANUP - Set cleanup handler
 * Original address: 0x00e41572
 */
void PROC2_$SET_CLEANUP(void *handler, status_$t *status_ret);

/*
 * PROC2_$SET_ACCT_INFO - Set accounting info
 * Original address: 0x00e41ac0
 */
void PROC2_$SET_ACCT_INFO(void *info, status_$t *status_ret);

/*
 * PROC2_$GET_BOOT_FLAGS - Get boot flags
 * Original address: 0x00e41b56
 */
uint16_t PROC2_$GET_BOOT_FLAGS(void);

/*
 * PROC2_$GET_TTY_DATA - Get TTY data
 * Original address: 0x00e41bb8
 */
void PROC2_$GET_TTY_DATA(void *data, status_$t *status_ret);

/*
 * PROC2_$SET_TTY - Set TTY
 * Original address: 0x00e41c04
 */
void PROC2_$SET_TTY(void *tty_data, status_$t *status_ret);

/*
 * PROC2_$SET_SESSION_ID - Set session ID
 * Original address: 0x00e41c42
 */
void PROC2_$SET_SESSION_ID(uint16_t session_id, status_$t *status_ret);

/*
 * PROC2_$GET_CPU_USAGE - Get CPU usage
 * Original address: 0x00e41d2a
 */
void PROC2_$GET_CPU_USAGE(uid_$t *proc_uid, void *usage, status_$t *status_ret);

/*
 * PROC2_$ALIGN_CTL - Alignment control
 * Original address: 0x00e41d74
 */
void PROC2_$ALIGN_CTL(int param, status_$t *status_ret);

/*
 * PROC2_$WHO_AM_I - Get current process UID
 * Looks up current process in PROC2 table and returns its UID.
 * Original address: 0x00e73862
 */
void PROC2_$WHO_AM_I(uid_$t *proc_uid);

/*
 * PROC2_$GET_UPIDS - Get Unix PIDs for process
 * Original address: 0x00e738a8
 */
void PROC2_$GET_UPIDS(uid_$t *proc_uid, uint16_t *upid, uint16_t *upgid, uint16_t *uppid,
                      status_$t *status_ret);

/*
 * PROC2_$GET_MY_UPIDS - Get my Unix PIDs
 * Original address: 0x00e73968
 */
void PROC2_$GET_MY_UPIDS(uint16_t *upid, uint16_t *upgid, uint16_t *uppid);

#endif /* PROC2_H */
