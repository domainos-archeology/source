/*
 * PROC1 - Process Management Module
 *
 * This module provides process management for Domain/OS including:
 * - Process creation, binding, and termination
 * - Scheduling and ready list management
 * - Context switching (dispatcher)
 * - Lock management (resource locks)
 * - Atomic operations and inhibit regions
 * - CPU time tracking and load averaging
 *
 * Data: the PROC1_ module block PROC1_$DATA (A5 = 0xE254E8, below) and the
 * cells of the PROC1_ASM code segment (0xE1EAC8.., after the block).
 */

#ifndef PROC1_H
#define PROC1_H

#include "base/base.h"
#include "ec/ec.h"
#include "time/time.h"
#include "proc1/proc1_config.h"

/*
 * Process Control Block (PCB) structure
 * Size: ~0x68 bytes (104 bytes)
 *
 * The ready list is a doubly-linked circular list ordered by:
 *   1. resource_locks_held (descending - more locks = higher priority)
 *   2. state (descending - higher state = higher priority)
 *
 * A sentinel PCB (PID 2 on SAU2) serves as the list anchor.
 * PROC1_$READY_PCB physically overlaps the sentinel's nextp field,
 * so loading PROC1_$READY_PCB gives the first real process in the list.
 * The sentinel has state=0x08, lower than any real process (>= 0x10),
 * ensuring insertion loops always terminate.
 */
typedef struct proc1_t {
    struct proc1_t *nextp;          /* 0x00: Next process in ready list */
    struct proc1_t *prevp;          /* 0x04: Previous process in ready list */

    /* Saved registers (context switch) */
    uint32_t    save_d2;            /* 0x08: Saved D2 */
    uint32_t    save_d3;            /* 0x0C: Saved D3 */
    uint32_t    save_d4;            /* 0x10: Saved D4 */
    uint32_t    save_d5;            /* 0x14: Saved D5 */
    uint32_t    save_d6;            /* 0x18: Saved D6 */
    uint32_t    save_d7;            /* 0x1C: Saved D7 */
    uint32_t    save_a2;            /* 0x20: Saved A2 */
    uint32_t    save_a3;            /* 0x24: Saved A3 */
    uint32_t    save_a4;            /* 0x28: Saved A4 */
    uint32_t    save_a5;            /* 0x2C: Saved A5 */
    uint32_t    save_a6;            /* 0x30: Saved A6 */
    uint32_t    save_a7;            /* 0x34: Saved A7 (SSP) */
    void        *save_usp;          /* 0x38: Saved user stack pointer */

    uint32_t    wait_start;         /* 0x3C: TIME_$CLOCKH when wait started */
    uint32_t    resource_locks_held;/* 0x40: Bitmask of held resource locks */

    uint16_t    mypid;              /* 0x44: Process ID */
    uint16_t    asid;               /* 0x46: Address Space ID */
    int16_t     vtimer;             /* 0x48: Virtual timer value */
    uint16_t    pad_4a;             /* 0x4A: Padding or reserved */
    uint32_t    cpu_total;          /* 0x4C: CPU time high word */
    uint16_t    cpu_usage;          /* 0x50: CPU time low word */

    uint16_t    state;              /* 0x52: Process state */
    uint8_t     pri_min;            /* 0x54: Minimum priority or flags */
    uint8_t     pri_max;            /* 0x55: Priority/flags byte */
                                    /*   Bit 0 (0x01): Waiting on EC */
                                    /*   Bit 1 (0x02): Suspended */
                                    /*   Bit 2 (0x04): Deferred suspend? */
                                    /*   Bit 3 (0x08): Bound (in use) */

    uint16_t    inh_count;          /* 0x56: Min priority/state (initialized to 1 by BIND) */
    uint16_t    sw_bsr;             /* 0x58: Software base/something (initialized to 0x10 by BIND) */
    uint16_t    nesting_depth;      /* 0x5A: Lock/inhibit nesting depth counter */

    uint32_t    field_5c;           /* 0x5C: Unknown */
    uint32_t    field_60;           /* 0x60: Unknown */
    uint32_t    field_64;           /* 0x64: Unknown */
} proc1_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(proc1_t, nextp) == 0x00, "proc1_t.nextp");
_Static_assert(__builtin_offsetof(proc1_t, prevp) == 0x04, "proc1_t.prevp");
_Static_assert(__builtin_offsetof(proc1_t, save_d2) == 0x08, "proc1_t.save_d2");
_Static_assert(__builtin_offsetof(proc1_t, save_d3) == 0x0C, "proc1_t.save_d3");
_Static_assert(__builtin_offsetof(proc1_t, save_d4) == 0x10, "proc1_t.save_d4");
_Static_assert(__builtin_offsetof(proc1_t, save_d5) == 0x14, "proc1_t.save_d5");
_Static_assert(__builtin_offsetof(proc1_t, save_d6) == 0x18, "proc1_t.save_d6");
_Static_assert(__builtin_offsetof(proc1_t, save_d7) == 0x1C, "proc1_t.save_d7");
_Static_assert(__builtin_offsetof(proc1_t, save_a2) == 0x20, "proc1_t.save_a2");
_Static_assert(__builtin_offsetof(proc1_t, save_a3) == 0x24, "proc1_t.save_a3");
_Static_assert(__builtin_offsetof(proc1_t, save_a4) == 0x28, "proc1_t.save_a4");
_Static_assert(__builtin_offsetof(proc1_t, save_a5) == 0x2C, "proc1_t.save_a5");
_Static_assert(__builtin_offsetof(proc1_t, save_a6) == 0x30, "proc1_t.save_a6");
_Static_assert(__builtin_offsetof(proc1_t, save_a7) == 0x34, "proc1_t.save_a7");
_Static_assert(__builtin_offsetof(proc1_t, save_usp) == 0x38, "proc1_t.save_usp");
_Static_assert(__builtin_offsetof(proc1_t, wait_start) == 0x3C, "proc1_t.wait_start");
_Static_assert(__builtin_offsetof(proc1_t, resource_locks_held) == 0x40, "proc1_t.resource_locks_held");
_Static_assert(__builtin_offsetof(proc1_t, mypid) == 0x44, "proc1_t.mypid");
_Static_assert(__builtin_offsetof(proc1_t, asid) == 0x46, "proc1_t.asid");
_Static_assert(__builtin_offsetof(proc1_t, vtimer) == 0x48, "proc1_t.vtimer");
_Static_assert(__builtin_offsetof(proc1_t, pad_4a) == 0x4A, "proc1_t.pad_4a");
_Static_assert(__builtin_offsetof(proc1_t, cpu_total) == 0x4C, "proc1_t.cpu_total");
_Static_assert(__builtin_offsetof(proc1_t, cpu_usage) == 0x50, "proc1_t.cpu_usage");
_Static_assert(__builtin_offsetof(proc1_t, state) == 0x52, "proc1_t.state");
_Static_assert(__builtin_offsetof(proc1_t, pri_min) == 0x54, "proc1_t.pri_min");
_Static_assert(__builtin_offsetof(proc1_t, pri_max) == 0x55, "proc1_t.pri_max");
_Static_assert(__builtin_offsetof(proc1_t, inh_count) == 0x56, "proc1_t.inh_count");
_Static_assert(__builtin_offsetof(proc1_t, sw_bsr) == 0x58, "proc1_t.sw_bsr");
_Static_assert(__builtin_offsetof(proc1_t, nesting_depth) == 0x5A, "proc1_t.nesting_depth");
_Static_assert(__builtin_offsetof(proc1_t, field_5c) == 0x5C, "proc1_t.field_5c");
_Static_assert(__builtin_offsetof(proc1_t, field_60) == 0x60, "proc1_t.field_60");
_Static_assert(__builtin_offsetof(proc1_t, field_64) == 0x64, "proc1_t.field_64");
_Static_assert(sizeof(proc1_t) == 0x68, "proc1_t size");
#endif

