/*
 * proc1/proc1_internal.h - Internal PROC1 Definitions
 *
 * Contains internal functions, data, and types used only within
 * the proc1 subsystem. External consumers should use proc1/proc1.h.
 */

#ifndef PROC1_INTERNAL_H
#define PROC1_INTERNAL_H

#include "proc1/proc1.h"

/*
 * PROC1_ASM_DATA_SECTION - keep PROC1_$CURRENT inside the PROC1_ASM code
 * segment.
 *
 * In the image PROC1_$CURRENT is not part of a Pascal A5 data block: the SAU2
 * map puts it at 0xE20608, inside the PROC1_ASM segment (0xE1EAC8, size
 * 0x24A4) between DI_$Q_HEAD and PROC1_$AS_ID, i.e. in the same segment as the
 * EC/PROC1 assembly routines.  proc1_$process_exit_handler reads it with a
 * 16-bit PC-relative operand - `3f 3a fb 2e  move.w (-0x4d2,PC),-(SP)' at
 * 0xE20AD8 (0xE20ADA - 0x4D2 = 0xE20608) - so the cell has to stay within
 * 32KB of proc1/sau2/init_stack.o or the R_68K_PC16 relocation overflows
 * (source-uwxz).  Give it a section of its own, which the generated
 * build/sau2/layout.ld (tools/gen_layout_ld.py) links at its map position,
 * ahead of the ec/proc1 code, as svc/svc_internal.h does for the SVC
 * dispatch tables (source-a5t8).
 *
 * This is a code-segment cell, NOT an A5 module block, so it is deliberately
 * outside the scope of the `.moddata.<name>' scheme in
 * docs/design-per-process-data.md (source-0i3).
 */
#if defined(ARCH_M68K)
#define PROC1_ASM_DATA_SECTION  __attribute__((section(".text.proc1_asm_data")))
#else
#define PROC1_ASM_DATA_SECTION
#endif

/*
 * ============================================================================
 * Internal Function Declarations
 * ============================================================================
 */

/*
 * INIT_STACK - Initialize process stack for first dispatch
 *
 * Sets up a new process's initial stack so that when the
 * dispatcher context-switches to it, the process will begin
 * execution at the specified entry point.
 *
 * Parameters:
 *   pcb - Pointer to process control block
 *   entry_ptr - Pointer to entry point address
 *   sp_ptr - Pointer to stack pointer value
 *
 * Original address: 0x00E20AA4 (40 bytes)
 */
void INIT_STACK(proc1_t *pcb, void **entry_ptr, void **sp_ptr);

/*
 * proc1_$add_ready_body - FIFO priority-ordered ready list insertion
 *
 * Inserts a PCB into the ready list ordered by resource_locks_held
 * (descending) then state (descending), with FIFO ordering within
 * the same priority level (inserts AFTER equal-priority entries for
 * round-robin fairness).
 *
 * Contrast with proc1_$insert_into_ready_list which inserts BEFORE
 * equal-priority entries (LIFO within same priority).
 *
 * On m68k, the assembly version (sau2/add_ready_body.s) uses register
 * calling convention with A1 = PCB pointer and cannot be called from C;
 * C code must call PROC1_$ADD_READY (the stack-argument wrapper).  The
 * C version below is only built for non-m68k targets.
 *
 * Original address: 0x00e20824
 *
 * (The prototype itself now lives in proc1/proc1.h so that ML can use it.)
 */

/*
 * proc1_$insert_scan - the LIFO ready-list walk entered at 0x00E20854
 *
 * A0 = pos is the entry the walk starts at: PROC1_$READY_PCB from
 * proc1_$insert_into_ready_list (0x00E2084C), or the moved PCB's former
 * next from proc1_$reorder_if_needed (`bra.b 0x00E20854' at 0x00E2081E).
 * Portable C only; on m68k both entries are labels in
 * proc1/sau2/ready_list.s.
 */
#if !defined(ARCH_M68K)
void proc1_$insert_scan(proc1_t *pcb, proc1_t *pos);
#endif

