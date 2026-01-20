/*
 * XPD - eXtended Process Debugging Module
 *
 * This module provides process debugging capabilities for Domain/OS:
 * - Debugger registration and unregistration
 * - Process tracing options (ptrace-like functionality)
 * - Fault capture and event handling
 * - Target process memory read/write
 * - Register access (general-purpose and floating-point)
 * - Process restart/continue operations
 *
 * The XPD subsystem allows a debugger process to attach to and control
 * a target process. Each debugger can control multiple targets, and
 * each target can only have one debugger.
 *
 * Memory layout (m68k):
 *   - XPD data base: 0xEA5034
 *   - Debugger table: 6 entries at 0xEA5044 (16 bytes per entry)
 *   - xpd_$lock = 2 (resource lock for XPD operations)
 *
 * Original addresses:
 *   - XPD_$INIT: 0x00e32304
 *   - XPD_$DATA: 0xEA5034
 */

#ifndef XPD_H
#define XPD_H

#include "base/base.h"
#include "ec/ec.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "ml/ml.h"

/*
 * Lock IDs
 */
#define XPD_LOCK_ID             2       /* xpd_$lock */

/*
 * Status codes
 */
#define status_$xpd_not_a_debugger              0x00160005
#define status_$xpd_debugger_not_found          0x00160006
#define status_$xpd_debugger_table_full         0x00160007
#define status_$xpd_already_a_debugger          0x00160009
#define status_$xpd_target_not_suspended        0x0016000B
#define status_$xpd_invalid_ec_key              0x0016000C
#define status_$xpd_state_unavailable_for_this_event  0x0016000E
#define status_$xpd_invalid_option              0x0016000F
#define status_$xpd_proc_not_debug_target       0x00190010
#define status_$xpd_illegal_target_setup        0x00160011
#define status_$xpd_invalid_state_argument      0x00160003

/*
 * Status codes for debug events/faults
 */
#define status_$xpd_target_is_forking           0x00160012
#define status_$xpd_target_is_execing           0x00160013
#define status_$xpd_target_is_invoking          0x00160014
#define status_$xpd_target_is_exiting           0x00160015
#define status_$xpd_target_is_loading_exec_image 0x00160016
#define status_$fault_single_step_completed     0x00120015
#define status_$fault_process_BLAST             0x00120019
#define status_$mst_guard_fault                 0x0004000a

/*
 * Ptrace options structure
 * Size: 0x0E (14) bytes
 *
 * Used by SET_PTRACE_OPTS, INQ_PTRACE_OPTS, RESET_PTRACE_OPTS
 */
typedef struct xpd_$ptrace_opts_t {
    uint32_t    signal_mask;    /* 0x00: Bitmask of signals to trap */
    uint32_t    trace_range_lo; /* 0x04: Low address for trace range */
    uint32_t    trace_range_hi; /* 0x08: High address for trace range */
    uint8_t     flags;          /* 0x0C: Trace flags */
                                /*   Bit 0 (0x01): Trap on signals in mask */
                                /*   Bit 1 (0x02): Inherit options on fork */
                                /*   Bit 2 (0x04): Unknown */
                                /*   Bit 3 (0x08): Inherit ptrace options */
                                /*   Bit 4 (0x10): Unknown */
                                /*   Bit 5 (0x20): Unknown */
                                /*   Bit 6 (0x40): Trace outside range */
                                /*   Bit 7 (0x80): Trace inside range */
    uint8_t     flags2;         /* 0x0D: Additional flags */
} xpd_$ptrace_opts_t;

/*
 * Debugger table entry structure
 * Size: 0x10 (16) bytes
 *
 * Located at XPD_$DATA + 0x10 * debugger_index
 */
typedef struct xpd_$debugger_entry_t {
    ec_$eventcount_t ec;        /* 0x00: Eventcount for this debugger slot */
    uint16_t    asid;           /* 0x0C: Address space ID of debugger */
                                /*       (0 = slot is free) */
    uint16_t    pad;            /* 0x0E: Padding */
} xpd_$debugger_entry_t;

/*
 * XPD data structure flags (stored in proc2_info_t at various offsets)
 *
 * Flags at offset 0x2B (flags byte within process debug info):
 *   Bit 0 (0x01): Unknown
 *   Bit 1 (0x02): Trace fault pending
 *   Bit 2 (0x04): Unknown
 *   Bit 3 (0x08): Unknown
 *   Bit 4 (0x10): Target is suspended by debugger
 *   Bit 5 (0x20): Debug state saved
 *   Bit 6 (0x40): Event acknowledged
 *   Bit 7 (0x80): Debug target flag
 */
