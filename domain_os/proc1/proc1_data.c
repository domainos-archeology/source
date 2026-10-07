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
#include "os/os.h"

/*
 * Current process state
 */
proc1_t *PROC1_$CURRENT_PCB = &proc1_$pcb_pool[0]; /* 0xE1EAC8 = 0xE1EBD2: PCB 1 */
/*
 * 0xE20608, inside the PROC1_ASM code segment; reached PC-relative from
 * proc1_$process_exit_handler (proc1/sau2/init_stack.s).  See
 * PROC1_ASM_DATA_SECTION in proc1/proc1_internal.h (source-uwxz).
 */
uint16_t PROC1_$CURRENT PROC1_ASM_DATA_SECTION = 0; /* PID of current process */

/*
 * Ready list tracking
 */
uint16_t PROC1_$READY_COUNT = 1;        /* 0xE1EBD0: the image holds 1 (PCB 1 is on the list) */

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
proc1_t *PCBS[PROC1_MAX_PROCESSES] = {
    NULL,                               /* pid 0 */
    &proc1_$pcb_pool[0],
    &proc1_$pcb_pool[1],
    &proc1_$pcb_pool[2],
    &proc1_$pcb_pool[3],
    &proc1_$pcb_pool[4],
    &proc1_$pcb_pool[5],
    &proc1_$pcb_pool[6],
    &proc1_$pcb_pool[7],
    &proc1_$pcb_pool[8],
    &proc1_$pcb_pool[9],
    &proc1_$pcb_pool[10],
    &proc1_$pcb_pool[11],
    &proc1_$pcb_pool[12],
    &proc1_$pcb_pool[13],
    &proc1_$pcb_pool[14],
    &proc1_$pcb_pool[15],
    &proc1_$pcb_pool[16],
    &proc1_$pcb_pool[17],
    &proc1_$pcb_pool[18],
    &proc1_$pcb_pool[19],
    &proc1_$pcb_pool[20],
    &proc1_$pcb_pool[21],
    &proc1_$pcb_pool[22],
    &proc1_$pcb_pool[23],
    &proc1_$pcb_pool[24],
    &proc1_$pcb_pool[25],
    &proc1_$pcb_pool[26],
    &proc1_$pcb_pool[27],
    &proc1_$pcb_pool[28],
    &proc1_$pcb_pool[29],
    &proc1_$pcb_pool[30],
    &proc1_$pcb_pool[31],
    &proc1_$pcb_pool[32],
    &proc1_$pcb_pool[33],
    &proc1_$pcb_pool[34],
    &proc1_$pcb_pool[35],
    &proc1_$pcb_pool[36],
    &proc1_$pcb_pool[37],
    &proc1_$pcb_pool[38],
    &proc1_$pcb_pool[39],
    &proc1_$pcb_pool[40],
    &proc1_$pcb_pool[41],
    &proc1_$pcb_pool[42],
    &proc1_$pcb_pool[43],
    &proc1_$pcb_pool[44],
    &proc1_$pcb_pool[45],
    &proc1_$pcb_pool[46],
    &proc1_$pcb_pool[47],
    &proc1_$pcb_pool[48],
    &proc1_$pcb_pool[49],
    &proc1_$pcb_pool[50],
    &proc1_$pcb_pool[51],
    &proc1_$pcb_pool[52],
    &proc1_$pcb_pool[53],
    &proc1_$pcb_pool[54],
    &proc1_$pcb_pool[55],
    &proc1_$pcb_pool[56],
    &proc1_$pcb_pool[57],
    &proc1_$pcb_pool[58],
    &proc1_$pcb_pool[59],
    &proc1_$pcb_pool[60],
    &proc1_$pcb_pool[61],
    &proc1_$pcb_pool[62],
    &proc1_$pcb_pool[63]
};

/*
 * proc1_$pcb_pool - the 64 PCBs at 0xE1EBD2..0xE205D2 with the image's
 * initial contents (see proc1/proc1.h).  Image bytes (file offset =
 * VA - 0xDFFC00): pid 1 at 0xE1EBD2 `00E1EC3A 00E1EC3A .. 0001 0001 FFFF
 * .. 0010 0008 0001 0010'; pid 2 at 0xE1EC3A `00E1EBD2 00E1EBD2 .. (0x34)
 * 00EB07FC .. 0002 .. FFFF .. (0x54) 0008'; pids 3..64 `(0x44) pid FFFF ..
 * (0x52) 0010 .. 0001 0010'.  PCB 2's save_a7 is NULL_PC (0xEB07FC = NULLPROC's
 * VA cell at the top of the null process stack, os_$stack_t.null_pc).
 */
