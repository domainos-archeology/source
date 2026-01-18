/*
 * FIM - Fault/Interrupt Manager Module
 *
 * Provides fault and interrupt handling for Domain/OS.
 * Manages CPU exceptions, signal delivery, cleanup handlers,
 * floating point state, and quit processing.
 *
 * This module is heavily architecture-specific (m68k/68010+)
 * with most functions implemented in assembly.
 */

#ifndef FIM_H
#define FIM_H

#include "base/base.h"
#include "ec/ec.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Lock IDs used by FIM */
#define FIM_WIRED_LOCK_ID       0x0D
#define FIM_UNWIRED_LOCK_ID     0x03

/* Delivery frame flags (offset 0x4E in delivery frame) */
#define FIM_DF_FLAG_NEGATIVE      0x80    /* Bit 7: Negative status */
#define FIM_DF_FLAG_SUPERVISOR    0x40    /* Bit 6: Was in supervisor mode */
#define FIM_DF_FLAG_TRACE         0x20    /* Bit 5: Trace fault */
#define FIM_DF_FLAG_BUS_ERROR     0x10    /* Bit 4: Bus error */
#define FIM_DF_FLAG_RESTORE_FP    0x08    /* Bit 3: Need to restore FP state */
#define FIM_DF_FLAG_IN_FIM        0x04    /* Bit 2: In FIM (always set) */
#define FIM_DF_FLAG_FP_SAVED      0x02    /* Bit 1: FP state saved */
#define FIM_DF_FLAG_CLEANUP_RAN   0x01    /* Bit 0: Cleanup handler ran */

/* Exception vector format codes (m68010+) */
#define FIM_FRAME_FORMAT_SHORT  0x0     /* 4-word frame */
#define FIM_FRAME_FORMAT_THROW  0x1     /* Throwaway frame */
#define FIM_FRAME_FORMAT_INSTR  0x2     /* Instruction exception */
#define FIM_FRAME_FORMAT_COPROC 0x9     /* Coprocessor mid-instruction */
#define FIM_FRAME_FORMAT_SHORT_BUS 0xA  /* Short bus cycle fault */
#define FIM_FRAME_FORMAT_LONG_BUS  0xB  /* Long bus cycle fault */

/* FP save area types */
#define FIM_FP_TYPE_NULL        0       /* Null state (no FP context) */
#define FIM_FP_TYPE_IDLE        1       /* Idle state */
#define FIM_FP_TYPE_BUSY        2       /* Busy state */

/*
 * ============================================================================
 * Data Structures
 * ============================================================================
 */

/*
 * m68010 exception frame - varies by format
 * Format code is in bits 15:12 of the status register extension word
 */
typedef struct fim_exception_frame_t {
    uint16_t    sr;             /* 0x00: Status register */
    uint32_t    pc;             /* 0x02: Program counter */
    uint16_t    format_vector;  /* 0x06: Format code (bits 15:12) and vector (bits 11:0) */
    /* Additional words depend on format code - see frame tables */
} fim_exception_frame_t;

/*
 * Long bus cycle fault frame (format 0xB, 68010)
 * This is the largest exception frame format
 */
typedef struct fim_long_bus_frame_t {
    uint16_t    sr;             /* 0x00: Status register */
    uint32_t    pc;             /* 0x02: Program counter */
    uint16_t    format_vector;  /* 0x06: Format/vector word */
    uint16_t    ssw;            /* 0x08: Special status word */
    /* Additional fields for address, data, etc. */
} fim_long_bus_frame_t;

/*
 * FIM delivery frame - created on user stack for fault delivery
 * Total size: 0x6A bytes
 *
 * This structure is built by FIM_$BUILD_DF and contains all the
 * context needed to deliver a fault to user mode.
 */