/*
 * PCB flag bits (in pri_max at offset 0x55)
 */
#define PROC1_FLAG_WAITING      0x01    /* Waiting on event count */
#define PROC1_FLAG_SUSPENDED    0x02    /* Process is suspended */
#define PROC1_FLAG_DEFER_SUSP   0x04    /* Deferred suspend pending */
#define PROC1_FLAG_BOUND        0x08    /* Process is bound (in use) */

/*
 * Lock IDs for PROC1_$SET_LOCK / PROC1_$CLR_LOCK
 * Locks are implemented as bits in resource_locks_held
 */
#define PROC1_CREATE_LOCK_ID    0x0B    /* Process creation lock */

/*
 * Status codes
 */
#define status_$illegal_process_id          0x000A0001
#define status_$no_pcb_is_available         0x000A0008
#define status_$process_not_bound           0x000A0005
#define status_$process_not_suspended       0x000A0003
#define status_$process_already_suspended   0x000A0004
#define status_$no_stack_space_is_available 0x000A0009
#define status_$process_not_suspendable     0x000A000A

/*
 * ============================================================================
 * PROC1_$DATA - the PROC1_ module data block (map "D E254E8 PROC1_ size = CC4")
 * ============================================================================
 *
 * Module data block PROC1_$DATA: Claude Opus 5.5 (source-l2yd).
 *
 * Every Pascal PROC1_ routine loads A5 with `lea (0xe254e8).l,A5' (e.g.
 * PROC1_$BIND 0x00E14D24); PROC1_$INIT (boot-time segment) reaches it with
 * `movea.l #0xe254e8,A0' (0x00E2F95C), and routines outside PROC1 address
 * PROC1_$TYPE and PROC1_$STATS by their map addresses.  A MODULE_DATA block
 * linked in the SAU2 map's order after PMAP_$DATA and before
 * RING_$WIRED_DATA; the address is the ordering key, not the link address.
 * The map names three objects in it (OS_STACK_BASE 0xE25C18, PROC1_$STATS
 * 0xE25D20, PROC1_$TYPE 0xE2612C); the rest are known by displacement:
 *
 *   A5 off  image      field
 *   0x000   0xE254E8   loadav[3]         PROC1_$INIT_LOADAV clr.l (A5)/(4,A5)/
 *                                        (8,A5); PROC1_$GET_LOADAV `lea (A5),A0'
 *   0x00C   0xE254F4   (4 bytes, never addressed)
 *   0x010   0xE254F8   loadav_elem       PROC1_$INIT_LOADAV `pea (0x10,A5)' to
 *                                        TIME_$Q_ENTER_ELEM
 *   0x014   0xE254FC   ts_elem[0..64]    timeslice timer element per pid
 *   0x730   0xE25C18   os_stack_base[0..64]  map OS_STACK_BASE
 *   0x834   0xE25D1C   (4 bytes, never addressed)
 *   0x828   0xE25D10   stats[0..64]      per-pid counters; map PROC1_$STATS
 *                                        (0xE25D20) is stats[1]
 *   0xC38   0xE26120   stack_free_list   PROC1_$ALLOC_STACK / PROC1_$FREE_STACK
 *   0xC3C   0xE26124   stack_high_water
 *   0xC40   0xE26128   stack_low_water
 *   0xC42   0xE2612A   type[0..64]       map PROC1_$TYPE (0xE2612C) is type[1];
 *                                        type[64] ends the block at 0xCC4
 *
 * Four per-process tables, each declared at the lowest address the code can
 * reach and indexed with the pid the assembly scales (design section 3,
 * docs/design-per-process-data.md).  Where element 0 overlays the object
 * before it, the table is a union arm beside that object:
 *
 *   ts_elem[pid]        A5 + 0x14 + pid*0x1C.  PROC1_$INIT_TS_TIMER builds
 *                       pid*0x1C as pid*4*8 - pid*4 (0x00E14B24..0x00E14B2E)
 *                       and passes `pea (0x14,A2)' with A2 = A5 + pid*0x1C;
 *                       PROC1_$SET_TS `pea (0x14,A5,D2w)' with D2 = pid*0x1C.
 *                       Element 0 (0x14..0x30) overlays loadav_elem
 *                       (0x10..0x2A); element 64 ends at 0x730.
 *   os_stack_base[pid]  A5 + 0x730 + pid*4: PROC1_$BIND `lsl.w #0x2,D3w' /
 *                       `lea (0x0,A5,D3w*0x1),A1' / `move.l D4,(0x730,A1)'
 *                       (0x00E14D7E..0x00E14D84).  Element 0 is the map's
 *                       OS_STACK_BASE.  Target VAs of stack tops.
 *   stats[pid]          A5 + 0x828 + pid*0x10: PROC1_$BIND `lsl.w #0x4,D3w' /
 *                       `lea (0x0,A5,D3w*0x1),A3' / `clr.l (0x828,A3)'
 *                       (0x00E14DC4..0x00E14DCA); callers outside PROC1 use
 *                       `(-0x8,A2,D2w)' with A2 = 0xE25D20 and D2 = pid*0x10
 *                       (0x00E748CA).  Element 0 (0x828..0x838) overlays
 *                       os_stack_base[62..64] and the pad at 0x834.
 *   type[pid]           A5 + 0xC42 + pid*2: PROC1_$SET_TYPE `(0xc42,A0)' with
 *                       A0 = A5 + pid*2; callers outside PROC1 use
 *                       `cmpi.w #0x9,(-0x2,A0,D0w*0x1)' with A0 = 0xE2612C and
 *                       D0 = pid*2 (DIR_$LOCK_OBJ 0x00E4AFCA).  Element 0
 *                       (0xC42) overlays the low word of stack_low_water.
 *
 * The block holds no pointers: the stack cells and os_stack_base[] are
 * target VAs (ARCH_VA_TO_PTR / ARCH_PTR_TO_VA at the use sites), so the
 * bias arms sit at the same offsets on a host.  The one host difference is
 * time_queue_elem_t, 0x1A bytes on the target but padded to 0x1C by a
 * 4-byte-aligning host, which moves everything from 0x730 on; the offsets
 * past ts_elem are therefore asserted for the target only.
 *
 * Image contents: `gsk read 0xE254E8 3268' is zero throughout.
 */