#define XPD_FLAG_SUSPENDED          0x10
#define XPD_FLAG_STATE_SAVED        0x20
#define XPD_FLAG_EVENT_ACKED        0x40
#define XPD_FLAG_DEBUG_TARGET       0x80
#define XPD_FLAG_TRACE_PENDING      0x02

/*
 * Restart modes for XPD_$RESTART
 */
#define XPD_RESTART_MODE_CONTINUE   1   /* Continue execution */
#define XPD_RESTART_MODE_STEP       2   /* Single step with trace */
#define XPD_RESTART_MODE_STEP_NO_TRACE 3 /* Single step without trace */

/*
 * Register info modes for XPD_$GET_REGISTERS / XPD_$PUT_REGISTERS
 */
#define XPD_REG_MODE_GENERAL        0   /* General purpose registers (D0-D7, A0-A7) */
#define XPD_REG_MODE_EXCEPTION      1   /* Exception frame */
#define XPD_REG_MODE_FP_STATE       2   /* Floating point state */
#define XPD_REG_MODE_DEBUG_STATE    3   /* Debug state info */

/*
 * Maximum number of debugger slots
 */
#define XPD_MAX_DEBUGGERS           6

/*
 * Maximum number of debug targets (same as PROC2 max processes)
 */
#define XPD_MAX_TARGETS             57

/*
 * Global data
 */
#if defined(M68K)
    #define XPD_$DATA               (*(ec_$eventcount_t *)0xEA5034)
    #define XPD_$DEBUGGER_TABLE     ((xpd_$debugger_entry_t *)(0xEA5034 + 0x10))
#else
    extern ec_$eventcount_t XPD_$DATA;
    extern xpd_$debugger_entry_t XPD_$DEBUGGER_TABLE[XPD_MAX_DEBUGGERS];
#endif

/*
 * ============================================================================
 * Initialization and Cleanup
 * ============================================================================
 */

/*
 * XPD_$INIT - Initialize XPD subsystem
 *
 * Initializes the XPD data area including all eventcounts for
 * debugger slots and target processes.
 *
 * Original address: 0x00e32304
 */
void XPD_$INIT(void);

/*
 * XPD_$CLEANUP - Cleanup XPD state for current process
 *
 * Called when a process exits to release any debug resources.
 * Unregisters the process as a debugger and clears debug state.
 *
 * Original address: 0x00e75046
 */
void XPD_$CLEANUP(void);

/*
 * ============================================================================
 * Debugger Registration
 * ============================================================================
 */

/*
 * XPD_$SET_DEBUGGER - Set up debugger/target relationship
 *
 * Establishes a debugging relationship between a debugger process
 * and a target process. If target_uid is NIL, removes the debugger.
 * If debugger_uid is NIL, removes the debugger from the target.
 *
 * Parameters:
 *   debugger_uid - UID of debugger process (or NIL to remove)
 *   target_uid   - UID of target process (or NIL to remove debugger)
 *   status_ret   - Status return
 *
 * Original address: 0x00e5bbd8
 */
void XPD_$SET_DEBUGGER(uid_t *debugger_uid, uid_t *target_uid, status_$t *status_ret);

/*
 * ============================================================================
 * Ptrace Options
 * ============================================================================
 */

/*
 * XPD_$SET_PTRACE_OPTS - Set process trace options
 *
 * Sets trace options for a target process. The caller must be the
 * debugger or the target process itself.
 *
 * Parameters:
 *   proc_uid   - UID of target process (or NIL for current process)
 *   opts       - Pointer to ptrace options structure
 *   status_ret - Status return
 *
 * Original address: 0x00e5af9e
 */
void XPD_$SET_PTRACE_OPTS(uid_t *proc_uid, xpd_$ptrace_opts_t *opts, status_$t *status_ret);

/*
 * XPD_$INQ_PTRACE_OPTS - Inquire process trace options
 *
 * Retrieves trace options for a target process.
 *
 * Parameters:
 *   proc_uid   - UID of target process (or NIL for current process)
 *   opts       - Pointer to receive ptrace options
 *   status_ret - Status return
 *
 * Original address: 0x00e5b076
 */
