/*
 * fim/fim_data.c - FIM Global Data
 *
 * Global variables for the Fault/Interrupt Manager subsystem.
 *
 * Memory map (relative to FIM_DATA_BASE = 0x00E2126C):
 *   0x000-0x039: FIM_IN_FIM[] - Per-AS "in FIM" flags
 *   0x03C-0x123: FIM_$USER_FIM_ADDR[] - Per-AS user FIM handlers
 *   0x124-0x133: FIM_FRAME_SIZE_TABLE[] - Exception frame sizes
 *   0x446-0x549: FIM_CLEANUP_STACK[] - Per-process cleanup handler stack heads
 *   0x624-0x65D: FIM_$TRACE_BIT[] - Per-AS pending trace fault bit
 *                                   (defined in fim/sau2/fim.s, not here)
 *   0x6EE-0x6F1: FIM_$SPUR_CNT - Spurious interrupt count
 *
 * Every per-AS table here holds FIM_AS_COUNT (58) elements; see the
 * derivation of that count beside its definition in fim/fim.h.
 * FIM_CLEANUP_STACK is the one table here that is not per-AS: it is indexed
 * by PROC1_$CURRENT and holds PROC1_MAX_PROCESSES (65) elements.
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
 * Address: 0x00E2126C (0x00E2126C + 58 = 0x00E212A6, then 2 bytes of
 * alignment padding before FIM_$USER_FIM_ADDR)
 */
int8_t FIM_IN_FIM[FIM_AS_COUNT];

/*
 * FIM_$USER_FIM_ADDR - Per-AS user-mode FIM handler
 *
 * If non-NULL, faults are delivered to this address in user mode.
 * If NULL, faults cause a crash.
 *
 * Indexed by PROC1_$AS_ID (multiply by 4 for byte offset)
 * Address: 0x00E212A8 (0x00E2126C + 0x3C); 0x00E212A8 + 58 * 4 = 0x00E21390,
 * the address of FIM_FRAME_SIZE_TABLE below.
 */
void *FIM_$USER_FIM_ADDR[FIM_AS_COUNT];

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
 * FIM_CLEANUP_STACK - Cleanup handler stack heads, one per process
 *
 * Each entry is the head of that process's cleanup-handler chain (a
 * fim_cleanup_entry_t built on the process's own stack by FIM_$CLEANUP), or
 * NULL when no handler is established.
 *
 * Element size and index: every instruction that reaches this table does the
 * same three things -- load the current process id, scale it by four, and
 * add it to the table base as a signed word index.  All three sites are in
 * the cleanup/signal group and all three materialise the same base:
 *
 *   FIM_$CLEANUP     0x00E21634  move.w (0x00E20608).l,D0w  ; PROC1_$CURRENT
 *                    0x00E2163E  lsl.w  #0x2,D0w
 *                    0x00E21640  lea    (0x70,PC),A0        ; 0x00E21642+0x70
 *                    0x00E21644  adda.w D0w,A0
 *                    0x00E21648  move.l (A0),D0             ; read head
 *                    0x00E21652  move.l A1,(A0)             ; push new entry
 *   FIM_$RLS_CLEANUP 0x00E21668  lea    (0x48,PC),A0        ; 0x00E2166A+0x48
 *                    0x00E2166E  move.l (A1),(A0)           ; pop
 *   FIM_$SIGNAL      0x00E21692  lea    (0x1e,PC),A0        ; 0x00E21694+0x1e
 *                    0x00E2169A  move.l (A0),D1             ; read head
 *                    0x00E216A0  move.l (A1)+,(A0)          ; pop
 *
 * All three PC-relative displacements resolve to 0x00E216B2, and every
 * access is a longword at that base plus PROC1_$CURRENT * 4, so the element
 * size is 4.
 *
 * Element count: the object runs from 0x00E216B2 to the next object in the
 * image, FIM_$PROC2_STARTUP at 0x00E217B6 (everything in between reads back
 * as zero with "gsk read 0x00E216B2 260").  That is 0x104 = 260 bytes = 65
 * longwords, which is exactly PROC1_MAX_PROCESSES -- the same count as
 * PCBS[], PROC1_$TYPE[] and OS_STACK_BASE[], the other tables indexed by a
 * PROC1 process id.  (This file previously carried a guessed 64.)
 *
 * The original performs no bounds check on the index.
 *
 * Address: 0x00E216B2
 *
 * TODO(source-z5bf, 0x00E216B2): fim/sau2/fim.s reserves the same 260 bytes
 * under the private label cleanup_stack_table so that the objects around it
 * keep their image spacing, and FIM_$CLEANUP / FIM_$RLS_CLEANUP /
 * FIM_$SIGNAL push and pop that copy rather than this symbol.
 */
