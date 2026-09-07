---
name: file-lot-and-lock-init
description: "FILE_$LOCK_INIT (0xE32744) instruction map, the 1793rd LOT sentinel slot, and the fact that the per-process lock rows are cleared in full - recovered 2026-09-07 (bead source-0sgi)"
metadata:
  type: project
---

**`FILE_$LOCK_INIT` = 0x00E32744, 240 bytes.** Five steps, in this order:

1. 0xE32760-0xE32780: 58 rows x 150 words. `clr.w (-0x2662,A0)` with the
   displacement running 2,4,...,300 off `0xEA202C-0x2662 = 0xE9F9CA`, so the
   first word cleared is **0xE9F9CC** (xref 0xE3276C -> 0xE9F9CC confirms) and
   the **whole 300-byte row** goes, slots 1..150. The old
   `file_lock_table_entry_t.header` "preserved during init" comment was wrong.
   `clr.w (0x1d98,A1)` clears the row count at 0xEA3DC4 + asid*2.
2. 0xE32784-0xE327A8: free list over LOT entries **1..1792**
   (`move.w #0x6ff,D0w`). `movea.l #0xe935cc,A0` then `lea (0x1c,A0),A0`
   pre-biases A0 to the END of entry 1; D1 starts at 1. Writes ONLY
   `clr.b (-0x4,A1)` = refcount (+0x18) and `move.w D2w,(-0x8,A1)` = next
   (+0x14). Everything else in an entry survives init.
3. 0xE327AC: `clr.w (0x00e9f9c4).l` - a lone word 8 bytes below the
   per-process table base. Write-only in the entire image, and **not
   nameable**: the SAU2 SR10.2 map (see [[reference-sau2-domain-os-map]])
   puts it inside segment `FILE_$LOT_DATA` (E935CC, size 0x1086C, i.e.
   E935CC..EA3E38 - exactly through the per-ASID count array), and that
   segment exports no symbols. Closed, bead source-9r49.
4. Control block 0xE82128 (= `OS_DATA_SHUTWIRED`; `+0xC8` = 0xE821F0 is
   **FILE_$LOT_HASHTAB**, `+0x2CE` = 0xE823F6 is **FILE_$LOT_FREE**, both from
   the SAU2 map - the sr10.4 maps misleadingly call 0xE821F0's counterpart
   FILE_$ASID_LOCKS): `+0x2CE = 1` at 0xE327B8 **and again** at 0xE327E2
   (a genuine double write); 251 words cleared from +0xC8 (0xE327BE);
   `+0x2CC = 1` (0xE327E8); `UID_$GEN(+0xC0)`; base_uid = UID_$NIL with the low
   longword's bottom 20 bits replaced by NODE_$ME (0xE245A4); `clr.b +0x2D0`.
5. `EC_$INIT(0xE2C028)` at 0xE327CE, `REM_FILE_$UNLOCK_ALL` at 0xE32824.

**The 1793rd LOT slot is the free-list terminator.** The last loop iteration
stores 1793 into entry 1792's `next` (0xE3279E) and never initialises slot
1793, so its BSS zero is what makes `tst.w`/`beq` at 0xE5EBB4 in
`FILE_$PRIV_LOCK_$ALLOC_ENTRY` report "table full". `FILE_$LOCK_ENTRIES` is
therefore declared `[FILE_LOCK_ENTRY_COUNT + 1]`. 1792 is witnessed only by
this loop - nothing is labelled between 0xE9B1CC (table end) and 0xE9F9C4.

Base-constant loads worth citing: 0xE603FA, 0xE6056C, 0xE6087A, 0xE60A74,
0xE60AE0, 0xE60C70 all load `#0xe935cc`; 0xE603D4, 0xE609D6, 0xE60A06,
0xE60C0C, 0xE60C22 load `#0xea202c`.

See [[acl-image-cache]] and [[feedback_fidelity_gates]].
