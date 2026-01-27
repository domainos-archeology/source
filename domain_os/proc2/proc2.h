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

#include "base/base.h"
#include "ml/ml.h"
#include "proc1/proc1.h"

/*
 * Lock ID for PROC2 operations
 */
#define PROC2_LOCK_ID 4

/*
 * Status codes
 */
#define status_$proc2_uid_not_found 0x00190001
#define status_$proc2_invalid_signal 0x00190002
#define status_$proc2_bad_process_group 0x00190003
#define status_$proc2_suspend_timed_out 0x00190005
#define status_$proc2_not_suspended 0x00190006
#define status_$proc2_already_suspended 0x00190007
#define status_$proc2_invalid_process_name 0x0019000A
#define status_$proc2_bad_eventcount_key 0x0019000B
#define status_$proc2_zombie 0x0019000E
#define status_$proc2_table_full 0x0019000F
#define status_$proc2_permission_denied 0x00190012
#define status_$proc2_process_already_debugging 0x00190011
#define status_$proc2_another_fault_pending 0x00190009
#define status_$proc2_process_wasnt_vforked 0x0019000C
#define status_$proc2_internal_error 0x00190013
#define status_$proc2_already_orphan 0x00190014
#define status_$proc2_process_is_group_leader 0x00190015
#define status_$proc2_process_using_pgroup_id 0x00190016
#define status_$proc2_pgroup_in_different_session 0x00190017

/*
 * Process flags (offset 0x2A in proc2_info_t)
 */
#define PROC2_FLAG_ZOMBIE 0x2000 /* Process is zombie */
#define PROC2_FLAG_ORPHAN                                                      \
  0x1000 /* Process is orphaned (no controlling parent) */
#define PROC2_FLAG_ALT_ASID 0x0800 /* Has alternate ASID */
#define PROC2_FLAG_VALID 0x0180    /* Process is valid */
#define PROC2_FLAG_DEBUG 0x0008    /* Process is being debugged */
#define PROC2_FLAG_SERVER 0x0002   /* Process is a server */
#define PROC2_FLAG_INIT 0x8000     /* Initial flags value */

/*
 * Signal numbers (BSD-style)
 */
#define SIGHUP 1     /* Hangup */
#define SIGINT 2     /* Interrupt */
#define SIGQUIT 3    /* Quit */
#define SIGILL 4     /* Illegal instruction */
#define SIGTRAP 5    /* Trace trap */
#define SIGABRT 6    /* Abort */
#define SIGEMT 7     /* EMT instruction */
#define SIGFPE 8     /* Floating point exception */
#define SIGKILL 9    /* Kill (cannot be caught or ignored) */
#define SIGBUS 10    /* Bus error */
#define SIGSEGV 11   /* Segmentation violation */
#define SIGSYS 12    /* Bad system call */
#define SIGPIPE 13   /* Write on pipe with no reader */
#define SIGALRM 14   /* Alarm clock */
#define SIGTERM 15   /* Software termination */
#define SIGURG 16    /* Urgent condition on socket */
#define SIGSTOP 17   /* Stop (cannot be caught or ignored) */
#define SIGTSTP 18   /* Stop from terminal */
#define SIGCONT 19   /* Continue */
#define SIGCHLD 20   /* Child status changed */
#define SIGTTIN 21   /* Background read from tty */
#define SIGTTOU 22   /* Background write to tty */
#define SIGIO 23     /* I/O possible */
#define SIGXCPU 24   /* CPU time limit exceeded */
#define SIGXFSZ 25   /* File size limit exceeded */
#define SIGVTALRM 26 /* Virtual timer expired */
#define SIGPROF 27   /* Profiling timer expired */
#define SIGWINCH 28  /* Window size changed */
#define SIGUSR1 30   /* User-defined signal 1 */
#define SIGUSR2 31   /* User-defined signal 2 */

#define NSIG 32 /* Number of signals */

/*
 * Signal mask info structure (returned by GET_SIG_MASK)
 * Size: 0x1E (30) bytes
 */