#define PROC1_PCB_INIT(pid) \
    [(pid) - 1] = { .mypid = (pid), .vtimer = -1, .state = 0x10, \
                    .inh_count = 1, .sw_bsr = 0x10 }

proc1_t proc1_$pcb_pool[PROC1_MAX_PROCESSES - 1] = {
    [0] = { .nextp = &proc1_$pcb_pool[1], .prevp = &proc1_$pcb_pool[1],
            .mypid = 1, .asid = 1, .vtimer = -1, .state = 0x10,
            .pri_min = 0, .pri_max = 8, .inh_count = 1, .sw_bsr = 0x10 },
    [1] = { .nextp = &proc1_$pcb_pool[0], .prevp = &proc1_$pcb_pool[0],
            .save_a7 = ARCH_PTR_TO_VA_STATIC(&OS_$STACK.null_pc, 0x00EB07FC),
            .mypid = 2, .vtimer = -1, .pri_min = 0, .pri_max = 8 },
    PROC1_PCB_INIT(3),
    PROC1_PCB_INIT(4),
    PROC1_PCB_INIT(5),
    PROC1_PCB_INIT(6),
    PROC1_PCB_INIT(7),
    PROC1_PCB_INIT(8),
    PROC1_PCB_INIT(9),
    PROC1_PCB_INIT(10),
    PROC1_PCB_INIT(11),
    PROC1_PCB_INIT(12),
    PROC1_PCB_INIT(13),
    PROC1_PCB_INIT(14),
    PROC1_PCB_INIT(15),
    PROC1_PCB_INIT(16),
    PROC1_PCB_INIT(17),
    PROC1_PCB_INIT(18),
    PROC1_PCB_INIT(19),
    PROC1_PCB_INIT(20),
    PROC1_PCB_INIT(21),
    PROC1_PCB_INIT(22),
    PROC1_PCB_INIT(23),
    PROC1_PCB_INIT(24),
    PROC1_PCB_INIT(25),
    PROC1_PCB_INIT(26),
    PROC1_PCB_INIT(27),
    PROC1_PCB_INIT(28),
    PROC1_PCB_INIT(29),
    PROC1_PCB_INIT(30),
    PROC1_PCB_INIT(31),
    PROC1_PCB_INIT(32),
    PROC1_PCB_INIT(33),
    PROC1_PCB_INIT(34),
    PROC1_PCB_INIT(35),
    PROC1_PCB_INIT(36),
    PROC1_PCB_INIT(37),
    PROC1_PCB_INIT(38),
    PROC1_PCB_INIT(39),
    PROC1_PCB_INIT(40),
    PROC1_PCB_INIT(41),
    PROC1_PCB_INIT(42),
    PROC1_PCB_INIT(43),
    PROC1_PCB_INIT(44),
    PROC1_PCB_INIT(45),
    PROC1_PCB_INIT(46),
    PROC1_PCB_INIT(47),
    PROC1_PCB_INIT(48),
    PROC1_PCB_INIT(49),
    PROC1_PCB_INIT(50),
    PROC1_PCB_INIT(51),
    PROC1_PCB_INIT(52),
    PROC1_PCB_INIT(53),
    PROC1_PCB_INIT(54),
    PROC1_PCB_INIT(55),
    PROC1_PCB_INIT(56),
    PROC1_PCB_INIT(57),
    PROC1_PCB_INIT(58),
    PROC1_PCB_INIT(59),
    PROC1_PCB_INIT(60),
    PROC1_PCB_INIT(61),
    PROC1_PCB_INIT(62),
    PROC1_PCB_INIT(63),
    PROC1_PCB_INIT(64)
};
#undef PROC1_PCB_INIT

#if defined(ARCH_M68K)
_Static_assert(sizeof(proc1_$pcb_pool) == 0xE205D2 - 0xE1EBD2,
               "proc1_$pcb_pool: 64 PCBs end at PROC1_$TSVV");
#endif

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