#define PROC1_$DATA_SIZE 0xCC4          /* map: PROC1_ size = CC4 */

/* PROC1_$GET_LOADAV copies the three load averages out as a block
 * (three `move.l (A0)+,(A1)+' at 0x00E14BCC). */
#define PROC1_LOADAV_COUNT 3

/*
 * proc1_ts_slot_t - one timeslice timer element with its stride padding.
 *
 * The element is an ordinary time_queue_elem_t (0x1A bytes); the table
 * stride is 0x1C (see ts_elem above).
 */
typedef struct proc1_ts_slot_t {
    time_queue_elem_t elem;     /* 0x00: the queue element handed to TIME */
    uint16_t          pad_1a;   /* 0x1A: stride padding */
} proc1_ts_slot_t;

_Static_assert(__builtin_offsetof(proc1_ts_slot_t, elem) == 0x00, "proc1_ts_slot_t.elem");
/* time_queue_elem_t is 0x1A on the target, 0x1C on a 4-byte-aligning host */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(proc1_ts_slot_t, pad_1a) == 0x1A, "proc1_ts_slot_t.pad_1a");
_Static_assert(sizeof(proc1_ts_slot_t) == 0x1C, "proc1_ts_slot_t: 0x1C stride");
#endif

/*
 * proc1_$stats_t - one PROC1_$STATS record (0x10 bytes, four longword
 * counters).  The counters are named by their users rather than here:
 *   stat[0] +0x00  pages touched (AST_$TOUCH, AST_$TOUCH_AREA)
 *   stat[1] +0x04  pages touched, the other flavour (AST_$TOUCH)
 *   stat[2] +0x08  pages read from disk (AST_$READ_AREA_PAGES, PMAP)
 *   stat[3] +0x0C  pages read over the network (AST_$READ_AREA_PAGES_NETWORK)
 * PACCT_$LOG receives &stat[2] and &stat[3] (0x00E748CA / 0x00E748C6).
 */
typedef struct proc1_$stats_t {
    uint32_t    stat[4];        /* 0x00 */
} proc1_$stats_t;

_Static_assert(sizeof(proc1_$stats_t) == 0x10, "proc1_$stats_t: 0x10 stride (lsl.w #0x4)");

typedef struct proc1_$data_t {
    union {                                     /* +0x000 */
        struct {
            int32_t           loadav[PROC1_LOADAV_COUNT]; /* +0x000 8.24 fixed */
            uint32_t          _000c;            /* +0x00C never addressed */
            time_queue_elem_t loadav_elem;      /* +0x010 */
        };
        struct {
            uint8_t           _ts_bias[0x14];
            /* +0x014: [0..64]; [0] overlays loadav_elem */
            proc1_ts_slot_t   ts_elem[PROC1_MAX_PROCESSES];
        };
    };
    union {                                     /* +0x730 */
        struct {
            uint32_t          os_stack_base[PROC1_MAX_PROCESSES]; /* +0x730 VAs */
            uint32_t          _0834;            /* +0x834 never addressed */
        };
        struct {
            uint8_t           _stats_bias[0xF8];
            /* +0x828: [0..64]; [1] is map PROC1_$STATS, [0] overlays
             * os_stack_base[62..64] and the pad */
            proc1_$stats_t    stats[PROC1_MAX_PROCESSES];
        };
    };
    union {                                     /* +0xC38 */
        struct {
            uint32_t          stack_free_list;  /* +0xC38 VA of the first free
                                                 *        4KB stack, 0 = none */
            uint32_t          stack_high_water; /* +0xC3C VA, grows down */
            uint32_t          stack_low_water;  /* +0xC40 VA, grows up */
        };
        struct {
            uint8_t           _type_bias[0x0A];
            /* +0xC42: [0..64]; [1] is map PROC1_$TYPE, [0] overlays the low
             * word of stack_low_water */
            uint16_t          type[PROC1_MAX_PROCESSES];
        };
    };
} proc1_$data_t;