void XPD_$INQ_PTRACE_OPTS(uid_t *proc_uid, xpd_$ptrace_opts_t *opts, status_$t *status_ret);

/*
 * XPD_$RESET_PTRACE_OPTS - Reset ptrace options to defaults
 *
 * Clears all ptrace options in the given structure.
 *
 * Parameters:
 *   opts - Pointer to ptrace options structure to reset
 *
 * Original address: 0x00e5b156
 */
void XPD_$RESET_PTRACE_OPTS(xpd_$ptrace_opts_t *opts);

/*
 * XPD_$INHERIT_PTRACE_OPTIONS - Check if ptrace options should inherit
 *
 * Checks if the inherit flag is set in the ptrace options.
 *
 * Parameters:
 *   opts - Pointer to ptrace options structure
 *
 * Returns:
 *   -1 (0xFF) if inherit flag is set
 *   0 if inherit flag is not set
 *
 * Original address: 0x00e5b174
 */
int8_t XPD_$INHERIT_PTRACE_OPTIONS(xpd_$ptrace_opts_t *opts);

/*
 * ============================================================================
 * Target Control
 * ============================================================================
 */

/*
 * XPD_$RESTART - Restart a suspended debug target
 *
 * Resumes execution of a suspended debug target with specified mode.
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   mode       - Restart mode (1=continue, 2=step with trace, 3=step no trace)
 *   pc         - New PC value (or 1 to keep current)
 *   signal     - Signal to deliver (or current if unchanged)
 *   status     - New status code (or 0 to keep current)
 *   status_ret - Status return
 *
 * Original address: 0x00e5b54a
 */
void XPD_$RESTART(uid_t *proc_uid, uint16_t *mode, int32_t *pc,
                  int16_t *signal, int32_t *status, status_$t *status_ret);

/*
 * XPD_$CONTINUE_PROC - Continue a debug target
 *
 * Resumes execution of a debug target that is waiting.
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   response   - Response code for the target
 *   status_ret - Status return
 *
 * Original address: 0x00e5bed8
 */
void XPD_$CONTINUE_PROC(uid_t *proc_uid, int32_t response, status_$t *status_ret);

/*
 * XPD_$SET_ENABLE - Enable/disable debug events
 *
 * Enables or disables debug event delivery for a target process.
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   enable     - Enable flag (high bit set = enable)
 *   status_ret - Status return
 *
 * Original address: 0x00e5bf50
 */
void XPD_$SET_ENABLE(uid_t *proc_uid, int8_t *enable, status_$t *status_ret);

/*
 * ============================================================================
 * Fault and Event Handling
 * ============================================================================
 */

/*
 * XPD_$CAPTURE_FAULT - Capture a fault in a debug target
 *
 * Called when a fault occurs in a debug target to capture the
 * fault state and notify the debugger.
 *
 * Parameters:
 *   context     - Pointer to fault context
 *   frame       - Pointer to exception frame
 *   signal      - Pointer to signal number (updated on return)
 *   status_ret  - Status of fault (updated on return)
 *
 * Original address: 0x00e5b1ee
 */
void XPD_$CAPTURE_FAULT(void *context, int32_t *frame, uint16_t *signal, status_$t *status_ret);

/*
 * XPD_$POST_EVENT - Post an event to the debugger
 *
 * Posts an event notification to the debugger process.
 *
 * Parameters:
 *   event_type - Pointer to event type
 *   event_data - Pointer to event data
 *   result     - Pointer to receive result code
 *
 * Original address: 0x00e75090
 */
void XPD_$POST_EVENT(int32_t *event_type, void *event_data, uint16_t *result);

/*
 * XPD_$GET_EVENT_AND_DATA - Get pending event from target
 *
 * Retrieves a pending debug event from any target of the current
 * debugger process.
 *
 * Parameters:
 *   proc_uid   - Receives UID of target with event (or NIL if none)
 *   event_type - Receives event type
 *   status_ret - Receives event status
 *
 * Original address: 0x00e5be28
 */
void XPD_$GET_EVENT_AND_DATA(uid_t *proc_uid, uint16_t *event_type, status_$t *status_ret);

/*
 * XPD_$GET_EC - Get eventcount for debug notifications
 *
 * Returns an eventcount that advances when a debug event occurs.
 *
 * Parameters:
 *   key        - Key value (must be 0)
 *   ec_ret     - Receives eventcount pointer
 *   status_ret - Status return
 *
 * Original address: 0x00e5bdc2
 */
