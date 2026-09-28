---
name: proc1-create-bind-waitn
description: Recovered ABIs from re-emitting PROC1_$CREATE_P / BIND / ALLOC_STACK / EC_WAITN (2026-09): packed create arg, pri_max 0x0A, lowest-index WAITN result, shared ready-list tail
metadata:
  type: project
---

Verified against the disassembly during the proc1 re-emission batch.

- **PROC1_$CREATE_P (0xE15148)**: the packed longword argument is
  `stack_size << 16 | type` (`move.l #0x800000f` = 0x800-byte stack, type 0xF).
  Failure paths return the TYPE word, not 0. Only `tst.w (2,A2)` (low status
  word) is tested. Jump table at 0xE1518A: types 3,4,5,10 -> ws_param 5,
  type 8 -> 6, everything else 0.
- **PROC1_$BIND (0xE14D1C)**: scans PCBS slots 3..0x40; writes word 0x000A over
  pri_min:pri_max, i.e. the process is born BOUND|SUSPENDED and PROC1_$RESUME
  starts it. The 8-byte template at 0xE14E1C (00..00 10) sets state = 0x10.
  INIT_STACK gets &arg1 (entry) and &arg2 (initial_sp); arg3 goes to
  OS_STACK_BASE[pid]. The "no PCB" exit returns an unwritten D2.
- **PROC1_$ALLOC_STACK (0xE1501A)**: small (< 0x1000 after rounding) grows
  LOW upward, result = LOW + size + 0x400; large grows HIGH downward,
  result = old HIGH, new HIGH = HIGH - size - 0x400; exactly-0x1000 requests
  pop STACK_FREE_LIST first (result = link + 4). WP_$CALLOC failure is
  overwritten with status 0xA0009 and mapped pages are not unmapped.
- **PROC1_$EC_WAITN (0xE2065A)**: linking stops at the FIRST satisfied
  eventcount; teardown walks every linked cell newest-first and overwrites D0
  with each satisfied index, so the result is the LOWEST satisfied index + 1
  (0 if none, count itself if count <= 0). IPL raised at entry, forced 0 at
  exit. Waiter cell = ec_$eventcount_waiter_t {val, prev, next, pcb}; an
  eventcount's (4)/(8) links overlay a cell's prev/next.
- **Ready-list insertion tail** at 0xE20862 is shared by add_ready_body
  (0xE20824, `bls` = FIFO) and insert_into_ready_list (0xE20844, `bcs` =
  LIFO); proc1/sau2/add_ready_body.s carries a private copy with the image's
  instruction sequence, so its `bhi` displacement cannot match the image.
- **0x00E2087C is an unreferenced 24-byte routine** (no map symbol, no
  Ghidra function, no xrefs) between ADD_READY's tail and GET_CPUT: A1 =
  PROC1_$CURRENT_PCB, IPL 7, remove + FIFO re-add, DISPATCH_INT, forced
  IPL 0.  Not emitted by the tree (bead source-268i).  It is NOT
  PROC1_$INT_EXIT (that is 0x00E208FE).
- **ready_list.s gate-alias convention (2026-09-19)**: on m68k
  `proc1_$add_ready_body` / `proc1_$reorder_if_needed` are `.set` to the
  stack-argument GATES (PROC1_$ADD_READY / PROC1_$REORDER_READY) so C
  callers work; register-convention asm callers must `bsr` the `_int`
  labels (clr_lock.s does).  A `bsr proc1_$add_ready_body` with A1 preloaded
  would clobber A1 from (4,SP).
- **Host trap**: `proc1_ts_slot_t` / `time_queue_elem_t` hold pointers, so the
  0x1A/0x1C stride asserts in proc1.h must be `#if defined(ARCH_M68K)` or every
  proc1 host test fails to compile.

**How to apply:** trust these over decompiler output; the tests in
proc1/test/test_{alloc_stack,bind,create_p,ec_waitn}.c pin each point.

## proc12 batch additions (2026-09-19)

- **Ready-list primitives are one asm file** now: proc1/sau2/ready_list.s
  holds 0xE206D2..0xE206EE and 0xE207D4..0xE2087C.  The register-convention
  bodies are exported as `*_int` (A1 = pcb) for clr_lock.s / set_lock.s /
  ec/sau2/advance_int.s; the C names proc1_$remove_from_ready_list,
  proc1_$reorder_if_needed and proc1_$add_ready_body are `.set` aliases of
  the stack gates PROC1_$REMOVE_READY / REORDER_READY / ADD_READY, so C
  callers keep the internal names on every target.  The portable C bodies
  are `#if !ARCH_M68K`.  sau2.ld gathers `.text.proc1_remove_ready`.
- **0xE2AFA0 is TIME_$WRT_TIMER(uint16_t *channel, uint16_t *value)**, not
  WRT_VT_TIMER; PROC1_$SET_VT passes &PROC1_$VT_TIMER_DATA (the constant
  word 2 at 0xE14A06 = the VT channel) and &pcb->vtimer.
- **PROC1_$TS_END_CALLBACK**: locks held -> REORDER_READY + bset #4; no
  locks -> REMOVE_READY + ADD_READY (the old C had the arms swapped).  *arg
  is the element address; pid = low word of elem->callback_arg.
- **PROC1_$TRY_TO_SUSPEND** writes the WORD at 0x54 (pri_min:pri_max) with
  `& 0xFFFB | 2` and calls ADVANCE (0xE20728, no IPL bracket/dispatch).
- **PROC1_$SET_PRIORITY's mode is a byte boolean** at (0xA,A6) (`move.b`);
  callers push `clr.w` to query.  Crash paths in SET_PRIORITY/SET_TYPE fall
  through after CRASH_SYSTEM.  Status 0xA000A = process_not_suspendable
  (UNBIND's crash cell at 0xE14F4E).
- **PROC1_$SET_VT / SUSPENDP / UNBIND** never write status_$ok themselves
  (UNBIND's status is whatever SUSPEND/SUSPENDP left).
- LOADAV: M$MIS$LLL returns a 32-bit long, so an average of 0x01000000
  overflows the product and wraps negative - reproduced, not fixed.
