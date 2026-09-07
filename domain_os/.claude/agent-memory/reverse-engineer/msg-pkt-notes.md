---
name: msg-pkt-notes
description: Recovered ABIs and data layouts in the MSG and PKT subsystems - MSG_$WAIT's 2-arg shape, the socket-descriptor alias, PKT_$SEND_INTERNET's 15 arguments, and PKT's module globals at 0xE24C9C
metadata:
  type: project
---

Verified against the disassembly while closing source-4v0i and source-hny4.

## MSG

- **MSG_$WAIT (0x00E59BA4)** takes exactly two arguments. It allocates its
  `status_$t` at `(-0x4,A6)` (`link.w A6,-0x8` / `pea (-0x4,A6)`), pushes only
  `(0x8,A6)` and `(0xc,A6)`, and returns the Domain boolean from
  `tst.l (-0x4,A6)` / `seq D0b`. It is `SVC_$TRAP2_TABLE[0x15]`, and TRAP #2 is
  the *two*-argument dispatcher — that table's arity is corroborating evidence
  for any handler in it.
- **`*(0xE28DB0 + socket*4)` is `SOCK_$EVENT_COUNTERS[socket - 1]`.** The
  compiler emits `movea.l #0xe28db4,A1` then `movea.l (-0x4,A0),A0`, which
  Ghidra renders as `&sock_spinlock + socket*4`. `sock.h` already spells this
  properly. The pointer is simultaneously an `ec_$eventcount_t *` and a
  `sock_$sock_t *`; the byte at `+0x15` is `queue_count`, so
  `move.b (0x15,A3),D1b` is "a packet is already queued", not a flag.
- **MSG_$SOCK_OWNERS = 0x00E80F5C** (= MSG_$DATA_BASE 0xE80D84 + 0x1D8), 8 bytes
  per socket indexed by socket number with slot 0 unused. Byte is
  `(0x3F - asid) >> 3` in *word* arithmetic with a logical shift; the bit is
  `asid & 7` (`btst.b D1,(0,A1,D0w)` numbers bits mod 8).
- `msg_$data_t` in msg_internal.h does not lay out to the binary (depth[224] at
  0x1E would end at 0x1DE, but the bitmaps start at 0x1D8) — see source-eq3o.

## PKT

- **Module base A5 = 0x00E24C9C = `PKT_$DATA`.** Confirmed by
  PKT_$NEXT_ID, PKT_$RECENTLY_MISSING, PKT_$NOTE_VISIBLE, PKT_$SEND_INTERNET,
  PKT_$LIKELY_TO_ANSWER and PKT_$PING_SERVER all doing `lea (0xe24c9c).l,A5`.
  `ping_server_flags` is at **+0x88**, not immediately after the ping template.
  `+0x5A` is the 2-byte ping request header (image value 1), not padding.
- **PKT_$SEND_INTERNET (0x00E1264E) takes 15 arguments** plus a 2-byte Pascal
  result slot; callers pop 0x34. Prologue offsets: 0x08 routing_key(4),
  0x0C dest_node(4), 0x10 dest_sock(2), 0x12 src_node_or(4), 0x16 src_node(4),
  0x1A src_sock(2), 0x1C pkt_info(4), 0x20 request_id(2), 0x22 template(4),
  0x26 template_len(2), 0x28 data(4), 0x2C data_len(2), 0x2E retry_hint(4),
  0x32 timeout_out(4), 0x36 status_ret(4).
  The last two are **both** word out-params written unconditionally by
  PKT_$BLD_INTERNET_HDR (`move.w #0x5,(A0)` at 0x00E1230E,
  `move.w #0x4,(A1)` at 0x00E12316) — neither may be NULL and they must be
  distinct locals. The *response timeout* comes from `timeout_out` (arg 14);
  `retry_hint` (arg 13) is the send retry limit. Several C call sites in the
  tree had these crossed or NULLed.
- **`pkt_$no_data` = the zero longword at 0x00E12BB4**, in the code region
  between PKT_$LIKELY_TO_ANSWER's `rts` and PKT_$PING_SERVER. Both ping senders
  pass its address as the "data" argument with data_len 0
  (`pea (0x120,PC)` at 0x00E12A92 and `pea (-0x148,PC)` at 0x00E12CFA).
- **APP_$RECEIVE's result record** (44 bytes, `app.h`) is +0x00 header VA,
  +0x04 header page (masked with 0xFFFFFC00 for NETBUF_$RTN_HDR), +0x08 the
  four data-buffer longwords, +0x1C routing key.
- `move.b Dn,-(A7)` writes the byte at the **even** (high) half of the pushed
  word: PKT_$NOTE_VISIBLE's caller does `move.b D6b,-(SP)` and the callee reads
  `move.b (0xc,A6),D1b`. Same convention as RIP_$FIND_NEXTHOP's flags byte.
- **PKT_$LIKELY_TO_ANSWER builds its `rip_$dest_addr_t` on an uninitialised
  stack slot**: `andi.l #-0x100000,(-0x1a,A6)` then `or.l D0,(-0x1a,A6)` keeps
  the stale top 12 bits of `host_lo`. Harmless because every consumer masks the
  node to 20 bits, but it must be reproduced, not "fixed".

Related: [[feedback-fidelity-gates]], [[domain-pascal-codegen-conventions]].