_Static_assert(__builtin_offsetof(proc1_$data_t, loadav) == 0x000, "loadav (A5)");
_Static_assert(__builtin_offsetof(proc1_$data_t, loadav_elem) == 0x010, "loadav_elem (pea (0x10,A5))");
_Static_assert(__builtin_offsetof(proc1_$data_t, ts_elem) == 0x014, "ts_elem[0] (0x14,A5,pid*0x1C)");
#if defined(ARCH_M68K)
/* Target only: time_queue_elem_t pads to 0x1C on a host (see above). */
_Static_assert(__builtin_offsetof(proc1_$data_t, ts_elem[1]) == 0x030, "ts_elem stride 0x1C");
_Static_assert(__builtin_offsetof(proc1_$data_t, ts_elem[PROC1_MAX_PROCESSES]) == 0x730,
               "ts_elem[64] ends at OS_STACK_BASE");
_Static_assert(__builtin_offsetof(proc1_$data_t, os_stack_base) == 0x730, "OS_STACK_BASE (0x730,A1)");
_Static_assert(__builtin_offsetof(proc1_$data_t, os_stack_base[1]) == 0x734, "os_stack_base stride 4 (lsl.w #0x2)");
_Static_assert(__builtin_offsetof(proc1_$data_t, _0834) == 0x834, "pad after os_stack_base[64]");
_Static_assert(__builtin_offsetof(proc1_$data_t, stats) == 0x828, "stats[0] (0x828,A3)");
_Static_assert(__builtin_offsetof(proc1_$data_t, stats[1]) == 0x838, "PROC1_$STATS (0xE25D20) = stats[1]");
_Static_assert(__builtin_offsetof(proc1_$data_t, stats[PROC1_MAX_PROCESSES]) == 0xC38,
               "stats[64] ends at stack_free_list");
_Static_assert(__builtin_offsetof(proc1_$data_t, stack_free_list) == 0xC38, "stack_free_list (0xc38,A5)");
_Static_assert(__builtin_offsetof(proc1_$data_t, stack_high_water) == 0xC3C, "stack_high_water (0xc3c,A5)");
_Static_assert(__builtin_offsetof(proc1_$data_t, stack_low_water) == 0xC40, "stack_low_water (0xc40,A5)");
_Static_assert(__builtin_offsetof(proc1_$data_t, type) == 0xC42, "type[0] (0xc42,A0) / (-0x2,0xE2612C)");
_Static_assert(__builtin_offsetof(proc1_$data_t, type[1]) == 0xC44, "PROC1_$TYPE (0xE2612C) = type[1]");
_Static_assert(__builtin_offsetof(proc1_$data_t, type[PROC1_MAX_PROCESSES]) == PROC1_$DATA_SIZE,
               "type[64] ends the block");
_Static_assert(sizeof(proc1_$data_t) == PROC1_$DATA_SIZE, "PROC1_: map size 0xCC4");
#endif
/* Pointer-free and alignment-independent relative to its own arm. */
_Static_assert(__builtin_offsetof(proc1_$data_t, stats[1]) - __builtin_offsetof(proc1_$data_t, stats) == 0x10,
               "stats stride 0x10");
_Static_assert(__builtin_offsetof(proc1_$data_t, type[1]) - __builtin_offsetof(proc1_$data_t, type) == 2,
               "type stride 2");
_Static_assert(__builtin_offsetof(proc1_$data_t, stats) - __builtin_offsetof(proc1_$data_t, os_stack_base) == 0xF8,
               "stats[0] overlays os_stack_base[62]");
_Static_assert(__builtin_offsetof(proc1_$data_t, type) - __builtin_offsetof(proc1_$data_t, stack_free_list) == 0x0A,
               "type[0] overlays the low word of stack_low_water");

MODULE_DATA_DECLARE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);

/*
 * Maximum number of state/priority levels for timeslice table
 */
#define PROC1_MAX_STATES 32

/*
 * ============================================================================
 * PROC1_ASM cells (map "D E1EAC8 PROC1_ASM size = 24A4")
 * ============================================================================
 *
 * The PROC1_ASM segment is the hand-written dispatcher/EC/lock code with
 * its data cells interleaved (0xE1EAC8..0xE20F6C).  Its cells are reached
 * by absolute address or PC-relative from that code and exported by name;
 * none is addressed through A5, so they stay individual objects here
 * (proc1/proc1_data.c; like the NETLOG_ASM cells, ordering them is
 * source-91vs).  Our proc1/sau2/ *.s files name them as .extern symbols and
 * define none of them.
 *
 *   0xE1EAC8  PROC1_$CURRENT_PCB
 *   0xE1EACC  PCBS[0..64]      element pid at 0xE1EACC + pid*4: PROC1_$BIND
 *                              `movea.l #0xe1eacc,A1' / `lea (0x0,A1,D3w*0x1),A3'
 *                              with D3 = pid*4 (0x00E14D96..0x00E14D9C);
 *                              element 0 is the map symbol, element 64 ends
 *                              at PROC1_$READY_COUNT (0xE1EBD0)
 *   0xE1EBD0  PROC1_$READY_COUNT
 *   0xE1EC3A  PROC1_$READY_PCB
 *   0xE205D2  PROC1_$TSVV[18]
 *   0xE205F6  PROC1_$SUSPEND_EC
 *   0xE20608  PROC1_$CURRENT
 *   0xE2060A  PROC1_$AS_ID
 *   0xE2060E  PROC1_$ATOMIC_OP_DEPTH
 */
