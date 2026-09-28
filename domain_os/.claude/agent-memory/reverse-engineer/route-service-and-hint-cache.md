---
name: route-service-and-hint-cache
description: ROUTE_$SERVICE's request record, driver-info callback slots and status-restore rule; the HINT 2-entry cache bound and HINT_$INIT's module-block pointer.
metadata:
  type: project
---

Recovered 2026-09-07 while fixing beads source-t5pz / c49h / p3pi / 49lj.

**Why:** the disassembly review found four wrong arms in ROUTE_$SERVICE and a
three-iteration loop in the HINT cache that overran into the next global.

**How to apply:** treat the layouts below as settled; re-derive only if the
image disagrees.

## route_$short_port_t is ROUTE_$SERVICE's request record

`ROUTE_$SHORT_PORT` (0xE69C08) copies port+0x2C as ONE longword into +0x04, so
the record looked like it had a 4-byte "host_id" there.  ROUTE_$SERVICE's reads
split it: +0x04 is compared against `port->active` (0xE6A3A8) and +0x06 against
1 and 2 (0xE6A072).  The 12 bytes are
`{network, status, port_type, socket, queue_length}` — the same shape going in
as a request and coming back as the reply (ROUTE_$SERVICE overwrites the
caller's record with ROUTE_$SHORT_PORT on the way out, 0xE6A5CA).

`ROUTE_$SERVICE`'s first argument is a **16-bit Pascal SET**, not a struct
pointer: every test is `btst.b #n,(0x1,A3)` and callers pass `pea (d,PC)` at a
constant word (ROUTE_$SHUTDOWN's 0x0008 at 0xE6A65A, 0x0002 at 0xE6A65C).

## route_$driver_info_t has three callback slots ROUTE_$SERVICE uses

- `+0x14 leave_status_1` — run when the port leaves status 1 (0xE6A416)
- `+0x18 enter_status_1` — run once the port has reached status 1 (0xE6A53A)
- `+0x1C attach_service` — run right after leave_status_1 succeeds (0xE6A43E)

`+0x14` / `+0x18` take `(uint16_t *socket_ptr, status_$t *)` with no result
slot (`addq.w #8,SP`).  `+0x1C` has the SAME shape as `+0x24 set_service`:
a word result slot plus five args
`(&port->socket, &0xE6A02C, 0, &local, status_ret)`.  All held as 32-bit VAs.

## The status-restore rule at 0xE6A518 has THREE outcomes, not two

```
tst.l (A0) / bne.b 0xE6A590   -> status != 0: port->active = old_status
cmpi.w #0x1,(0x2c,A2) / bne   -> status == 0, active != 1: NO restore at all
                              -> status == 0, active == 1: close-side cleanup
```
An `if (ok && active==1) {...} else { restore }` is wrong: the middle case
falls through untouched.

## The RIP hop-count cells are not interchangeable

Three `RIP_$UPDATE_D` pairs, and only the **old-network removal** pair passes
0xE69FB0 (= 0x0010).  The port-0 re-announce and the new-network add pass
0xE6A5D8 (= 0x0000).  Op cells: 0xE69FAE = 0x00 (standard), 0xE6A5DA = 0xFF.
0xE6A02C is four zero bytes (the null service record).

## ROUTE_$CLOSE_PORT (0xE69EC2) is a NESTED procedure

Reached with `bsr.w` and **no pushed arguments**; it does `movea.l (A6),A2` and
reads the caller's args at (0xc,A2)/(0x10,A2) *and* the caller's locals at
(-0x48,A2) and (-0x62,A2).  The (-0x62,A2) read at 0xE69F2A happens before
ROUTE_$SERVICE ever writes that slot.  Still emitted as its own file; bead
source-kc3d tracks folding it in.

## The HINT local cache is exactly 2 entries

`HINT_$INIT_CACHE` (0xE313E0), `HINT_$LOOKUP_CACHE` (0xE49D2A) and
`HINT_$ADD_CACHE` (0xE49DAC) all use `moveq #0x1,D0` + `dbf`, i.e. **2**
iterations.  A third would hit `hint_globals_t.hintfile_uid` at globals+0x18
and, on the write side, `hintfile_ptr` at globals+0x20.  The SAU2 map's
`D E7DB50 HINT_ size = 28` is the whole record.

`HINT_$LOOKUP_CACHE`'s expiry branch (`bge.b 0xE49D66`) goes to the **loop
step**, not the exit: an expired match keeps scanning, and the fall-through at
0xE49D70 always clears the result byte.

## HINT_$INIT keeps its mapped pointer in the module block

0xE312C0 stores MST_$MAPS' result into globals+0x20 (0xE7DB70)
**unconditionally, before the status test**, and the version test at 0xE312F8
reads back through that cell — not through `HINT_$HINTFILE_PTR` (0xE2459C,
named by the map in the NET_ASM segment), which entry cleared.  A version-1
file therefore returns with the module block set and HINT_$HINTFILE_PTR null.

Constant cells (0xE313AA..): len 0x0014, lock index 0x0000,
**lock mode 0x0004**, rights 0x0000, path "`node_data/hint_file" (20 bytes, no
NUL).  FILE_$LOCK reads args 2 and 3 as words and arg 4 as a single BYTE;
its 5th argument is a real frame slot (A6-0x1C) that FILE_$LOCK never reads.

See also [[byte-pool-vs-typed-array]], [[module-base-inherited-a5]].

## The hint FILE walkers both ascend (and the +0x10 pointer bias)

`HINT_$GET_HINTS` (0xE49966) and `hint_$add_internal` (0xE49A2C) hold the slot
pointer **biased by +0x10** and read the key at `(-0x10,An)`.  Starting it at
`bucket_base + 0x1C` therefore lands on `hintfile + 0x54*b + 0x0C`, i.e. slot 0
— the +0x0C is `hint_file_t.buckets`, not part of the bias.  Both step
**forward** (`lea (0x1c,A0),A0`), and GET_HINTS steps its address pointer
forward too (`addq.l #0x8,A2`).  Slot fields off the biased pointer:
`(-0xc)/(-0x8)` = addrs[0], `(-0x4)/(A0)` = addrs[1], `(0x4)/(0x8)` = addrs[2].

Two traps in `hint_$add_internal`:

- 0xE49AA8 `cmp.l (0x4,A2),D6 / beq` **skips** the addrs[1]→addrs[2] shift when
  addrs[1] already names the node being promoted; the shift runs only when
  they differ.
- 0xE49B40 builds the recursive call's UID in a frame slot that is **never
  fully initialised**: the high longword at A6-0x10 is never written and the
  low is `(stale & 0xFFF00000) | node_id`.  Harmless because the callee masks
  with 0xFFFFF, but reproduce it as an uninitialised local.

`GET_HINTS`' trailing NODE_$ME entry is written at pair index `count-1` and
does **not** bump the count; `cmpi.l #0x4 / bls` is unsigned, so keys 0..4 get
no self entry.

## ROUTE_$READ_USER_STATS has a fifth argument nobody reads

Frame (`link.w A6,-0x14`): +0x08 socket_ptr, +0x0C stats_buf, **a WORD at
+0x10 that is never referenced**, +0x12 length_ret, +0x16 status_ret.  It is a
real argument — `NET_IO_$DEVICE_STAT` pushes five plus a discarded word result
slot into the driver's +0x0C entry (0xE5A3FE-0xE5A414), the third being
`move.w (0xc,A6),-(SP)`.  `RING_$GET_STATS` (0xE76950) has the identical frame
and ignores it too.  Meaning unrecovered.

The routine reads `port+0x36` as a **signed word** bucket count (`bmi`) and
`port+0x34` as a **longword** of the same count — the word is that longword's
low half, so route_$port_t carries them as two words.