typedef struct fim_delivery_frame_t {
    uint16_t    magic;          /* 0x00: Magic number 0xDFDF */
    uint32_t    status;         /* 0x02: Fault status code */
    /* 0x06-0x41: Saved registers D0-D7, A0-A6 (15 longs = 60 bytes) */
    uint32_t    regs[15];       /* D0-D7, A0-A5, A6 */
    uint32_t    pc;             /* 0x42: Saved PC */
    uint32_t    fault_info1;    /* 0x46: Fault-specific info */
    uint32_t    fault_info2;    /* 0x4A: Fault-specific info */
    uint8_t     flags;          /* 0x4E: Delivery frame flags */
    uint8_t     version;        /* 0x4F: Frame version (2) */
    uint32_t    reserved1;      /* 0x50: Reserved */
    uint16_t    orig_sr;        /* 0x54: Original SR from exception */
    uint32_t    orig_pc;        /* 0x56: Original PC from exception */
    uint16_t    orig_sr2;       /* 0x5A: User SR */
    uint32_t    fp_save_ptr;    /* 0x5C: Pointer to FP state (or 0) */
    uint16_t    param3;         /* 0x60: Signal parameter 3 */
    uint32_t    param4;         /* 0x62: Signal parameter 4 */
    uint32_t    user_pc;        /* 0x66: User program counter */
} fim_delivery_frame_t;

/*
 * FIM per-process cleanup handler entry
 * Stored in a stack per address space
 */
typedef struct fim_cleanup_entry_t {
    struct fim_cleanup_entry_t *next;   /* Link to previous handler */
    void        *handler;       /* Handler function */
    void        *context;       /* Handler context */
} fim_cleanup_entry_t;

/*
 * Register save area used by various FIM functions
 */
typedef struct fim_regs_t {
    uint32_t    d[8];           /* D0-D7 */
    uint32_t    a[7];           /* A0-A6 */
    uint32_t    usp;            /* User stack pointer */
} fim_regs_t;

/*
 * ============================================================================
 * Global Data
 * ============================================================================
 */

/*
 * FIM_$QUIT_VALUE - Quit value array indexed by address space ID
 *
 * Each address space has a quit value that indicates whether
 * a quit (SIGQUIT) has been requested for processes in that AS.
 */
extern uint32_t FIM_$QUIT_VALUE[];

/*
 * FIM_$QUIT_EC - Quit event count array indexed by address space ID
 *
 * Each address space has an event count for quit signaling.
 * When a quit is requested, the corresponding EC is advanced.
 */
extern void *FIM_$QUIT_EC[];

/*
 * FIM_IN_FIM - Per-AS flag indicating FIM is handling a fault
 * Indexed by PROC1_$AS_ID
 * Values: 0 = not in FIM, 0xFF = in FIM, negative = FIM blocked
 */
extern int8_t FIM_IN_FIM[];

/*
 * FIM_$USER_FIM_ADDR - Per-AS user-mode FIM handler address
 * Indexed by PROC1_$AS_ID << 2
 */
extern void *FIM_$USER_FIM_ADDR[];

/*
 * FIM_$COLD_BUS_ERR - Bus error handler for cold boot
 * Address 0x00e35004
 */
extern void FIM_$COLD_BUS_ERR(void);

/*
 * Exception frame size table (12 entries for format codes 0-11)
 * Gives the size in bytes of each exception frame format
 */
extern uint8_t FIM_FRAME_SIZE_TABLE[];

/*
 * ============================================================================
 * Function Prototypes - C Functions
 * ============================================================================
 */

/*
 * FIM_$BUILD_DF - Build a delivery frame for fault delivery
 *
 * This is the main fault handling function that:
 * 1. Extracts fault information from exception frame
 * 2. Checks if user FIM handler exists
 * 3. Saves FPU state if needed
 * 4. Builds delivery frame on user stack
 * 5. Sets up return to user FIM handler
 *
 * Parameters:
 *   exception_frame - Pointer to m68k exception frame
 *   return_pc - PC to use for return
 *   regs - Saved register set
 *   flags - Fault flags
 *   signal_params - Signal parameters
 *   status - Fault status code
 *   result - Output: delivery frame pointers
 *
 * Returns:
 *   0xFF if fault delivered to user, 0 if handled locally
 */
uint8_t FIM_$BUILD_DF(void *exception_frame, uint32_t return_pc,
                      fim_regs_t *regs, uint16_t flags,
                      uint16_t signal_param, uint32_t status,
                      void **result);

