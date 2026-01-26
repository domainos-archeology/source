/*
 * fim/fim_data.c - FIM Global Data
 *
 * Global variables for the Fault/Interrupt Manager subsystem.
 *
 * Memory map (relative to FIM_DATA_BASE = 0x00E2126C):
 *   0x000-0x0FF: FIM_IN_FIM[] - Per-AS "in FIM" flags
 *   0x03C-0x13B: FIM_$USER_FIM_ADDR[] - Per-AS user FIM handlers
 *   0x124-0x12F: FIM_FRAME_SIZE_TABLE[] - Exception frame sizes
 *   0x446-0x4C5: FIM_CLEANUP_STACK[] - Cleanup handler stack heads
 *   0x6EE-0x6F1: FIM_$SPUR_CNT - Spurious interrupt count
 */

#include "fim/fim_internal.h"

/*
 * FIM_IN_FIM - Per-AS flag indicating FIM is active
 *
 * Values:
 *   0x00 = Not in FIM
 *   0xFF = In FIM (set on entry, cleared on exit)
 *   < 0  = FIM blocked (negative = don't deliver to user)
 *
 * Indexed by PROC1_$AS_ID
 * Address: 0x00E2126C
 */
int8_t FIM_IN_FIM[64];

/*
 * FIM_$USER_FIM_ADDR - Per-AS user-mode FIM handler
 *
 * If non-NULL, faults are delivered to this address in user mode.
 * If NULL, faults cause a crash.
 *
 * Indexed by PROC1_$AS_ID (multiply by 4 for byte offset)
 * Address: 0x00E212A8 (0x00E2126C + 0x3C)
 */
void *FIM_$USER_FIM_ADDR[64];

/*
 * FIM_FRAME_SIZE_TABLE - Exception frame sizes by format code
 *
 * Maps m68010 exception frame format codes to sizes in bytes.
 * Format code is in bits 15:12 of the format/vector word.
 *
 * Format codes:
 *   0: Short frame (4 words = 8 bytes)
 *   1: Throwaway frame (4 words = 8 bytes)
 *   2: Instruction exception (6 words = 12 bytes)
 *   8: Bus error short (29 words = 58 bytes)
 *   9: Coprocessor mid-instruction (10 words = 20 bytes)
 *   A: Short bus cycle fault (16 words = 32 bytes)
 *   B: Long bus cycle fault (46 words = 92 bytes)
 *
 * Address: 0x00E21390
 */
uint8_t FIM_FRAME_SIZE_TABLE[16] = {
    8,      /* Format 0: Short frame */
    8,      /* Format 1: Throwaway frame */
    12,     /* Format 2: Instruction exception */
    12,     /* Format 3: Reserved */
    12,     /* Format 4: Reserved */
    12,     /* Format 5: Reserved */
    12,     /* Format 6: Reserved */
    12,     /* Format 7: Reserved */
    58,     /* Format 8: Bus error short (68010) */
    20,     /* Format 9: Coprocessor mid-instruction */
    32,     /* Format A: Short bus cycle fault */
    92,     /* Format B: Long bus cycle fault */
    12,     /* Format C: Reserved */
    12,     /* Format D: Reserved */
    12,     /* Format E: Reserved */
    12      /* Format F: Reserved */
};

/*
 * FIM_CLEANUP_STACK - Cleanup handler stack heads
 *
 * Each entry is a pointer to the head of the cleanup handler
 * linked list for an address space. NULL if no handlers.
 *
 * Indexed by PROC1_$CURRENT (multiply by 4)
 * Address: 0x00E216B2
 */
void *FIM_CLEANUP_STACK[64];

/*
 * FIM_$QUIT_VALUE - Per-AS quit value
 *
 * Non-zero if a quit has been requested for the AS.
 * Checked by user-mode code to handle SIGQUIT.
 */
uint32_t FIM_$QUIT_VALUE[64];

/*
 * FIM_$QUIT_EC - Per-AS quit event count
 *
 * Event count that is advanced when a quit is requested.
 * User-mode code waits on this to detect quit requests.
 */
/* NOTE: Each ec_$eventcount_t is 12 bytes. This array is accessed with
 * indices like as_id * 3 (to account for 12-byte stride in 4-byte units).
 * The actual layout is 64 * 12 = 768 bytes.
 */
ec_$eventcount_t FIM_$QUIT_EC[64];

/*
 * FIM_$QUIT_INH - Per-AS quit inhibit flag
 *
 * Non-zero if quit delivery is inhibited for the AS.
 * Set during single-step debugging.
 *
 * Address: 0x00E2248A
 */
int8_t FIM_$QUIT_INH[64];

/*
 * FIM_$TRACE_STS - Per-AS trace fault status
 *
 * Contains the status code for a pending trace fault.
 * Set by FIM_$SINGLE_STEP, checked by fault delivery.
 *
 * Address: 0x00E223A2
 */
status_$t FIM_$TRACE_STS[64];

/*
 * FIM_$TRACE_BIT - Per-AS trace bit
 *
 * Bit 7 set if trace fault pending for the AS.
 * Used to coordinate trace fault delivery.
 *
 * Address: 0x00E21888
 */
uint8_t FIM_$TRACE_BIT[64];

/*
 * FIM_$PENDING_TRACE_FAULTS - Count of pending trace faults
 *
 * Number of processes with pending trace faults.
 * When non-zero, FIM_$EXIT is patched to NOP to allow
 * trace fault delivery.
 *
 * Address: 0x00E21FF6
 */
uint32_t FIM_$PENDING_TRACE_FAULTS;

/*
 * FIM_$SPUR_CNT - Spurious interrupt count
 *
 * Total number of spurious interrupts received.
 * Used for diagnostics.
 *
 * Address: 0x00E21F7E (relative 0x6EE)
 */
uint32_t FIM_$SPUR_CNT;