extern proc1_t *PROC1_$CURRENT_PCB;     /* 0xE1EAC8: Current running process */
extern proc1_t *PROC1_$READY_PCB;       /* 0xE1EC3A: Head of ready list */
extern uint16_t PROC1_$CURRENT;         /* 0xE20608: Current process ID */
extern uint16_t PROC1_$READY_COUNT;     /* 0xE1EBD0: Number of ready processes */
extern uint16_t PROC1_$ATOMIC_OP_DEPTH; /* 0xE2060E: Atomic operation nesting */
extern uint16_t PROC1_$AS_ID;           /* 0xE2060A: Current address space ID */
extern proc1_t *PCBS[PROC1_MAX_PROCESSES];      /* 0xE1EACC: [0..64] by pid */

/* 0xE205D2: SAU2 map PROC1_$TSVV, one timeslice word per state, 18 entries
 * (0xE205D2..0xE205F6 = PROC1_$SUSPEND_EC); ADVANCE_INT bounds the index with
 * `cmp.l #0x11' at 0xE20780. */
#define PROC1_TSVV_COUNT 18
extern int16_t PROC1_$TSVV[PROC1_TSVV_COUNT];

/*
 * Event count for process suspension
 */

extern ec_$eventcount_t PROC1_$SUSPEND_EC; /* 0xE205F6: Suspend event count */

/* INIT_STACK - Initialize process stack for first dispatch
 * Sets up the stack so when dispatched, process starts at *entry_ptr */
extern void INIT_STACK(proc1_t *pcb, void **entry_ptr, void **sp_ptr);

/*
 * ============================================================================
 * Process Lifecycle Functions
 * ============================================================================
 */

/*
 * PROC1_$INIT - Initialize process subsystem
 * Original address: 0x00e2f958
 */
void PROC1_$INIT(void);

/*
 * PROC1_$CREATE_P - Create a new process
 * Parameters:
 *   funcptr - Entry point function
 *   type - Process type (packed: low=stack type, high=proc type)
 *   status_ret - Status return
 * Returns: Process ID
 * Original address: 0x00e15148
 */
uint16_t PROC1_$CREATE_P(void *funcptr, uint32_t type, status_$t *status_ret);

/*
 * PROC1_$BIND - Bind a process to a PCB
 * Original address: 0x00e14d1c
 */
/*
 * Frame (0x00E14D1C): 0x08 entry, 0x0C initial_sp, 0x10 stack_base,
 * 0x14 ws_param (word), 0x16 status.  INIT_STACK is handed &entry and
 * &initial_sp (`pea (0xc,A6)' at 0x00E14DF4); stack_base is what goes into
 * OS_STACK_BASE[pid] (0x00E14D84).  PROC1_$CREATE_P passes the same value
 * for both stack arguments.
 */
uint16_t PROC1_$BIND(void *entry, void *initial_sp, void *stack_base,
                     uint16_t ws_param, status_$t *status_p);

/*
 * PROC1_$UNBIND - Unbind a process
 * Original address: 0x00e14e24
 */
void PROC1_$UNBIND(uint16_t pid, status_$t *status_ret);

/*
 * PROC1_$ALLOC_STACK - Allocate a process stack
 * Original address: 0x00e1501a
 */

void *PROC1_$ALLOC_STACK(uint16_t type, status_$t *status_ret);

/*
 * PROC1_$FREE_STACK - Free a process stack
 * Original address: 0x00e1511a
 */
void PROC1_$FREE_STACK(void *stack);

/*
 * ============================================================================
 * Suspend/Resume Functions
 * ============================================================================
 */

/*
 * PROC1_$SUSPEND - Suspend a process
 * Original address: 0x00e147fa
 */
int8_t PROC1_$SUSPEND(uint16_t process_id, status_$t *status_ret);

/*
 * PROC1_$SUSPENDP - Suspend with parameters
 * Original address: 0x00e14876
 */
int8_t PROC1_$SUSPENDP(uint16_t pid, status_$t *status_ret);

/*
 * PROC1_$RESUME - Resume a suspended process
 * Original address: 0x00e1476e
 */
void PROC1_$RESUME(uint16_t pid, status_$t *status_p);

/*
 * PROC1_$TRY_TO_SUSPEND - Internal: attempt to suspend
 * Original address: 0x00e1471c
 */
void PROC1_$TRY_TO_SUSPEND(proc1_t *pcb);

/*
 * ============================================================================
 * Dispatcher Functions
 * ============================================================================
 */

/*
 * PROC1_$DISPATCH - High-level dispatch
 * Original address: 0x00e20a18
 */
void PROC1_$DISPATCH(void);

/*
 * PROC1_$DISPATCH_INT - Dispatch (internal, A1=current PCB)
 * Assembly function that saves context and switches processes.
 * Original address: 0x00e20a20
 */
void PROC1_$DISPATCH_INT(void);

/*
 * PROC1_$DISPATCH_INT2 - Dispatch variant (A1=pcb param)
 * Original address: 0x00e20a24
 */
void PROC1_$DISPATCH_INT2(proc1_t *pcb);

/*
 * PROC1_$DISPATCH_INT3 - Full context switch implementation
 * Original address: 0x00e20a34
 */
void PROC1_$DISPATCH_INT3(void);

/*
 * ============================================================================
 * Ready List Functions
 * ============================================================================
 */

/*
 * PROC1_$ADD_READY - Add process to ready list
 * Original address: 0x00e20820
 */
void PROC1_$ADD_READY(proc1_t *pcb);

/*
 * PROC1_$REMOVE_READY - Remove process from ready list
 * Original address: 0x00e206d2
 */
void PROC1_$REMOVE_READY(proc1_t *pcb);

/*
 * PROC1_$REORDER_READY - Reorder process in ready list
 * Original address: 0x00e207d4
 *
 * A four-byte gate (`movea.l (0x4,SP),A1') that falls into
 * proc1_$reorder_if_needed; every caller pushes the PCB (`pea (A0)' at
 * 0x00E2F9B4, `pea (A4)' at 0x00E152B8, `pea (A3)' at 0x00E14AC0).
 */
void PROC1_$REORDER_READY(proc1_t *pcb);

/*
 * proc1_$remove_from_ready_list - Internal remove helper
 * Original address: 0x00e206d6
 */
void proc1_$remove_from_ready_list(proc1_t *pcb);