/*
 * ============================================================================
 * Function Prototypes - Assembly Functions
 * ============================================================================
 */

/*
 * FIM_$EXIT - Return from exception
 *
 * Simply executes RTE instruction.
 * Address: 0x00e228bc (2 bytes)
 */
void FIM_$EXIT(void);

/*
 * FIM_$UII - Unimplemented Instruction Interrupt handler
 *
 * Handles illegal/unimplemented instruction traps.
 * Address: 0x00e21326 (38 bytes)
 */
void FIM_$UII(void);

/*
 * FIM_$GENERATE - Generate a fault
 *
 * Small stub for fault generation.
 * Address: 0x00e216cc (6 bytes)
 */
void FIM_$GENERATE(void *context);

/*
 * FIM_$PRIV_VIOL - Privilege violation handler
 *
 * Handles privilege violation exceptions (user mode trying
 * to execute supervisor-only instructions).
 * Address: 0x00e212d8 (74 bytes)
 */
void FIM_$PRIV_VIOL(void);

/*
 * FIM_$ILLEGAL_USP - Illegal USP handler
 *
 * Handles invalid user stack pointer situations.
 * Address: 0x00e216d2 (4 bytes)
 */
void FIM_$ILLEGAL_USP(void);

/*
 * FIM_$CLEANUP - Set up cleanup handler
 *
 * Establishes a cleanup handler for the current process.
 * Similar to setjmp - returns status_$cleanup_handler_set initially,
 * then a different status when cleanup is triggered.
 *
 * Parameters:
 *   handler - Cleanup handler context (receives jmp_buf-like data)
 *
 * Returns:
 *   status_$cleanup_handler_set on initial call
 *   Error status when cleanup triggered
 *
 * Address: 0x00e21634 (40 bytes)
 */
status_$t FIM_$CLEANUP(void *handler);

/*
 * FIM_$RLS_CLEANUP - Release cleanup handler
 *
 * Removes the most recently established cleanup handler.
 * Restores the cleanup context from the provided buffer.
 *
 * Parameters:
 *   cleanup_data - Pointer to cleanup context buffer
 *
 * Address: 0x00e2165c (22 bytes)
 */
void FIM_$RLS_CLEANUP(void *cleanup_data);

/*
 * FIM_$POP_SIGNAL - Pop signal from handler stack
 *
 * Restores the stack pointer from the cleanup context and
 * returns to the caller.
 *
 * Parameters:
 *   cleanup_data - Pointer to cleanup context buffer
 *
 * Address: 0x00e21672 (12 bytes)
 */
void FIM_$POP_SIGNAL(void *cleanup_data);

/*
 * FIM_$SIGNAL_FIRST - Signal first handler
 *
 * Invokes cleanup handler without unwinding.
 *
 * Parameters:
 *   status - Status to deliver
 *
 * Address: 0x00e2167e (10 bytes)
 */
void FIM_$SIGNAL_FIRST(status_$t status);

/*
 * FIM_$SIGNAL - Signal cleanup handlers
 *
 * Invokes the cleanup handler chain, similar to longjmp.
 * Does not return if a handler is established.
 *
 * Parameters:
 *   status - Status to deliver to handler
 *
 * Address: 0x00e21688 (42 bytes)
 */
void FIM_$SIGNAL(status_$t status);

/*
 * FIM_$PROC2_STARTUP - Process 2 startup entry point
 *
 * Entry point for new process startup after fork.
 * Address: 0x00e21736 (30 bytes)
 */
void FIM_$PROC2_STARTUP(void);

/*
 * FIM_$SINGLE_STEP - Single step exception handler
 *
 * Handles trace exceptions for single-step debugging.
 * Address: 0x00e21754 (80 bytes)
 */
void FIM_$SINGLE_STEP(void);

/*
 * FIM_$FAULT_RETURN - Return from fault handler
 *
 * Restores state and returns from user fault handler.
 * Address: 0x00e217a4 (80 bytes)
 */
void FIM_$FAULT_RETURN(void);