void *FIM_CLEANUP_STACK[PROC1_MAX_PROCESSES];

/*
 * FIM_$QUIT_VALUE - Per-AS quit value
 *
 * Non-zero if a quit has been requested for the AS.
 * Checked by user-mode code to handle SIGQUIT.  Written by FIM_$INIT_ASID
 * (0x00E0AA24) and FIM_$ACKNOWLEDGE (0x00E0A96C), both of which copy the
 * head longword (the value) of FIM_$QUIT_EC[as].
 *
 * Address: 0x00E222BA; 0x00E222BA + 58 * 4 = 0x00E223A2, the address of
 * FIM_$TRACE_STS below.
 */
uint32_t FIM_$QUIT_VALUE[FIM_AS_COUNT];

/*
 * FIM_$QUIT_EC - Per-AS quit event count
 *
 * Event count that is advanced when a quit is requested.
 * User-mode code waits on this to detect quit requests.
 */
/* NOTE: Each ec_$eventcount_t is 12 bytes. This array is accessed with
 * indices like as_id * 3 (to account for 12-byte stride in 4-byte units).
 * The actual layout is 58 * 12 = 696 bytes.
 *
 * Address: 0x00E22002; 0x00E22002 + 696 = 0x00E222BA, the address of
 * FIM_$QUIT_VALUE above.
 */
ec_$eventcount_t FIM_$QUIT_EC[FIM_AS_COUNT];

/*
 * FIM_$QUIT_INH - Per-AS quit inhibit flag
 *
 * Non-zero if quit delivery is inhibited for the AS.
 * Set during single-step debugging, and by FIM_$INIT_ASID (0x00E0AA24) and
 * FIM_$FREE_ASID (0x00E0AA6C).
 *
 * Address: 0x00E2248A; 0x00E2248A + 58 = 0x00E224C4, the address of the
 * signal-delivery eventcount array FIM_$ACKNOWLEDGE advances.
 */
int8_t FIM_$QUIT_INH[FIM_AS_COUNT];

/*
 * FIM_$INITIAL_STACK_SIZE - Bytes reserved above a new process's startup
 * context on its initial stack (used by PROC2_$CREATE / PROC2_$FORK).
 * Original address: 0x00E21824 (4 bytes; image value 0x00000008)
 */
uint32_t FIM_$INITIAL_STACK_SIZE = 8;

/*
 * FIM_$TRACE_STS - Per-AS trace fault status
 *
 * Contains the status code for a pending trace fault.
 * Set by FIM_$SINGLE_STEP, checked by fault delivery.
 *
 * Address: 0x00E223A2; 0x00E223A2 + 58 * 4 = 0x00E2248A, the address of
 * FIM_$QUIT_INH above.
 */
status_$t FIM_$TRACE_STS[FIM_AS_COUNT];