/*
 * proc1_$insert_into_ready_list - LIFO priority-ordered ready list insertion
 * Inserts BEFORE equal-priority entries (prioritize newly inserted).
 * Original address: 0x00e20844
 */
void proc1_$insert_into_ready_list(proc1_t *pcb);

/*
 * proc1_$reorder_if_needed - Reorder if priority changed
 * Original address: 0x00e207d8
 *
 * Register convention on m68k: reached with `bsr.w` and A1 = pcb.
 */
void proc1_$reorder_if_needed(proc1_t *pcb);

/*
 * proc1_$add_ready_body - FIFO priority-ordered ready list insertion
 *
 * The body of PROC1_$ADD_READY.  PROC1_$ADD_READY (0x00e20820) is a
 * four-byte wrapper (`movea.l (0x4,SP),A1`) that falls through into this
 * code, so on m68k the body takes its PCB in A1 and is reached with
 * `bsr.w` (e.g. from the ML_$UNLOCK / ML_$EXCLUSION_STOP epilogue at
 * 0x00E20ECC).  Declared here so the ML subsystem can express that call
 * without reaching into proc1_internal.h.
 *
 * Original address: 0x00e20824
 */
void proc1_$add_ready_body(proc1_t *pcb);

/*
 * ============================================================================
 * Lock Functions
 * ============================================================================
 */

/*
 * PROC1_$SET_LOCK - Acquire a resource lock
 * Original address: 0x00e20ae4
 */
void PROC1_$SET_LOCK(uint16_t lock_id);

/*
 * PROC1_$CLR_LOCK - Release a resource lock
 * Original address: 0x00e20b92
 */
void PROC1_$CLR_LOCK(uint16_t lock_id);

/*
 * PROC1_$TST_LOCK - Test if a lock is held
 * Original address: 0x00e148ca
 */
/* 0x00E148DE: `sne D1b / move.b D1b,D0b' - a Domain boolean in D0.b */
int8_t PROC1_$TST_LOCK(uint16_t lock_id);

/*
 * PROC1_$GET_LOCKS - Get locks held by current process
 * Original address: 0x00e148e6
 */
uint32_t PROC1_$GET_LOCKS(void);

/*
 * ============================================================================
 * Atomic Operation Functions
 * ============================================================================
 */

/*
 * PROC1_$BEGIN_ATOMIC_OP - Begin atomic operation region
 * Original address: 0x00e209e6
 */
void PROC1_$BEGIN_ATOMIC_OP(void);

/*
 * PROC1_$END_ATOMIC_OP - End atomic operation region
 * Original address: 0x00e209fa
 */
void PROC1_$END_ATOMIC_OP(void);

/*
 * PROC1_$INHIBIT_BEGIN - Begin inhibit region
 * Original address: 0x00e20efc
 */
void PROC1_$INHIBIT_BEGIN(void);

/*
 * PROC1_$INHIBIT_END - End inhibit region
 * Original address: 0x00e20ea2
 */
void PROC1_$INHIBIT_END(void);

/*
 * PROC1_$INHIBIT_CHECK - Check inhibit state
 * Parameters:
 *   pcb - Process to check
 * Returns:
 *   -1 if process is inhibited (nesting_depth != 0)
 *   0 if not inhibited
 * Original address: 0x00e20ef0
 */
int8_t PROC1_$INHIBIT_CHECK(proc1_t *pcb);

/*
 * ============================================================================
 * Event Count Integration
 * ============================================================================
 */

/*
 * PROC1_$EC_WAITN - Wait on event counts (internal)
 * Original address: 0x00e2065a
 */
/*
 * In the image (0x00E2065A) this is register-convention assembly: A1 = pcb,
 * A4 = &ecs[0], A3 = &vals[0], D0.w = count, result in D0.w (1-based index
 * of the lowest satisfied eventcount, 0 when none / count when count <= 0).
 * ec/wait.c and ec/waitn.c call it with the C signature below.
 */
uint16_t PROC1_$EC_WAITN(proc1_t *pcb, ec_$eventcount_t **ecs,
                          int32_t *wait_vals, int16_t num_ecs);

/*
 * ============================================================================
 * CPU Time and Load Functions
 * ============================================================================
 */

/*
 * PROC1_$GET_CPUT - Get CPU time for current process (shifted by 1)
 * Original address: 0x00e20894
 *
 * Parameters:
 *   time_ret - Pointer to receive 48-bit CPU time (shifted left 1)
 */
void PROC1_$GET_CPUT(clock_t *clock);

/*
 * PROC1_$GET_CPUT8 - Get CPU time for current process (unshifted)
 * Original address: 0x00e2089c
 *
 * Returns the CPU time for the current process as a 48-bit value.
 *
 * Parameters:
 *   time_ret - Pointer to receive 48-bit CPU time value
 */
void PROC1_$GET_CPUT8(clock_t *time_ret);

/*
 * PROC1_$GET_CPU_USAGE - Get CPU usage for current process
 * Original address: 0x00e208aa
 *
 * Parameters:
 *   time_ret - Pointer to receive CPU time (6 bytes, shifted left by 1)
 *   stat1_ret - Pointer to receive field_60 from PCB
 *   stat2_ret - Pointer to receive field_64 from PCB
 */
/* 0x00E208B0: argument 1 is the 6-byte clock; 2 and 3 receive PCB+0x60/+0x64 */
void PROC1_$GET_CPU_USAGE(clock_t *clock, uint32_t *stat1_ret, uint32_t *stat2_ret);

/*
 * PROC1_$GET_ANY_CPUT - Get the raw accumulated CPU time of any process
 * Original address: 0x00e153f8
 *
 * Frame: 0x08 cpu_time_ret, 0x0C pid (word).  Copies PCB cpu_total:cpu_usage
 * as they stand (no doubling, no timer correction); crashes on a bad pid.
 */
void PROC1_$GET_ANY_CPUT(clock_t *cpu_time_ret, uint16_t pid);