typedef struct proc2_sig_mask_t {
  uint32_t blocked_1; /* 0x00: Blocked signals part 1 */
  uint32_t blocked_2; /* 0x04: Blocked signals part 2 */
  uint32_t pending;   /* 0x08: Pending signals */
  uint32_t mask_1;    /* 0x0C: Signal mask part 1 */
  uint32_t mask_2;    /* 0x10: Signal mask part 2 */
  uint32_t mask_3;    /* 0x14: Signal mask part 3 */
  uint32_t mask_4;    /* 0x18: Signal mask part 4 */
  uint8_t flag_1;     /* 0x1C: Signal flag (from flags bit 10) */
  uint8_t flag_2;     /* 0x1D: Signal flag (from flags bit 2) */
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
  uid_t uid; /* 0x00: Process UID */

  uint8_t pad_08[0x08];      /* 0x08: Unknown */
  uint16_t pgroup_table_idx; /* 0x10: Index into pgroup table (also used in
                                LIST_PGROUP) */

  uint16_t next_index; /* 0x12: Next process index in list */
  uint16_t pad_14;     /* 0x14: Unknown */
  uint16_t upid;       /* 0x16: Unix-style PID (returned by GET_UPIDS) */
  uint16_t pad_18[2];  /* 0x18: Unknown */
  uint16_t
      owner_session; /* 0x1C: Owning session (used for permission checks) */
  uint16_t parent_pgroup_idx;  /* 0x1E: Parent process index (for pgroup leader
                                  counting) */
  uint16_t first_child_idx;    /* 0x20: First child process index (head of child
                                  list) */
  uint16_t next_child_sibling; /* 0x22: Next sibling in parent's child list */
  uint16_t first_debug_target_idx; /* 0x24: First process in debug target list
                                      (if debugger) */
  uint16_t
      debugger_idx; /* 0x26: Debugger process index (for GET_DEBUGGER_PID) */
  uint16_t
      next_debug_target_idx; /* 0x28: Next process in debugger's target list */

  uint16_t flags; /* 0x2A: Process flags */
                  /*   Bit 13 (0x2000): Zombie */
                  /*   Bit 11 (0x0800): Has alternate ASID */
                  /*   Bit 8-7 (0x0180): Valid */
                  /*   Bit 15 (0x8000): Init flag */

  uint8_t pad_2c[0x20]; /* 0x2C: Unknown */

  uid_t pgroup_uid;        /* 0x4C: Process group UID */
  uint16_t pgroup_uid_idx; /* 0x54: Process group UID index */
  uint8_t pad_56[0x06];    /* 0x56: Unknown */
  uint16_t session_id; /* 0x5C: Session ID (also returned by GET_TTY_DATA as 2nd
                          param) */
  uint16_t pad_5e;     /* 0x5E: Unknown */

  uid_t tty_uid;     /* 0x60: TTY UID (8 bytes, returned by GET_TTY_DATA) */
  uint32_t cr_rec;   /* 0x68: Creation record pointer */
  uint32_t cr_rec_2; /* 0x6C: Creation record data */

  /* Signal handling data (0x70-0x8F) */
  uint32_t sig_pending;   /* 0x70: Pending signals */
  uint32_t sig_blocked_1; /* 0x74: Blocked signals part 1 */
  uint32_t sig_blocked_2; /* 0x78: Blocked signals part 2 */
  uint32_t sig_mask_3;    /* 0x7C: Signal mask part 3 */
  uint32_t sig_mask_2;    /* 0x80: Signal mask part 2 */
  uint32_t sig_mask_1;    /* 0x84: Signal mask part 1 */
  uint32_t pad_88;        /* 0x88: Unknown */
  uint32_t sig_mask_4;    /* 0x8C: Signal mask part 4 */

  uint16_t pad_90; /* 0x90: Unknown */
  uint16_t pad_92; /* 0x92: Unknown */
  uint16_t pad_94; /* 0x94: Unknown */

  uint16_t asid;          /* 0x96: Address Space ID (returned by FIND_ASID) */
  uint16_t asid_alt;      /* 0x98: Alternate ASID (when flag 0x800 set) */
  uint16_t level1_pid;    /* 0x9A: PROC1 process ID */
  uint16_t cleanup_flags; /* 0x9C: Cleanup handler flags (bit per handler) */

  char name[32]; /* 0x9E: Process name (32 chars max) */
  uint8_t
      name_len; /* 0xBE: Process name length (0x21='!', 0x22='"' for no name) */

  uint8_t pad_bf[0x25]; /* 0xBF: Padding to 0xE4 */
} proc2_info_t;

