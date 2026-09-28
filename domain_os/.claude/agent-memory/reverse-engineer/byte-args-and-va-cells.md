# Byte arguments, VA cells and the packed clock_t

Notes from the retype pass on AST_/ROUTE_/AREA_/AUDIT_ (beads source-fan2,
source-00m7, source-o7gq, source-stvi, source-hi9m, source-efc9, source-no75).

## Domain BOOLEAN arguments are ONE byte, and the byte is the EVEN address

`move.b Dn,-(A7)` / `st -(A7)` decrement SP by 2 and write the byte at the
*new* (even) SP, so a boolean argument at frame offset `d` is read back with
`move.b (d,A6),Dnb` and the slot is two bytes wide.  A word-typed model of
such an argument forces callers to pass `0xFF00`, and any callee that
re-pushes it has to shift down by 8 - that shift is the tell.

Confirmed byte arguments:

| routine | address | instruction |
| --- | --- | --- |
| `AST_$INVALIDATE` arg4 | 0x00E0662E | `move.b (0x14,A6),D3b` @0x00E06644, `tst.b D3b`/`bpl` @0x00E066FC |
| `AST_$TRUNCATE` arg4 (out-cell) | 0x00E05C40 | `move.b D3b,(A0)` @0x00E05C70, `st (A1)` @0x00E05DB6 |
| `ast_$process_aote` args 2,3,4 | 0x00E01AD2 | `move.b (0xc/0xe/0x10,A6),D2b/D3b/D4b` @0x00E01ADA-0x00E01AE2 |
| `AST_$DEACTIVATE_SEGMENT` args 2,3 | 0x00E01950 | pushed as `move.b D3b,-(SP)` / `move.b D2b,-(SP)` @0x00E01B4A |

`ast_$process_aote`'s frame is aote 0x08, flags1 0x0C, flags2 0x0E,
flags3 0x10, status 0x12 - fourteen bytes, which is exactly what the callers
push (`pea` + `clr.l` + `clr.w` + `pea`).

## AST_$TRUNCATE's remote path does NOT pass the caller's result byte

0x00E06250 `pea (-0x40,A6)` gives `REM_FILE_$TRUNCATE` a local six-byte clock
cell (its fifth argument is a `clock_t *`, written at 0x00E61A14/0x00E61A18 or
by `TIME_$CLOCK`), and 0x00E0624C passes the CALLER's status pointer
(`move.l (0x16,A6),-(SP)`), not a local.  0x00E06270-0x00E062CC then copies
that clock into the AOTE at +0x28/+0x2C and +0x40/+0x44.  Tracked by
source-2ih2.

## route_$set_service_fn_t's fifth argument is the status return

Proven by the driver on the far end, not by `NETWORK_$SET_SERVICE` (which
passes an uninitialised local): `RING_$IOCTL` (0x00E76B2C) reads it as
`movea.l (0x16,A6),A3` and writes `0x310002` / `0` / `0x310001` through it.
`ROUTE_$SERVICE` agrees - 0x00E6A42E `pea (A0)` with A0 = its caller's status.
Argument 4 (frame 0x12) is still never read by anyone.

## Host-layout parity: uint32_t VA cells, not host pointers

A record whose image size must survive on a 64-bit host cannot hold host
pointers.  Spell those cells `uint32_t` and dereference with
`ARCH_VA_TO_PTR` / `ARCH_PTR_TO_VA` (arch/host/arch.h has a settable
`ARCH_HOST_VA_BASE` window, so a test can point it at its own arena and store
real 32-bit VAs).  Done for `area_$seg_table_t.next`/`.bitmap_ptr` (record now
0x0C on both) and `audit_data_t.pool_next`/`.pool_limit`.

`audit_data_t` still cannot drop its `#if defined(ARCH_M68K)` guard:
`buffer_base` (+0x88), `write_ptr` (+0x90), `event_count` (+0x198) and the
38-slot `hash_buckets` array (+0xB0) are still host pointers (source-9h07).
`ring_global_t` is likewise still guarded - it comes out 0x690 instead of
0x5D0 on the host because `ring_unit_t` holds pointers.

## clock_t is six bytes and now says so

`base/base.h`'s `clock_t` is `struct __attribute__((packed, aligned(2)))
{uint32 high; uint16 low;}` with `_Static_assert`s on size 6 / alignment 2.
Without the attribute the host pads it to 8/4 and every embedding record
shifts.  With it, `netbuf_globals_t` (0x338, clock at 0x300) and
`tpad_$globals_t` (0x20, clock at 0x04) assert unconditionally; new coverage
lives in `base/test/test_clock_layout.c`.
`audit_event_record_t` stays m68k-only for a different reason - its 32-bit
fields sit on odd word boundaries and callers take their addresses, so it
cannot be packed.

## Retyping a prototype in a shared header breaks foreign test stubs

Host tests declare their own mocks (`void AST_$TRUNCATE(..., uint8_t *result,
...)`) and also pull in the real header transitively, so a prototype change
turns into "conflicting types for ..." - a hard error, not a warning.  Grep
`<subsys>/test/` for the symbol before changing any public prototype.  The
m68k build does NOT warn on `int8_t *` vs `uint8_t *` (its flags are
`-std=c23 -O2 -Werror`, no `-Wall`), so only `make test` catches it.