/*
 * FIM_$TRACE_BIT - Per-AS trace bit
 *
 * Bit 7 set if trace fault pending for the AS.
 * Used to coordinate trace fault delivery.
 *
 * Address: 0x00E21890.  FIM_$CLEAR_TRACE_FAULT (0x00E22890) opens with
 * "lea (-0x1002,PC),A1"; the PC value for that displacement is the address
 * of the extension word, 0x00E22892, giving A1 = 0x00E21890.  The same A1
 * then reaches FIM_$PENDING_TRACE_FAULTS at (0x76E,A1) = 0x00E21FFE and
 * FIM_$EXIT at (0x102C,A1) = 0x00E228BC, which is where FIM_$EXIT is.
 * 0x00E21890 + 58 = 0x00E218CA, the address of JMP_TO_BUS_ERR.
 *
 * NOT DEFINED HERE.  This table is the first object of the module's wired
 * data area, which the image places in the middle of the FIM code region --
 * between FIM_$SETUP_RETURN (ends 0x00E2188E) and JMP_TO_BUS_ERR -- so it is
 * emitted with the surrounding bytes in fim/sau2/fim.s.  fim/fim.h declares
 * it; the extent assertion for it lives beside the definition there.
 */

/*
 * FIM_$PENDING_TRACE_FAULTS - Count of pending trace faults
 *
 * Number of processes with pending trace faults.
 * When non-zero, FIM_$EXIT is patched to NOP to allow
 * trace fault delivery.
 *
 * Address: 0x00E21FFE ((0x76E,A1) in FIM_$CLEAR_TRACE_FAULT, see
 * FIM_$TRACE_BIT above); 0x00E21FFE + 4 = 0x00E22002, the address of
 * FIM_$QUIT_EC above.
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

/*
 * ============================================================================
 * Per-AS table extents
 * ============================================================================
 *
 * Each per-AS table's element count is pinned by the address of the object
 * that follows it in the image (see the derivation beside FIM_AS_COUNT in
 * fim/fim.h).  These assertions restate that arithmetic so a change to
 * FIM_AS_COUNT or to ec_$eventcount_t cannot silently resize a table.
 *
 * Only checked for the m68k build: on a 64-bit host ec_$eventcount_t's two
 * waiter pointers are 8 bytes each, so FIM_$QUIT_EC is not 696 bytes there.
 */
#if defined(ARCH_M68K)
_Static_assert(sizeof(FIM_IN_FIM) == 0x00E212A6 - 0x00E2126C,
               "FIM_IN_FIM spans 0x00E2126C..0x00E212A5");
_Static_assert(sizeof(FIM_$USER_FIM_ADDR) == 0x00E21390 - 0x00E212A8,
               "FIM_$USER_FIM_ADDR ends where FIM_FRAME_SIZE_TABLE begins");
_Static_assert(sizeof(FIM_CLEANUP_STACK) == 0x00E217B6 - 0x00E216B2,
               "FIM_CLEANUP_STACK ends where FIM_$PROC2_STARTUP begins");
_Static_assert(sizeof(FIM_CLEANUP_STACK) / sizeof(FIM_CLEANUP_STACK[0])
               == PROC1_MAX_PROCESSES,
               "FIM_CLEANUP_STACK holds one longword per PROC1 process");
_Static_assert(sizeof(FIM_$QUIT_EC) == 0x00E222BA - 0x00E22002,
               "FIM_$QUIT_EC ends where FIM_$QUIT_VALUE begins");
_Static_assert(sizeof(FIM_$QUIT_VALUE) == 0x00E223A2 - 0x00E222BA,
               "FIM_$QUIT_VALUE ends where FIM_$TRACE_STS begins");
_Static_assert(sizeof(FIM_$TRACE_STS) == 0x00E2248A - 0x00E223A2,
               "FIM_$TRACE_STS ends where FIM_$QUIT_INH begins");
_Static_assert(sizeof(FIM_$QUIT_INH) == 0x00E224C4 - 0x00E2248A,
               "FIM_$QUIT_INH ends where the delivery eventcounts begin");
#endif