void XPD_$GET_EC(int16_t *key, void **ec_ret, status_$t *status_ret);

/*
 * ============================================================================
 * Memory Access
 * ============================================================================
 */

/*
 * XPD_$READ_PROC - Read target process memory
 *
 * Reads memory from a debug target's address space.
 * Target must be suspended.
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   addr       - Address in target to read from
 *   len        - Pointer to length to read
 *   buffer     - Pointer to buffer to receive data
 *   status_ret - Status return
 *
 * Original address: 0x00e5b954
 */
void XPD_$READ_PROC(uid_t *proc_uid, void *addr, int32_t *len, void *buffer, status_$t *status_ret);

/*
 * XPD_$READ_PROC_ASYNC - Read target process memory (async check)
 *
 * Like READ_PROC but checks debug permissions first.
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   addr       - Address in target to read from
 *   len        - Pointer to length to read
 *   buffer     - Pointer to buffer to receive data
 *   status_ret - Status return
 *
 * Original address: 0x00e5b88e
 */
void XPD_$READ_PROC_ASYNC(uid_t *proc_uid, void *addr, int32_t *len,
                          void *buffer, status_$t *status_ret);

/*
 * XPD_$WRITE_PROC - Write target process memory
 *
 * Writes memory to a debug target's address space.
 * Target must be suspended.
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   addr       - Address in target to write to
 *   data       - Pointer to data to write
 *   buffer     - Pointer to buffer with length
 *   status_ret - Status return
 *
 * Original address: 0x00e5b9e2
 */
void XPD_$WRITE_PROC(uid_t *proc_uid, void *addr, int32_t *data, void *buffer, status_$t *status_ret);

/*
 * XPD_$READ - Read from address space by ASID
 *
 * Reads from an arbitrary address space given its ASID.
 *
 * Parameters:
 *   asid       - Pointer to ASID
 *   addr       - Address to read from
 *   len        - Pointer to length
 *   buffer     - Pointer to buffer
 *   status_ret - Status return
 *
 * Original address: 0x00e5ba70
 */
void XPD_$READ(uint16_t *asid, void *addr, int32_t *len, void *buffer, status_$t *status_ret);

/*
 * XPD_$WRITE - Write to address space by ASID
 *
 * Writes to an arbitrary address space given its ASID.
 *
 * Parameters:
 *   asid       - Pointer to ASID
 *   addr       - Address to write to
 *   len        - Pointer to length
 *   buffer     - Pointer to buffer
 *   status_ret - Status return
 *
 * Original address: 0x00e5baa6
 */
void XPD_$WRITE(uint16_t *asid, void *addr, int32_t *len, void *buffer, status_$t *status_ret);

/*
 * ============================================================================
 * Register Access
 * ============================================================================
 */

/*
 * XPD_$GET_REGISTERS - Get target process registers
 *
 * Retrieves register state from a suspended debug target.
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   mode       - Register mode (0=general, 1=exception, 2=FP, 3=debug)
 *   regs       - Pointer to receive register data
 *   status_ret - Status return
 *
 * Original address: 0x00e5c1a4
 */
void XPD_$GET_REGISTERS(uid_t *proc_uid, int16_t *mode, void *regs, status_$t *status_ret);

/*
 * XPD_$PUT_REGISTERS - Set target process registers
 *
 * Sets register state in a suspended debug target.
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   mode       - Register mode (0=general, 1=exception, 2=FP)
 *   regs       - Pointer to register data
 *   status_ret - Status return
 *
 * Original address: 0x00e5c33c
 */
void XPD_$PUT_REGISTERS(uid_t *proc_uid, int16_t *mode, void *regs, status_$t *status_ret);

/*
 * XPD_$GET_FP - Get floating-point registers for target
 *
 * Retrieves floating-point state from a suspended debug target.
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   status_ret - Status return
 *
 * Original address: 0x00e5bffc
 */
void XPD_$GET_FP(uid_t *proc_uid, status_$t *status_ret);

/*
 * XPD_$PUT_FP - Set floating-point registers for target
 *
 * Sets floating-point state in a suspended debug target.
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   status_ret - Status return
 *
 * Original address: 0x00e5c094
 */
void XPD_$PUT_FP(uid_t *proc_uid, status_$t *status_ret);

