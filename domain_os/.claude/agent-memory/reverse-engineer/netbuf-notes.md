---
name: netbuf-notes
description: Recovered NETBUF layouts and idioms - the globals at 0xE245A8, the MMAPE free-list link at entry offset 0x06, the 1KB header-buffer fields, and the ARCH_VA_TO_PTR idiom for host-testable VA fields
metadata:
  type: project
---

Verified against the disassembly while closing source-ltga.

- **`netbuf_globals_t` base A5 = 0x00E245A8.** `va_slots[192]` at 0x000,
  **`clock_t delay_time` at 0x300** (NOT a `time_queue_t` - both waiters push
  `pea (0x300,A5)` as TIME_$WAIT's *delay*, 0x00E0EE18 / 0x00E0EFE2), pad at
  0x306, `spin_lock` 0x308, `dat_allocs` 0x30C, `hdr_allocs` 0x310,
  `dat_delays` 0x314, `hdr_delays` 0x318, `dat_lim` 0x31C, `dat_cnt` 0x320,
  `dat_top` 0x324, `hdr_top` 0x328, `va_top` 0x32C, `va_base` 0x330,
  `hdr_alloc` word 0x334.
  TIME_$WAIT's *type* is the constant zero word at **0x00E0EEB2** in the code
  region (`pea (0x94,PC)` / `pea (-0x136,PC)`).

- **The data free-list link is the MMAPE word at entry offset 0x06**, i.e.
  `mmape_t.prev_vpn`, not `next_vpn` at 0x0A. Every site spells it
  `movea.l #0xeb4800,A0 / lsl.l #0x4,Dn / lea (0,A0,Dn),A1` then
  `(-0x1ffa,A1)`: 0xEB4800 - 0x1FFA = 0xEB2806 = MMAPE base + 6
  (0x00E0EAA2, 0x00E0EADA, 0x00E0EF64, 0x00E0F086).

- **`dat_top` at 0x324 is a longword written whole but read as its low word at
  0x326** (`move.l pages[k],(0x324,A5)` vs `move.w (0x326,A5),...`).

- **Header buffers are 1KB**: free-list link longword at **0x3E4**, the region
  **0x3E8..0x3FB** is zeroed on creation, and the buffer's physical address
  goes at **0x3FC**. NETBUF_$ADD_PAGES clears 0x3E8 with `clr.l` and
  0x3EC..0x3FB with a 16-byte block copy from four zeroed stack longwords;
  NETBUF_$GET_HDR clears only 0x3EC..0x3FB with a `dbf` loop.

- **NETBUF_$ADD_PAGES (0x00E0E928) takes two words**, and `hdr_alloc` is
  charged the **requested** count (`add.w D5w,(0x334,A5)`), not the clamped
  one. The data pages are chained **forward** - `MMAPE[pages[n-1]].link =
  pages[n]` for n = hdr_take+1 .. total-1 - because the loop's MMAPE index
  comes from `(-0x214,A1)` (element n-1) while the stored word comes from
  `(-0x20e,A2)` (element n, low half). The final trim calls
  NETBUF_$DEL_PAGES(0, dat_lim - dat_cnt) as a **word** subtraction, so the
  count is negative.

- **Host-testable target VAs**: route buffer field access through
  `ARCH_VA_TO_PTR` (arch/arch.h). On m68k it is the identity cast; a host test
  sets `ARCH_HOST_VA_BASE` to its own arena and stores arena offsets in the
  uint32_t VA fields. `netbuf/test/test_add_pages.c` and
  `xns/test/test_idp_send.c` both use it. Two traps:
  **`ARCH_VA_TO_PTR(0)` returns NULL** (virtual address zero is nil on the
  target), so a mock that hands out arena *offsets* must count from 1, not 0,
  or the code under test dereferences NULL. And a host CRASH_SYSTEM mock must
  `longjmp` rather than return, or the code under test runs on past a fatal
  check (NETBUF_$ADD_PAGES then overruns its 0x80-entry page array).

Related: [[msg-pkt-notes]], [[feedback-fidelity-gates]].