/*
 * PROC1_$GET_ANY_CPU_USAGE - Get CPU usage for any process
 * Original address: 0x00e1543e
 *
 * Returns CPU time and additional statistics for a specified process.
 * Crashes the system if PID is invalid (0 or > 64).
 *
 * Parameters:
 *   pid_ptr - Pointer to process ID
 *   cpu_time_ret - Pointer to receive CPU time (6 bytes, shifted left by 1)
 *   stat1_ret - Pointer to receive field_60 from PCB
 *   stat2_ret - Pointer to receive field_64 from PCB
 */
void PROC1_$GET_ANY_CPU_USAGE(uint16_t *pid_ptr, void *cpu_time_ret,
                               uint32_t *stat1_ret, uint32_t *stat2_ret);

/*
 * PROC1_$GET_LOADAV - Get system load average
 * Original address: 0x00e14bba
 */
void PROC1_$GET_LOADAV(uint32_t *loadav);

/*
 * PROC1_$INIT_LOADAV - Initialize load averaging
 * Original address: 0x00e14c94
 */
void PROC1_$INIT_LOADAV(void);

/*
 * ============================================================================
 * Priority and Type Functions
 * ============================================================================
 */

/*
 * PROC1_$SET_PRIORITY - Set process priority range
 * Sets min and max priority for process if mode < 0.
 * Original address: 0x00e1523c
 */
/*
 * Frame (0x00E1523C): 0x08 pid (word), 0x0A set (a Domain boolean BYTE:
 * `move.b (0xa,A6),D3b'; callers push `clr.w' for a query), 0x0C min,
 * 0x10 max.  set < 0 stores the clamped min/max into the PCB and
 * re-orders it; otherwise min/max receive the PCB's current pair.
 */
void PROC1_$SET_PRIORITY(uint16_t pid, int8_t set, uint16_t *min_priority, uint16_t *max_priority);

/*
 * PROC1_$SET_TYPE - Set process type
 * Original address: 0x00e152e4
 */
void PROC1_$SET_TYPE(uint16_t pid, uint16_t type);

/*
 * PROC1_$GET_TYPE - Get process type
 * Original address: 0x00e15324
 *
 * A Pascal procedure with a var result: frame 0x08 pid (word), 0x0A type_ret.
 * Crashes the system on pid 0 or pid > 64.
 */
void PROC1_$GET_TYPE(uint16_t pid, uint16_t *type_ret);

/*
 * ============================================================================
 * Virtual Timer Functions
 * ============================================================================
 */

/*
 * PROC1_$SET_VT - Set virtual timer
 * Original address: 0x00e1495c
 */
/* 0x00E149A0: `tst.l (A0)' then `move.w (0x4,A0)' - a 6-byte clock */
void PROC1_$SET_VT(uint16_t pid, clock_t *vt, status_$t *status_ret);

/*
 * PROC1_$VT_INT - Virtual timer interrupt handler
 * Original address: 0x00e1491e
 */
/* 0x00E1494A: writes the PCB's 6-byte CPU clock through argument 1 */
void PROC1_$VT_INT(clock_t *cpu_time_out);

/*
 * PROC1_$SET_TS - Set timeslice value
 * Original address: 0x00e14a08
 */
void PROC1_$SET_TS(proc1_t *pcb, int16_t value);

/*
 * PROC1_$TS_END_CALLBACK - Timeslice end callback
 * Original address: 0x00e14a70
 */
void PROC1_$TS_END_CALLBACK(void *arg);

/*
 * PROC1_$INIT_TS_TIMER - Initialize timeslice timer for process
 * Original address: 0x00e14b12
 */
void PROC1_$INIT_TS_TIMER(uint16_t pid);

/*
 * ============================================================================
 * Information Functions
 * ============================================================================
 */

/*
 * Process info structure returned by PROC1_$GET_INFO
 */
typedef struct proc1_$info_t {
    uint16_t    flags;          /* 0x00: the pri_min:pri_max WORD at PCB+0x54
                                 *       (`move.w (0x54,A3),(A2)' 0x00E14FA6) */
    uint16_t    usr;            /* 0x02: User status register */
    uint32_t    upc;            /* 0x04: User PC */
    uint32_t    usp;            /* 0x08: User stack pointer */
    uint32_t    usb;            /* 0x0C: user stack base - a LONGWORD:
                                 *       PROC1_$GET_INFO_INT `move.l A4,(A3)'
                                 *       0x00E20F60 */
    union {
        uint8_t cpu_total[8];   /* 0x10: byte view (proc2 copies zombie usage here) */
        struct {
            clock_t  time;      /* 0x10: PCB cpu_total:cpu_usage, doubled by ADD48 */
            uint16_t state;     /* 0x16: PCB state word, copied by the 8-byte block move */
        } cpu;
    };
} proc1_$info_t;

_Static_assert(__builtin_offsetof(proc1_$info_t, flags) == 0x00, "proc1_$info_t.flags");
_Static_assert(__builtin_offsetof(proc1_$info_t, usr) == 0x02, "proc1_$info_t.usr");
_Static_assert(__builtin_offsetof(proc1_$info_t, upc) == 0x04, "proc1_$info_t.upc");
_Static_assert(__builtin_offsetof(proc1_$info_t, usp) == 0x08, "proc1_$info_t.usp");
_Static_assert(__builtin_offsetof(proc1_$info_t, usb) == 0x0C, "proc1_$info_t.usb");
_Static_assert(__builtin_offsetof(proc1_$info_t, cpu_total) == 0x10, "proc1_$info_t.cpu_total");
_Static_assert(__builtin_offsetof(proc1_$info_t, cpu.state) == 0x16, "proc1_$info_t.cpu.state");
_Static_assert(sizeof(proc1_$info_t) == 0x18, "proc1_$info_t size");

/*
 * PROC1_$GET_INFO - Get process information
 * Original address: 0x00e14f52
 *
 * Parameters:
 *   pidp - Pointer to process ID
 *   info_ret - Pointer to proc1_$info_t structure to fill
 *   status_ret - Status return pointer
 */