/*
 * proc1_$get_current_cpu_time - the CPU-time helper at 0x00E208D0
 *
 * Register-convention assembly in the image (D1 = high, D0.w = low, A1
 * clobbered), `bsr'd by PROC1_$GET_CPUT, PROC1_$GET_CPUT8 and
 * PROC1_$GET_CPU_USAGE.  Defined in proc1/get_cput.c.
 */
void proc1_$get_current_cpu_time(uint32_t *high_out, uint16_t *low_out);

/*
 * proc1_$set_lock_body - Internal set lock implementation (assembly)
 *
 * Internal entry point for PROC1_$SET_LOCK, called with lock_id in D0.
 * Increments lock depth (PCB+0x5A), checks ordering, sets lock bit.
 *
 * Original address: 0x00e20ae8
 */
void proc1_$set_lock_body(void);

/*
 * proc1_$clr_lock_body - Internal clear lock implementation (assembly)
 *
 * Internal entry point for PROC1_$CLR_LOCK, called with lock_id in D0,
 * current PCB in A1, and interrupts disabled.
 * Clears lock bit, decrements lock depth, handles deferred operations.
 *
 * Original address: 0x00e20b9e
 */
void proc1_$clr_lock_body(void);

/*
 * PROC1_$LOADAV_CALLBACK - Periodic load average update callback
 *
 * Installed on TIME_$RTEQ by PROC1_$INIT_LOADAV (timer table entry 0).
 *
 * Original address: 0x00e14bda
 */
void PROC1_$LOADAV_CALLBACK(void);

/*
 * ============================================================================
 * Internal Data Declarations
 * ============================================================================
 */

/*
 * PROC1_$VT_TIMER_DATA - the timer-index word at 0x00E14A06
 *
 * A constant word (image bytes 00 02) in the PROC1_ code segment, reached
 * by `pea (0x1c,PC)' at PROC1_$SET_VT 0x00E149E8 and handed to
 * TIME_$WRT_TIMER as its by-reference timer index: channel 2 of the timer
 * chip at 0xFFAC00 is the virtual (CPU-time) timer.
 */
extern const uint16_t PROC1_$VT_TIMER_DATA;

/*
 * DAT_00e20606 - the "deferred-interrupt callback in progress" flag.
 *
 * It lives at 0xE20606, the byte immediately after DI_$Q_HEAD (0xE20602,
 * di/di.h); the SAU2 map's next symbol is PROC1_$CURRENT at 0xE20608, so the
 * two share one 6-byte map entry and the flag has no name of its own.
 *
 * Only proc1/sau2/int_handler.s touches it, always a byte
 * (`tst.b` at 0x00E20922, `st` at 0x00E20944, `sf` at 0x00E20956): the
 * interrupt tail sets it while it runs a DI callback with interrupts enabled
 * so a nested interrupt will not re-enter the queue.
 */
extern int8_t DAT_00e20606;

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */

/*
 * Bad_atomic_operation_err - the status proc1/sau2/dispatch.s hands
 * CRASH_SYSTEM when PROC1_$DISPATCH_INT is entered inside an atomic
 * operation.  The cell is a literal in the PROC1 assembly segment, reached by
 * `pea (d,PC)` from 0x00E209EE; image bytes at 0x00E20DE8: 00 0a 00 07.
 */
extern status_$t Bad_atomic_operation_err;

/*
 * Illegal_process_id_err - the status longword at 0x00E152E0, just ahead of
 * PROC1_$SET_TYPE.  One cell, reached PC-relative by four routines:
 *   PROC1_$SET_PRIORITY      `pea (0x80,PC)'   at 0x00E1525E
 *   PROC1_$SET_TYPE          `pea (-0x26,PC)'  at 0x00E15304
 *   PROC1_$GET_TYPE          `pea (-0x60,PC)'  at 0x00E1533E
 *   PROC1_$GET_ANY_CPUT      `pea (-0x12e,PC)' at 0x00E1540C
 *   PROC1_$GET_ANY_CPU_USAGE `pea (-0x17a,PC)' at 0x00E15458
 * Image bytes: 00 0a 00 01 (status_$illegal_process_id).
 */
extern const status_$t Illegal_process_id_err;

#endif /* PROC1_INTERNAL_H */