/*
 * XPD_$GET_TARGET_INFO - Get target debug info
 *
 * Retrieves debug status flags for a target process.
 *
 * Parameters:
 *   proc_uid     - UID of target process
 *   is_target    - Receives target flag (-1 if valid target, 0 otherwise)
 *   is_suspended - Receives suspended flag (-1 if suspended, 0 otherwise)
 *   status_ret   - Status return
 *
 * Original address: 0x00e5c12c
 */
void XPD_$GET_TARGET_INFO(uid_t *proc_uid, int8_t *is_target, int8_t *is_suspended,
                          status_$t *status_ret);

/*
 * ============================================================================
 * Internal Functions
 * ============================================================================
 */

/*
 * XPD_$FIND_INDEX - Find target process index (internal)
 *
 * Validates that the caller is the debugger for the target and
 * that the target is suspended.
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   status_ret - Status return
 *
 * Returns:
 *   Process table index, or 0 on error
 *
 * Original address: 0x00e5af38
 */
int16_t XPD_$FIND_INDEX(uid_t *proc_uid, status_$t *status_ret);

/*
 * XPD_$FIND_DEBUGGER_INDEX - Find debugger slot index (internal)
 *
 * Finds the debugger table slot for the given ASID.
 *
 * Parameters:
 *   asid       - Address space ID to search for
 *   status_ret - Status return
 *
 * Returns:
 *   Debugger table index (1-6), or 0 if not found
 *
 * Original address: 0x00e5badc
 */
int16_t XPD_$FIND_DEBUGGER_INDEX(int16_t asid, status_$t *status_ret);

/*
 * XPD_$REGISTER_DEBUGGER - Register as debugger (internal)
 *
 * Allocates a debugger table slot for the given ASID.
 *
 * Parameters:
 *   asid       - Address space ID to register
 *   status_ret - Status return
 *
 * Returns:
 *   Debugger table index (1-6), or 0 if table full
 *
 * Original address: 0x00e5bb1e
 */
int16_t XPD_$REGISTER_DEBUGGER(int16_t asid, status_$t *status_ret);

/*
 * XPD_$UNREGISTER_DEBUGGER - Unregister as debugger (internal)
 *
 * Releases a debugger table slot and continues all targets.
 *
 * Parameters:
 *   asid       - Address space ID to unregister
 *   status_ret - Status return
 *
 * Original address: 0x00e74f7c
 */
void XPD_$UNREGISTER_DEBUGGER(int16_t asid, status_$t *status_ret);

/*
 * XPD_$COPY_MEMORY - Copy memory between address spaces (internal)
 *
 * Copies data between two address spaces in chunks using a
 * temporary buffer. Handles guard faults.
 *
 * Parameters:
 *   dst_asid   - Destination address space ID
 *   dst_addr   - Destination address
 *   src_asid   - Source address space ID
 *   src_addr   - Source address
 *   len        - Number of bytes to copy
 *   status_ret - Status return
 *
 * Original address: 0x00e5b704
 */
void XPD_$COPY_MEMORY(int16_t dst_asid, void *dst_addr, int16_t src_asid,
                      void *src_addr, uint32_t len, status_$t *status_ret);

/*
 * XPD_$FP_GET_STATE - Get floating-point state (internal)
 *
 * Saves the floating-point state to a buffer.
 *
 * Parameters:
 *   fp_buf     - Pointer to FP save buffer
 *   fp_format  - Pointer to FP format info
 *
 * Original address: 0x00e5c50e
 */
void XPD_$FP_GET_STATE(void *fp_buf, void *fp_format);

/*
 * XPD_$FP_PUT_STATE - Set floating-point state (internal)
 *
 * Restores the floating-point state from a buffer.
 *
 * Parameters:
 *   fp_buf     - Pointer to FP save buffer
 *   fp_format  - Pointer to FP format info
 *
 * Original address: 0x00e5c4d0
 */
void XPD_$FP_PUT_STATE(void *fp_buf, void *fp_format);

/*
 * XPD_$GET_FP_INT - Get FP registers internal helper
 *
 * Original address: 0x00e5c55a
 */
void XPD_$GET_FP_INT(int16_t *asid, status_$t *status_ret);

/*
 * XPD_$PUT_FP_INT - Set FP registers internal helper
 *
 * Original address: 0x00e5c58e
 */
void XPD_$PUT_FP_INT(int16_t *asid, status_$t *status_ret);

#endif /* XPD_H */