void PROC1_$GET_INFO(int16_t *pidp, proc1_$info_t *info_ret, status_$t *status_ret);

/*
 * PROC1_$GET_INFO_INT - Get process info (internal, extracts registers from stack)
 * Original address: 0x00e20f12
 *
 * Parameters:
 *   pid - Process ID
 *   stack_base - Base of stack
 *   stack_top - Top of stack
 *   usr_ret - Pointer to receive user SR
 *   upc_ret - Pointer to receive user PC
 *   usb_ret - Pointer to receive user stack base
 *   usp_ret - Pointer to receive user SP
 */
void PROC1_$GET_INFO_INT(uint16_t pid, void *stack_base, void *stack_top,
                         uint16_t *usr_ret, uint32_t *upc_ret,
                         uint32_t *usb_ret, uint32_t *usp_ret);

/*
 * Process list entry structure (4 bytes)
 */
typedef struct proc_list_entry_t {
    uint16_t pid;       /* Process ID */
    uint16_t type;      /* Process type */
} proc_list_entry_t;

/*
 * PROC1_$GET_LIST - Get list of bound processes
 * Original address: 0x00e15362
 *
 * Parameters:
 *   count_ret - Pointer to receive count of processes found
 *   list_ret - Array to receive process info
 */
void PROC1_$GET_LIST(int16_t *count_ret, proc_list_entry_t *list_ret);

/*
 * PROC1_$GET_USP - Get user stack pointer
 * Original address: 0x00e20f0c
 */
void *PROC1_$GET_USP(void);

/*
 * ============================================================================
 * Address Space Functions
 * ============================================================================
 */

/*
 * PROC1_$SET_ASID - Set address space ID for current process
 * Sets the ASID in the current PCB and installs it in the MMU.
 * Original address: 0x00e148f8
 */
void PROC1_$SET_ASID(uint16_t asid);

/*
 * ============================================================================
 * Interrupt Handling
 * ============================================================================
 */

/*
 * PROC1_$INT_ADVANCE - Advance interrupt handling
 * Original address: 0x00e208f6
 */
void PROC1_$INT_ADVANCE(void);

/*
 * PROC1_$INT_EXIT - Exit from interrupt
 * Original address: 0x00e208fe
 */
void PROC1_$INT_EXIT(void);


/*
 * proc1_$release_tail - shared lock/inhibit release epilogue
 *                       (0x00E20EB6 .. 0x00E20EEE)
 *
 * In the binary this is not a callable routine; it is a run of straight-line
 * code that THREE entry points branch into:
 *   - ML_$UNLOCK          (`beq.w 0x00E20EB0` / `bra.w 0x00E20EB6` at 0x00E20BAE)
 *   - ML_$EXCLUSION_STOP  (falls out of 0x00E20EAC)
 *   - PROC1_$INHIBIT_END  (falls out of 0x00E20EAC; 0x00E20EA2 is both this
 *                          function's entry and ML_$EXCLUSION_STOP's
 *                          no-waiter block)
 * It is expressed here as a static inline so all three C files emit exactly
 * the same sequence.  It lives in the PROC1 public header because every
 * routine it calls belongs to PROC1 and because ML branches into it.
 *
 * Entry conditions in the original: A1 = pcb, IPL = 7.
 * Exit: falls out through `andi #-0x701,SR` (SET_IPL0), i.e. it FORCES the
 * interrupt priority level to 0 rather than restoring a saved SR.
 *
 * NOTE: proc1_$reorder_if_needed, proc1_$remove_from_ready_list,
 * proc1_$add_ready_body and PROC1_$DISPATCH_INT2 are all reached with
 * `bsr.w` and take their PCB argument in A1 (register convention);
 * only PROC1_$TRY_TO_SUSPEND is called with a stack argument
 * (`pea (A1)` / `jsr` / `addq.w #4,SP` at 0x00E20ED8).  The C prototypes
 * declare a normal (pcb) parameter; the m68k register-argument forms live
 * in the proc1 sau2 assembly sources.
 */
static inline void proc1_$release_tail(proc1_t *pcb)
{
    uint8_t pri_flags;

    /* 0x00E20EB6: bsr.w 0x00E207D8 */
    proc1_$reorder_if_needed(pcb);

    /* 0x00E20EBA: tst.l (0x40,A1) / bne.b 0x00E20EE6 */
    if (pcb->resource_locks_held == 0) {
        /*
         * 0x00E20EC0: bclr.b #0x4,(0x55,A1)
         * pri_max is the byte at PCB+0x55, so this is bit 4 (0x10) of that
         * byte.  bclr sets Z from the PREVIOUS value of the bit.
         */
        pri_flags = pcb->pri_max;
        pcb->pri_max = (uint8_t)(pri_flags & ~0x10);

        if ((pri_flags & 0x10) != 0) {
            /* 0x00E20EC8 / 0x00E20ECC: re-insert at the un-boosted priority */
            proc1_$remove_from_ready_list(pcb);
            proc1_$add_ready_body(pcb);
        }

        /*
         * 0x00E20ED0: btst.b #0x2,(0x55,A1) -- deferred suspend pending.
         * This is bit 2 (0x04) of the byte at 0x55, NOT 0x400.
         */
        if ((pcb->pri_max & PROC1_FLAG_DEFER_SUSP) != 0) {
            /* 0x00E20ED8: pea (A1) / jsr PROC1_$TRY_TO_SUSPEND / addq.w #4,SP */
            PROC1_$TRY_TO_SUSPEND(pcb);
            /* 0x00E20EE2: movea.l PROC1_$CURRENT_PCB,A1 */
            pcb = PROC1_$CURRENT_PCB;
        }
    }

    /* 0x00E20EE6: bsr.w 0x00E20A24 */
    PROC1_$DISPATCH_INT2(pcb);

    /* 0x00E20EEA: andi #-0x701,SR -- forced IPL 0, not an SR restore */
    SET_IPL0();
}

#endif /* PROC1_H */