/*
 * Process group table entry structure (8 bytes)
 * Table has 69 entries (indices 1-69), entry 0 unused.
 */
typedef struct pgroup_entry_t {
  int16_t ref_count;    /* 0x00: Reference count (0 = free slot) */
  int16_t leader_count; /* 0x02: Count of group leaders in this group */
  uint16_t upgid;       /* 0x04: Unix process group ID */
  uint16_t session_id;  /* 0x06: Session ID for this group */
} pgroup_entry_t;

#define PGROUP_TABLE_SIZE 70 /* Indices 0-69, 0 unused */

/* Base address for index calculations (table_base - entry_size) */
extern proc2_info_t *P2_INFO_TABLE;

/* Allocation pointer (index of first allocated entry) */
extern uint16_t P2_INFO_ALLOC_PTR;

/* Free list head (index of first free entry) */
extern uint16_t P2_FREE_LIST_HEAD;

/* Mapping table: PROC1 PID -> PROC2 index (at 0xEA551C + 0x3EB6) */
extern uint16_t *P2_PID_TO_INDEX_TABLE;

/* Process group table (8-byte entries at 0xEA551C + 0x3F30) */
extern pgroup_entry_t *PGROUP_TABLE;

/* Process UID storage */
extern uid_t PROC2_UID;

#define P2_INFO_ENTRY(idx) (&P2_INFO_TABLE[(idx) - 1])
#define P2_PID_TO_INDEX(pid) (P2_PID_TO_INDEX_TABLE[(pid)])
#define PGROUP_ENTRY(idx) (&PGROUP_TABLE[(idx)])

/*
 * External functions
 */
/* ML_$LOCK, ML_$UNLOCK declared in ml/ml.h */

/*
 * ============================================================================
 * Initialization
 * ============================================================================
 */

/*
 * PROC2_$INIT - Initialize PROC2 subsystem
 * Original address: 0x00e303d8
 */