/*
 * FIM_$FP_ABORT - Floating point abort handler
 *
 * Handles floating point exception aborts.
 * Address: 0x00e21b80 (48 bytes)
 */
void FIM_$FP_ABORT(void);

/*
 * FIM_$FP_INIT - Initialize floating point state
 *
 * Initializes the 68881/68882 FPU for a process.
 * Address: 0x00e21bb0 (84 bytes)
 */
void FIM_$FP_INIT(void);

/*
 * FIM_$FSAVE - Save floating point state
 *
 * Saves 68881/68882 state using FSAVE instruction.
 *
 * Parameters:
 *   status - Output: 0 if no FP context, non-zero if saved
 *   sp_ptr - Pointer to stack pointer (modified)
 *   type - FP save type
 *   unused - Unused parameter
 *
 * Address: 0x00e21c34 (160 bytes)
 */
void FIM_$FSAVE(int16_t *status, uint32_t *sp_ptr, uint16_t type, uint8_t unused);

/*
 * FIM_$FRESTORE - Restore floating point state
 *
 * Restores 68881/68882 state using FRESTORE instruction.
 *
 * Parameters:
 *   state_ptr - Pointer to saved FP state
 *
 * Address: 0x00e21cd4 (116 bytes)
 */
void FIM_$FRESTORE(void *state_ptr);

/*
 * FIM_$FP_GET_STATE - Get floating point state
 *
 * Gets the complete 68881/68882 state including registers.
 *
 * Parameters:
 *   state - Output buffer for FP state
 *   status - Status return
 *
 * Address: 0x00e21d48 (196 bytes)
 */
void FIM_$FP_GET_STATE(void *state, status_$t *status);

/*
 * FIM_$FP_PUT_STATE - Put floating point state
 *
 * Sets the complete 68881/68882 state including registers.
 *
 * Parameters:
 *   state - FP state to restore
 *   status - Status return
 *
 * Address: 0x00e21e0c (152 bytes)
 */
void FIM_$FP_PUT_STATE(void *state, status_$t *status);

/*
 * FIM_$SPURIOUS_INT - Spurious interrupt handler
 *
 * Handles spurious interrupts (no device acknowledged).
 * Address: 0x00e21ea4 (86 bytes)
 */
void FIM_$SPURIOUS_INT(void);

/*
 * FIM_$PARITY_TRAP - Parity error trap handler
 *
 * Handles memory parity errors.
 * Address: 0x00e21efa (98 bytes)
 */
void FIM_$PARITY_TRAP(void);

/*
 * FIM_$GET_USER_SR_PTR - Get pointer to user SR in exception frame
 *
 * Returns pointer to the SR word in an exception frame for a process.
 *
 * Parameters:
 *   process - Process ID
 *   unused - Unused parameters (D2 value)
 *
 * Returns:
 *   Pointer to SR word
 *
 * Address: 0x00e2277c (118 bytes)
 */
void *FIM_$GET_USER_SR_PTR(uint16_t process, uint32_t unused);

/*
 * FIM_$DELIVER_TRACE_FAULT - Deliver trace fault to process
 *
 * Marks the specified address space for receiving a trace fault.
 * Sets the trace bit and increments the pending trace faults counter.
 *
 * Parameters:
 *   as_id - Address space ID to deliver trace fault to
 *
 * Address: 0x00e22866 (42 bytes)
 */
void FIM_$DELIVER_TRACE_FAULT(int16_t as_id);

/*
 * FIM_$CLEAR_TRACE_FAULT - Clear trace fault state
 *
 * Address: 0x00e2281c (44 bytes)
 */
void FIM_$CLEAR_TRACE_FAULT(void);

/*
 * FIM_$CRASH - System crash handler
 *
 * Called when a fault cannot be delivered or is fatal.
 * Displays crash information and invokes CRASH_SYSTEM.
 *
 * Parameters:
 *   exception_frame - Pointer to exception frame
 *   regs - Saved register set
 *
 * Address: 0x00e1e864 (158 bytes)
 */
void FIM_$CRASH(void *exception_frame, fim_regs_t *regs);

#endif /* FIM_H */
