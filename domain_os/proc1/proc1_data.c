/*
 * proc1_data.c - PROC1 Global Data Definitions
 *
 * Two kinds of data (see proc1/proc1.h):
 *
 *   PROC1_$DATA   the PROC1_ module data block, A5 = 0xE254E8 (map
 *                 "D E254E8 PROC1_ size = CC4"): load averages, timeslice
 *                 timer elements, OS stack bases, per-pid statistics, the
 *                 stack allocator cells and the process type table.
 *
 *   PROC1_ASM     cells of the hand-written PROC1_ASM segment (0xE1EAC8..),
 *                 exported by name and reached absolutely or PC-relative,
 *                 never through A5; individual objects:
 *                   PROC1_$CURRENT_PCB      0xE1EAC8
 *                   PCBS                    0xE1EACC (65 pointers)
 *                   PROC1_$READY_COUNT      0xE1EBD0
 *                   PROC1_$READY_PCB        0xE1EC3A
 *                   PROC1_$TSVV             0xE205D2
 *                   PROC1_$SUSPEND_EC       0xE205F6
 *                   PROC1_$CURRENT          0xE20608
 *                   PROC1_$AS_ID            0xE2060A
 *                   PROC1_$ATOMIC_OP_DEPTH  0xE2060E
 */

#include "proc1/proc1_internal.h"

/*
 * Current process state
 */
proc1_t *PROC1_$CURRENT_PCB = NULL;     /* Pointer to current process's PCB */
proc1_t *PROC1_$READY_PCB = NULL;       /* Head of the ready list */
/*
 * 0xE20608, inside the PROC1_ASM code segment; reached PC-relative from
 * proc1_$process_exit_handler (proc1/sau2/init_stack.s).  See
 * PROC1_ASM_DATA_SECTION in proc1/proc1_internal.h (source-uwxz).
 */
uint16_t PROC1_$CURRENT PROC1_ASM_DATA_SECTION = 0; /* PID of current process */

/*
 * Ready list tracking
 */
uint16_t PROC1_$READY_COUNT = 0;        /* Number of processes in ready list */

/*
 * Atomic operation and address space state
 */
uint16_t PROC1_$ATOMIC_OP_DEPTH = 0;    /* Nesting depth of atomic operations */
uint16_t PROC1_$AS_ID = 0;              /* Current address space ID */

/*
 * Process Control Block (PCB) table, PCBS[0..64] indexed by pid
 *
 * Element pid at 0xE1EACC + pid*4 (PROC1_$BIND 0x00E14D96..0x00E14D9C, see
 * proc1/proc1.h).  PID allocation:
 *   0: Reserved/invalid
 *   1: System process
 *   2: Idle/init process
 *   3-64: User processes (on SAU2)
 */
proc1_t *PCBS[PROC1_MAX_PROCESSES] = { NULL };

/* Target only: the elements are pointers. */
#if defined(ARCH_M68K)
_Static_assert(sizeof(PCBS[0]) == 4, "PCBS stride 4 (lsl.w #0x2)");
_Static_assert(sizeof(PCBS) == 0xE1EBD0 - 0xE1EACC,
               "PCBS[0..64] ends at PROC1_$READY_COUNT");
#endif

/*
 * PROC1_$DATA - the PROC1_ module data block (layout, biases and asserts in
 * proc1/proc1.h).  Module data block PROC1_$DATA: Claude Opus 5.5
 * (source-l2yd).  A MODULE_DATA block linked in the SAU2 map's order after
 * PMAP_$DATA and before RING_$WIRED_DATA; the address is the ordering key,
 * not the link address.
 *
 * Image contents: `gsk read 0xE254E8 3268' is zero throughout - the load
 * averages, timer elements, stack cells and tables are all set at run time
 * (PROC1_$INIT 0x00E2F958, PROC1_$INIT_LOADAV, PROC1_$INIT_TS_TIMER,
 * PROC1_$BIND).
 */
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);

/*
 * ============================================================================
 * Timer Data
 * ============================================================================
 */

/*
 * Timeslice values indexed by state
 * Original address: 0xE205D2
 */
/* Image bytes at 0xE205D2 (gsk read): ffff x7, 7d00 x4, 30d4 x5, ffff x2.
 * Lives in the PROC1_ASM code segment (map: E205D2 PROC1_$TSVV, before
 * PROC1_$SUSPEND_EC E205F6), reached PC-relative by ADVANCE_INT's
 * `lea (PROC1_$TSVV:w,%pc),%a0` at 0xE2078C, hence PROC1_ASM_DATA_SECTION. */
int16_t PROC1_$TSVV[PROC1_TSVV_COUNT] PROC1_ASM_DATA_SECTION = {
    -1, -1, -1, -1, -1, -1, -1,
    0x7D00, 0x7D00, 0x7D00, 0x7D00,
    0x30D4, 0x30D4, 0x30D4, 0x30D4, 0x30D4,
    -1, -1
};
_Static_assert(sizeof(PROC1_$TSVV) == 0xE205F6 - 0xE205D2, "PROC1_$TSVV extent");

/*
 * ============================================================================
 * Event Count / Suspend Data
 * ============================================================================
 */

/*
 * Suspend event count - signaled when a process is suspended
 * Original address: 0xE205F6 (PROC1_ASM segment, between PROC1_$TSVV and
 * DI_$Q_HEAD).  Image bytes (gsk read 0xE205F6 12): 00000000 00e205f6
 * 00e205f6 - value 0 with both waiter links at itself, the empty-queue
 * state, spelled as the eventcount's own address like the other pre-linked
 * eventcounts in the tree (pmap/pmap_data.c, fim/fim_data.c).
 */
ec_$eventcount_t PROC1_$SUSPEND_EC = {
    .value = 0,
    .waiter_list_head = (ec_$eventcount_waiter_t *)&PROC1_$SUSPEND_EC,
    .waiter_list_tail = (ec_$eventcount_waiter_t *)&PROC1_$SUSPEND_EC,
};

/*
 * ============================================================================
 * Internal Timer Data
 * ============================================================================
 */

/*
 * PROC1_$VT_TIMER_DATA - the timer-index word at 0x00E14A06 (bytes 00 02);
 * see proc1/proc1_internal.h.
 */
const uint16_t PROC1_$VT_TIMER_DATA = 2;

/*
 * ============================================================================
 * Deferred-interrupt state and assembly-referenced constants
 * ============================================================================
 */

/*
 * DAT_00e20606 - "DI callback in progress" flag, the byte after DI_$Q_HEAD.
 * Zero in the image.  See proc1/proc1_internal.h.
 *
 * Original address: 0xE20606
 */
int8_t DAT_00e20606 = 0;

/*
 * Bad_atomic_operation_err - PROC1_$DISPATCH_INT's crash status.
 * Image bytes at 0x00E20DE8: 00 0a 00 07.
 *
 * Original address: 0xE20DE8
 */
status_$t Bad_atomic_operation_err = 0x000A0007;

/*
 * Illegal_process_id_err - the shared status cell at 0x00E152E0
 * (00 0a 00 01); see proc1/proc1_internal.h for its five `pea (d,PC)' users.
 */
const status_$t Illegal_process_id_err = status_$illegal_process_id;