status_$t PROC2_$INIT(int32_t boot_flags_param, status_$t *status_ret);

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
uint16_t PROC2_$GET_PID(uid_t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$FIND_INDEX - Find process table index by UID
 * Returns: Index in process table, 0 if not found
 * Original address: 0x00e4068e
 */
int16_t PROC2_$FIND_INDEX(uid_t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$GET_ASID - Get address space ID for process
 * Original address: 0x00e40702
 */
void PROC2_$GET_ASID(uid_t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$FIND_ASID - Find process by UID and return ASID/UPID
 * Original address: 0x00e40724
 */
uint16_t PROC2_$FIND_ASID(uid_t *proc_uid, int8_t *param_2,
                          status_$t *status_ret);

/*
 * PROC2_$UPID_TO_UID - Convert Unix PID to UID
 * Searches allocation list for matching upid and returns UID.
 * Returns proc2_$uid_not_found if not found, proc2_$zombie if zombie.
 * Original address: 0x00e40ece
 */
void PROC2_$UPID_TO_UID(int16_t *upid, uid_t *uid_ret, status_$t *status_ret);

/*
 * PROC2_$UID_TO_UPID - Convert UID to Unix PID
 * Searches allocation list for matching UID and returns upid.
 * Returns proc2_$uid_not_found if not found, proc2_$zombie if zombie.
 * Original address: 0x00e40f6c
 */
void PROC2_$UID_TO_UPID(uid_t *proc_uid, uint16_t *upid_ret,
                        status_$t *status_ret);

/*
 * ============================================================================
 * Process Naming
 * ============================================================================
 */

/*
 * PROC2_$SET_NAME - Set process name
 * Original address: 0x00e3ea2c
 */
void PROC2_$SET_NAME(char *name, int16_t *name_len, uid_t *proc_uid,
                     status_$t *status_ret);

/*
 * PROC2_$NAME_TO_UID - Find process by name
 * Searches for a process with the given name and returns its UID.
 * Original address: 0x00e3ead0
 */
void PROC2_$NAME_TO_UID(char *name, int16_t *name_len, uid_t *uid_ret,
                        status_$t *status_ret);

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
void PROC2_$GET_INFO(uid_t *proc_uid, void *info, uint16_t *info_len,
                     status_$t *status_ret);

/*
 * PROC2_$LIST - List process UIDs
 * Returns list of active process UIDs. Max 57 entries.
 * First entry is always system process UID.
 * Original address: 0x00e402f0
 */
void PROC2_$LIST(uid_t *uid_list, uint16_t *max_count, uint16_t *count);

/*
 * PROC2_$LIST2 - List processes (extended)
 * Iterates all 57 slots. Filters: (flags & 0x180) == 0x180
 * Original address: 0x00e40402
 */
void PROC2_$LIST2(uid_t *uid_list, uint16_t *max_count, uint16_t *count,
                  int32_t *start_index, uint8_t *more_flag,
                  int32_t *last_index);

/*
 * PROC2_$ZOMBIE_LIST - List zombie process UIDs
 * Iterates all 57 slots. Filters: (flags & 0x2100) == 0x2100
 * Original address: 0x00e40548
 */
void PROC2_$ZOMBIE_LIST(uid_t *uid_list, uint16_t *max_count, uint16_t *count,
                        int32_t *start_index, uint8_t *more_flag,
                        int32_t *last_index);

/*
 * PROC2_$GET_EC - Get process eventcount
 * Returns a registered eventcount for the process.
 * Key must be 0 (only valid key). Returns bad_eventcount_key otherwise.
 * Original address: 0x00e400c2
 */
void PROC2_$GET_EC(uid_t *proc_uid, int16_t *key, void **ec_ret,
                   status_$t *status_ret);

/*
 * PROC2_$GET_CR_REC - Get creation record UIDs
 * Converts EC handle to process index, returns parent and process UIDs.
 * Original address: 0x00e4015c
 */
void PROC2_$GET_CR_REC(uint32_t *ec_handle, uid_t *parent_uid, uid_t *proc_uid,
                       status_$t *status_ret);

/*
 * ============================================================================
 * Process Creation and Deletion
 * ============================================================================
 */

/*
 * PROC2_$CREATE - Create a new process
 *
 * Creates a new process with the specified parameters.
 *
 * Parameters:
 *   parent_uid  - UID of parent process
 *   code_desc   - Pointer to code descriptor
 *   map_param   - Parameter for initial memory mapping
 *   entry_point - Pointer to entry point address
 *   user_data   - Pointer to user data passed to startup
 *   reserved1   - Reserved (unused)
 *   reserved2   - Reserved (unused)
 *   flags       - Creation flags (bit 7 = create in new process group)
 *   uid_ret     - Output: new process UID
 *   ec_ret      - Output: eventcount handle for process
 *   status_ret  - Pointer to receive status
 *
 * Original address: 0x00e726ec
 */
void PROC2_$CREATE(uid_t *parent_uid, uint32_t *code_desc, uint32_t *map_param,
                   int32_t *entry_point, int32_t *user_data, uint32_t reserved1,
                   uint32_t reserved2, uint8_t *flags, uid_t *uid_ret,
                   void **ec_ret, status_$t *status_ret);

/*
 * PROC2_$FORK - Fork current process
 *
 * Forks the current process, creating a child with a copy of the
 * address space (or shared for vfork).
 *
 * Parameters:
 *   entry_point - Pointer to entry point for child
 *   user_data   - Pointer to user data passed to child startup
 *   fork_flags  - Pointer to fork flags (0 = vfork semantics, non-0 = fork)
 *   uid_ret     - Output: child process UID
 *   reserved    - Reserved (unused)
 *   upid_ret    - Output: child's UPID
 *   ec_ret      - Output: eventcount handle for fork completion
 *   status_ret  - Pointer to receive status
 *
 * Original address: 0x00e72bce
 */
void PROC2_$FORK(int32_t *entry_point, int32_t *user_data, int32_t *fork_flags,
                 uid_t *uid_ret, uint32_t reserved, uint16_t *upid_ret,
                 void **ec_ret, status_$t *status_ret);

/*
 * PROC2_$COMPLETE_FORK - Complete fork in child
 * Original address: 0x00e735f8
 */
void PROC2_$COMPLETE_FORK(status_$t *status_ret);

/*
 * PROC2_$COMPLETE_VFORK - Complete vfork in child
 *
 * Called by the child process after vfork to separate from the parent's
 * address space. Sets up a new address space for the child with the
 * specified code and entry point.
 *
 * Parameters:
 *   proc_uid    - UID for the new process
 *   code_desc   - Pointer to code descriptor
 *   map_param   - Parameter for memory mapping
 *   entry_point - Pointer to entry point address
 *   user_data   - Pointer to user data for startup
 *   reserved1   - Reserved (unused)
 *   reserved2   - Reserved (unused)
 *   status_ret  - Pointer to receive status
 *
 * On success, this function does not return - it jumps to the entry point.
 * On failure, calls PROC2_$DELETE.
 *
 * Original address: 0x00e73638
 */
void PROC2_$COMPLETE_VFORK(uid_t *proc_uid, uint32_t *code_desc,
                           uint32_t *map_param, int32_t *entry_point,
                           int32_t *user_data, uint32_t reserved1,
                           uint32_t reserved2, status_$t *status_ret);

/*
 * PROC2_$DELETE - Delete current process
 * Performs cleanup and enters infinite unbind loop.
 * Does not return - process is destroyed.
 * Original address: 0x00e74398
 */
void PROC2_$DELETE(void);

/*
 * PROC2_$STARTUP - Process startup
 * Called to complete process startup after creation.
 * Sets ASID, clears superuser mode, marks process valid, calls FIM.
 * Original address: 0x00e73454
 */
void PROC2_$STARTUP(void *context);

/*
 * PROC2_$SET_VALID - Mark process as valid
 * Marks current process as valid and initializes creation record if new
 * process. Original address: 0x00e73484
 */
void PROC2_$SET_VALID(void);

/*
 * ============================================================================
 * Signal Handling
 * ============================================================================
 */

/*
 * PROC2_$SIGNAL - Send signal to process
 * Checks permissions, then delivers signal via internal helper.
 * Original address: 0x00e3efa0
 */
void PROC2_$SIGNAL(uid_t *proc_uid, int16_t *signal, uint32_t *param,
                   status_$t *status_ret);

/*
 * PROC2_$SIGNAL_OS - Send signal (OS internal, no permission check)
 * Original address: 0x00e3f0a6
 */
void PROC2_$SIGNAL_OS(uid_t *proc_uid, int16_t *signal, uint32_t *param,
                      status_$t *status_ret);

/*
 * PROC2_$QUIT - Send SIGQUIT to process
 * Wrapper that calls SIGNAL with SIGQUIT (3).
 * Original address: 0x00e3f130
 */
void PROC2_$QUIT(uid_t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$SIGNAL_PGROUP - Send signal to process group
 * Original address: 0x00e3f23e
 */
void PROC2_$SIGNAL_PGROUP(uid_t *pgroup_uid, int16_t *signal, uint32_t *param,
                          status_$t *status_ret);

/*
 * PROC2_$SIGNAL_PGROUP_OS - Send signal to process group (OS)
 * Original address: 0x00e3f2c2
 */
void PROC2_$SIGNAL_PGROUP_OS(uid_t *pgroup_uid, int16_t *signal,
                             uint32_t *param, status_$t *status_ret);

/*
 * PROC2_$UID_TO_PGROUP_INDEX - Convert pgroup UID to index
 * Internal helper to get process group index from UID.
 * For synthetic UIDs (high byte = 0), looks up UPGID in pgroup table.
 * For real process UIDs, returns the process's pgroup_table_idx.
 * Returns 0 if not found.
 * Original address: 0x00e42272
 */
int16_t PROC2_$UID_TO_PGROUP_INDEX(uid_t *pgroup_uid);

/*
 * PROC2_$ACKNOWLEDGE - Acknowledge signal delivery
 * Called by signal handlers after processing a signal.
 * Clears signal masks and handles job control suspension.
 * Original address: 0x00e3f338
 */
void PROC2_$ACKNOWLEDGE(uint32_t *handler_addr, int16_t *signal,
                        uint32_t *result);

/*
 * PROC2_$DELIVER_PENDING_INTERNAL - Deliver pending signals (internal)
 * Internal helper to deliver all pending signals to a process.
 * Original address: 0x00e3ecea
 */
void PROC2_$DELIVER_PENDING_INTERNAL(int16_t index);

/*
 * PROC2_$DELIVER_FIM - Deliver Fault Interrupt Message
 * Delivers a fault/signal to the current process.
 * Returns 0xFF on success, 0 on failure/no signal.
 * Original address: 0x00e3edc0
 */
int8_t PROC2_$DELIVER_FIM(int16_t *signal_ret, status_$t *status,
                          uint32_t *handler_addr_ret, void *fault_param1,
                          void *fault_param2, uint32_t *mask_ret,
                          int8_t *flag_ret);

/*
 * PROC2_$DELIVER_PENDING - Deliver pending signals
 * Original address: 0x00e3f520
 */
void PROC2_$DELIVER_PENDING(status_$t *status_ret);

/*
 * PROC2_$SIGRETURN - Return from signal handler
 *
 * Restores signal mask from the sigcontext, checks for newly
 * deliverable signals, populates result with current mask/flag,
 * then calls FIM_$FAULT_RETURN to restore user state.
 *
 * Does not return.
 *
 * Parameters:
 *   context_ptr  - Pointer to pointer to sigcontext_t
 *   regs_ptr     - Pointer to pointer to register save area
 *   fp_state_ptr - Pointer to FP state
 *   result       - Output: result[0] = blocked mask, result[1] = flag
 *
 * Original address: 0x00e3f582
 */
NORETURN void PROC2_$SIGRETURN(void *context_ptr, void *regs_ptr,
                               void *fp_state_ptr, uint32_t *result);

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
 * PROC2_$SET_SIG_MASK - Set signal mask (extended)
 * Takes clear and set mask structures (8 uint32_t each).
 * Operation: new = (old & ~clear) | set
 * Original address: 0x00e3f7de
 */
void PROC2_$SET_SIG_MASK(int16_t *priority_delta, uint32_t *clear_mask,
                         uint32_t *set_mask, uint32_t *result);

/*
 * PROC2_$SIGPAUSE - Pause waiting for signal
 * Temporarily sets signal mask and waits for signal.
 * Original address: 0x00e3fa10
 */
void PROC2_$SIGPAUSE(uint32_t *new_mask, uint32_t *result);

/*
 * ============================================================================
 * Process Control
 * ============================================================================
 */

/*
 * PROC2_$SUSPEND - Suspend a process
 * Original address: 0x00e4126a
 */
void PROC2_$SUSPEND(uid_t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$RESUME - Resume a process
 * Original address: 0x00e413da
 */
void PROC2_$RESUME(uid_t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$WAIT - Wait for child process
 * Unix-style wait for child state changes.
 * Options: bit 0 = WNOHANG
 * Pid: -1=any, 0=same pgroup, >0=specific, <0=pgroup
 * Returns PID of waited child or 0 for WNOHANG with no child ready.
 * Original address: 0x00e3fdd0
 */
int16_t PROC2_$WAIT(uint16_t *options, int16_t *pid, uint32_t *result,
                    status_$t *status_ret);

/*
 * PROC2_$MAKE_ORPHAN - Make process an orphan
 * Original address: 0x00e40d1a
 */
void PROC2_$MAKE_ORPHAN(uid_t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$SHUTDOWN - Shutdown all other processes
 * Iterates through all processes and suspends valid ones except self.
 * Takes no parameters and returns no status.
 * Original address: 0x00e415c2
 */
void PROC2_$SHUTDOWN(void);

/*
 * ============================================================================
 * Process Groups
 * ============================================================================
 */

/*
 * PROC2_$SET_PGROUP - Set process group
 * Sets the process group for a target process.
 * new_upgid is the new process group ID (0 to leave current pgroup).
 * Original address: 0x00e410c8
 */
void PROC2_$SET_PGROUP(uid_t *proc_uid, uint16_t *new_upgid,
                       status_$t *status_ret);

/*
 * PROC2_$LIST_PGROUP - List process group members
 * Returns UIDs of all processes in the specified process group.
 * Original address: 0x00e401ea
 */
void PROC2_$LIST_PGROUP(uid_t *pgroup_uid, uid_t *uid_list, uint16_t *max_count,
                        uint16_t *count);

/*
 * PROC2_$PGROUP_INFO - Get process group info
 * Returns session ID and leader status for a process group.
 * is_leader is 0xFF if the group has no leader, 0 otherwise.
 * Original address: 0x00e41dbc
 */
void PROC2_$PGROUP_INFO(uint16_t *pgroup_id, uint16_t *session_id_ret,
                        uint8_t *is_leader_ret, status_$t *status_ret);

/*
 * PROC2_$UPGID_TO_UID - Convert UPGID to UID
 * Creates a synthetic UID from the UPGID by combining with UID_NIL.
 * Original address: 0x00e4100c
 */
void PROC2_$UPGID_TO_UID(uint16_t *upgid, uid_t *uid_ret,
                         status_$t *status_ret);

/*
 * PROC2_$PGUID_TO_UPGID - Convert process group UID to UPGID
 * For synthetic UIDs, extracts UPGID from high word.
 * For real process UIDs, looks up parent's UPGID.
 * Original address: 0x00e41072
 */
void PROC2_$PGUID_TO_UPGID(uid_t *pgroup_uid, uint16_t *upgid_ret,
                           status_$t *status_ret);

/*
 * ============================================================================
 * Debug Support
 * ============================================================================
 */

/*
 * PROC2_$DEBUG - Start debugging a process
 * Original address: 0x00e41620
 */
void PROC2_$DEBUG(uid_t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$OVERRIDE_DEBUG - Override debug settings
 * Original address: 0x00e41722
 */
void PROC2_$OVERRIDE_DEBUG(uid_t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$UNDEBUG - Stop debugging a process
 * Original address: 0x00e41810
 */
void PROC2_$UNDEBUG(uid_t *proc_uid, status_$t *status_ret);

/*
 * PROC2_$GET_DEBUGGER_PID - Get debugger's PID
 * Original address: 0x00e41b72
 */
uint16_t PROC2_$GET_DEBUGGER_PID(void);

/*
 * PROC2_$GET_REGS - Get process registers
 * Original address: 0x00e41d9e
 */
void PROC2_$GET_REGS(uid_t *proc_uid, void *regs, status_$t *status_ret);

/*
 * ============================================================================
 * Miscellaneous
 * ============================================================================
 */

/*
 * PROC2_$AWAKEN_GUARDIAN - Awaken guardian process
 * Notifies debugger/parent of child process state change.
 * Sends SIGTSTP and SIGCHLD to guardian.
 * Original address: 0x00e3e960
 */
void PROC2_$AWAKEN_GUARDIAN(int16_t *proc_index);

/*
 * PROC2_$SET_SERVER - Set server flag for process
 * If server_flag's high bit (0x80) is set, marks process as server.
 * Modifies bit 1 of the process flags field.
 * Original address: 0x00e41468
 */
void PROC2_$SET_SERVER(uid_t *proc_uid, int8_t *server_flag,
                       status_$t *status_ret);

/*
 * PROC2_$SET_STACK_TRUNC_SIZE - Set stack truncation size
 * Original address: 0x00e414dc
 */
void PROC2_$SET_STACK_TRUNC_SIZE(void);

/*
 * PROC2_$SET_PRIORITY - Set process priority range
 * Sets min and max priority for process, swapping if needed.
 * Calls PROC1_$SET_PRIORITY with the process's level1_pid.
 * Original address: 0x00e414de
 */
void PROC2_$SET_PRIORITY(uid_t *proc_uid, uint16_t *priority_1,
                         uint16_t *priority_2, status_$t *status_ret);

/*
 * PROC2_$SET_CLEANUP - Set cleanup flag bit
 * Sets a bit in the process's cleanup_flags field.
 * Each bit represents a different cleanup handler to call on process exit.
 * Only operates if address space is valid (PROC1_$AS_ID != 0).
 * Original address: 0x00e41572
 */
void PROC2_$SET_CLEANUP(uint16_t bit_num);

/*
 * PROC2_$SET_ACCT_INFO - Set accounting info
 * Sets accounting string (max 32 bytes) and accounting UID.
 * Original address: 0x00e41ac0
 */
void PROC2_$SET_ACCT_INFO(uint8_t *info, int16_t *info_len, uid_t *acct_uid,
                          status_$t *status_ret);

/*
 * PROC2_$GET_BOOT_FLAGS - Get boot flags
 * Returns boot flags from global variable.
 * Original address: 0x00e41b56
 */
void PROC2_$GET_BOOT_FLAGS(int16_t *flags_ret);

/*
 * PROC2_$GET_TTY_DATA - Get TTY data for current process
 * Returns tty_uid (8 bytes) and tty_flags (2 bytes) from current process.
 * Original address: 0x00e41bb8
 */
void PROC2_$GET_TTY_DATA(uid_t *tty_uid, uint16_t *tty_flags);

/*
 * PROC2_$SET_TTY - Set TTY for current process
 * Sets the controlling TTY UID. Takes only one parameter (no status return).
 * Original address: 0x00e41c04
 */
void PROC2_$SET_TTY(uid_t *tty_uid);

/*
 * PROC2_$SET_SESSION_ID - Set session ID
 * Sets session ID with validation for group leader and pgroup conflicts.
 * Original address: 0x00e41c42
 */
void PROC2_$SET_SESSION_ID(int8_t *flags, int16_t *session_id,
                           status_$t *status_ret);

/*
 * PROC2_$GET_CPU_USAGE - Get CPU usage for current process
 * Returns CPU usage structure from PROC1 plus additional data.
 * Output is 24 bytes (5 longwords from PROC1 + 1 constant 0x411c).
 * Original address: 0x00e41d2a
 */
void PROC2_$GET_CPU_USAGE(uint32_t *usage);

/*
 * PROC2_$ALIGN_CTL - Alignment control
 * Stub function that zeroes output parameters.
 * Original address: 0x00e41d74
 */
void PROC2_$ALIGN_CTL(int param_1, int param_2, uint16_t *result_1,
                      uint32_t *result_2, status_$t *status_ret);

/*
 * PROC2_$WHO_AM_I - Get current process UID
 * Looks up current process in PROC2 table and returns its UID.
 * Original address: 0x00e73862
 */
void PROC2_$WHO_AM_I(uid_t *proc_uid);

/*
 * PROC2_$GET_UPIDS - Get Unix PIDs for process
 * Original address: 0x00e738a8
 */
void PROC2_$GET_UPIDS(uid_t *proc_uid, uint16_t *upid, uint16_t *upgid,
                      uint16_t *uppid, status_$t *status_ret);

/*
 * PROC2_$GET_MY_UPIDS - Get my Unix PIDs
 * Original address: 0x00e73968
 */
void PROC2_$GET_MY_UPIDS(uint16_t *upid, uint16_t *upgid, uint16_t *uppid);

#endif /* PROC2_H */
