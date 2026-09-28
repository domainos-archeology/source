---
name: proc2-create-debug-pass
description: PROC2_$CREATE / COMPLETE_VFORK / BUILD_INFO / debug-family recoveries (2026-09-19) - MST_$MAP_INITIAL_AREA's 7 params, the shared 0xE41A20 / 0xE735F4 cells, the create status bug, the andi.b #0x7f flag merge.
metadata:
  type: project
---

Verified against the disassembly during the proc2 re-emission batch.

- **MST_$MAP_INITIAL_AREA (0x00E42E9E) has SEVEN params**: code_desc L, asid W,
  parent_uid ptr, map_param L, area_kind W (+0x16), touch boolean byte (+0x18),
  status ptr. PROC2_$CREATE pushes `move.l #0x70000` (kind 7, touch false);
  PROC2_$COMPLETE_VFORK pushes `st` then `move.w #7` (touch true). The old
  prototype merged them into one longword "flags".
- **Shared pea cells**: 0x00E41A20 (`00 00 00 01`, the XPD_$WRITE byte count)
  is used by both DEBUG_SETUP_INTERNAL and DEBUG_CLEAR_INTERNAL -> one
  definition `PROC2_$DEBUG_XPD_WRITE_LEN` in debug_setup_internal.c.
  0x00E735F4 (`00 00 40 00`) is used by PROC2_$SET_VALID and COMPLETE_VFORK ->
  `proc2_$map_area_size_00e735f4` defined in set_valid.c. Both declared in
  proc2_internal.h. 0x00E73860 is the `ff` boolean byte for COMPLETE_VFORK's
  MST_$MAP_AREA_AT (the following bytes are the next function's prologue).
- **PROC2_$CREATE** (0x00E726EC): MST_$ALLOC_ASID is handed the CALLER's
  status_ret; on failure the code sets its bit 31 then branches to the
  cleanup tail, which finally stores the never-written A6-0x18 local -
  same original bug shape as PROC2_$FORK (bead created 2026-09-19).
  `andi.b #0x7f` + `or.b` at 0x00E727E6 = flags = (flags & 0x7FFF) |
  ((arg & 0x80) << 8) - it CLEARS the 0x8000 init bit. entry+0x6C takes
  *code_desc and entry+0x68 takes *user_data (0x00E7280C/0x00E72810).
  Both allocated-list back-link writes (0x00E727A0, 0x00E72B64) are
  unconditional (may hit entry(0)+0x14).
- **PROC2_$COMPLETE_VFORK** (0x00E73638) also stores user_data at entry+0x68
  (`move.l D3,(-0x7c,A3)` 0x00E736C8) and reads the creation record from
  entry+0x6C (0x00E737E0); the first re-emission had the +0x68 store on
  cr_rec_2 (fixed in the 2026-09-19 review pass).
- **cr_rec_t** (creation record via entry+0x6C) and **startup_context_t**
  (14 bytes, 0x10 below the reserve) now live in proc2_internal.h.
- **DEBUG_* fault-mode test** is `btst.b #4,(-0xb9,An)` = LOW byte of flags,
  i.e. flags & 0x0010 (not 0x1000). DEBUG_UNLINK crashes with the
  0x00E41948 cell = status_$proc2_uid_not_found when the target is not on
  its debugger's list.
- **BUILD_INFO_INTERNAL**: the "neither bound nor zombie" arm (0x00E40C28)
  returns without the common tail; the zombie arm writes 6 bytes of
  entry+0xA4 into proc1_info.cpu_total (long + word) and 5 longwords into
  out+0xD0.  proc_info_combined_t cannot be packed (PROC1 calls take
  member addresses) so its offset asserts are ARCH_M68K-only.
- proc1/proc1.h had an unconditional `proc1_ts_slot_t` size assert that
  failed on the host for a while (time_queue_elem_t is 0x1C there); when a
  foreign header breaks every host test, compile your own tests against a
  scratchpad overlay copy of the header (`-I overlay` first) to verify them.

See [[dxm-proc2-mst-notes]], [[proc2-self-index]].

## Second batch (INIT / INFO / GET_INFO / GET_UPIDS / DETACH), same day

- **PGROUP_TABLE is 71 slots (0..70)**: INIT clears 1..70 (`moveq #0x45`+dbf
  from 0xEA9454), PGROUP_FIND_BY_UPGID scans 70, PGROUP_SET_INTERNAL's bound
  is `cmpi.w #0x46 / ble`; 0xEA944C + 71*8 = end of PROC2_$DATA.  It was 70.
- **GET_UPIDS / GET_MY_UPIDS output order is (upid, PARENT upid|1, upgid|0)**
  - the old prototypes had uppid/upgid swapped.  audit/log_event_s.c still
  names record+0x44 "upgid" and +0x42 "uppid"; those field names are
  probably swapped too (not fixed - outside proc2).
- **PROC2_$INIT returns uint32_t in D0** (boot-shell header longword 4, or
  the tape/floppy entry point), not a status; OS_$INIT stores and ignores it.
  Its constant pool is 0x00E30892..0x00E3094F; the proc_dir path is
  "`node_data/proc_dir" (backquote), MST_$MAP maps only the 10-byte header
  (length cell = 10) and MST_$MAP_AT is then given that header record.
  entry(1)+0x68 = AS_$STACK_HIGH and +0x6C = AS_$CR_REC (old C had them
  swapped); the boot-flags word is stored 6 bytes below the stack top.
- **TAPE_$BOOT takes 2 args** (entry_point ptr cleared, status unused) and
  returns FALSE in D0b; tape.h/boot.c had a 1-arg version.
- PROC2_$INFO's first argument is compared against entry+0x96 (ASID), and
  its allocated-list scan runs BEFORE the lock.
- The 0x00E40DF0 cell (00 19 00 13) is shared by DETACH_FROM_PARENT and
  MAKE_ORPHAN; modelled as the existing PROC2_Internal_Error.

## proc2a2 batch (pgroup / set_* / signal / sig* / wait), 2026-09-19

- **PROC2_FLAG_SERVER is 0x0200** (SET_SERVER's andi.b/or.b hit the HIGH
  byte of +0x2A); it was recorded as 0x0002.  Same byte-op trap in
  SIGRETURN (onstack = 0x0400, not 0x0004), SIGPAUSE (0x4000),
  WAIT_REAP_CHILD (`bclr.b #5` high byte clears 0x2000), SET_SIG_MASK
  (byte 0 of clr[7]/set[7] -> 0x0400, byte 1 -> 0x0004).
- **PGROUP_DECR_LEADER_COUNT is a nested procedure** (`movea.l (A6),A2`
  static link; its status goes to the enclosing PGROUP_CLEANUP_INTERNAL's
  A6-0x8).  It signals 1 then **0x16** on orphaning (bead filed: 0x16 is
  SIGTTOU in proc2.h's BSD numbering, expected SIGCONT).
- **PGROUP_FIND_BY_UPGID** compares sign-extended arg vs zero-extended
  table word -> upgids >= 0x8000 never match.  PGROUP_SET_INTERNAL crashes
  with the 0x00E42024 cell = 0x190016 when all 70 slots are busy.
- **PROC2_$QUIT**'s cells: 0x00E3F158 = word 3 (SIGQUIT), 0x00E3F15C =
  longword 0x00120010 (the signal param), NOT 0.
- **SIGNAL_PGROUP_INTERNAL**'s "same session" test compares the entry with
  itself (A4 == A3), so any 0x16 passes the ACL gate; its final-status
  ladder lets zombie/not-found override permission_denied when nobody was
  delivered to.  PROC2_$SIGNAL's chain: debugger==caller, or same non-zero
  session with signal 0x16, or ACL(&caller->level1_pid, &target->level1_pid).
- **PROC2_$WAIT**: the second walk is over +0x24/+0x28 (the fields named
  first/next_debug_target_idx) via WAIT_TRY_ZOMBIE; the 0x68-byte record
  (proc2_wait_result_t in proc2_internal.h) is built locally and copied out
  only on success; +0x18/+0x1A must match on the child walk; result byte
  0x64 is cleared on the CALLER's record at entry.  WAIT_TRY_ZOMBIE takes
  4 real args (options at +0xA never read).
- **PROC2_$STARTUP** receives ctx+4 (startup_context_t.self_ptr), so its
  `(0x8,A2)` asid read is startup_context_t.asid (+0x0C).
- UID_TO_UPID / UPID_TO_UID leave the output indeterminate on not-found.
- SET_PGROUP's group check reads PGROUP[idx].session_id even for idx 0
  before testing idx.
- Foreign-header churn during the batch: proc1.h retyped
  PROC1_$SET_PRIORITY's mode to int8_t (PROC1_SET_PRIORITY_SET is now
  (int8_t)0xFF) and PROC1_$TST_LOCK to int8_t; mst.h's MST_$GET_VA_INFO
  changed twice.  Test mocks must track the header, not the old C.
