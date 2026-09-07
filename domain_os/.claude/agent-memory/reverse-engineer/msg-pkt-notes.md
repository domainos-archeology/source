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
- **`msg_$data_t` at 0xE80D84 is `reserved[0x1E]` / `depth[0xE1]` at 0x1E /
  `ownership[0xE0][8]` at 0x1E0 / `open_count` word at 0x8E0, sizeof 0x8E2.**
  The apparent depth/ownership overlap is a **one-based array**: the 0x1D8
  ownership base is slot *zero* of an array indexed by the socket number, and
  socket 0 is rejected everywhere, so the first reachable bitmap is +0x1E0 -
  exactly where the depth table ends. Sockets run 1..0xE0 (MSG_$CLOSEI /
  MSG_$WAITI `ble #0xe0`; MSG_$OPENI `blt #0xe0`). Closed source-eq3o.

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
- **`pkt_$data_t` is 0xA8 bytes and 0x68 / 0x88 are two copies of one 0x20-byte
  `pkt_$info_t`**, byte-identical apart from the first word (0x0010 vs 0x0020).
  Both are pushed as PKT_$SEND_INTERNET's `pkt_info` argument
  (`pea (0x68,A5)` from LIKELY_TO_ANSWER, `pea (0x88,A5)` from PING_SERVER), so
  the old "reserved_76" hole was really the tail of the first record.
  `pkt_$info_t` (recovered from PKT_$BLD_INTERNET_HDR, its only full reader):
  +0x00 flags, +0x02 routing_type (1 local / 2 internet), +0x04 addr_type,
  +0x06 protocol (0x8031), +0x08 retry_limit, +0x0A field_0a, +0x0C field_0c,
  +0x0E addr[16]. Image values: retry_limit and field_0c are 0xFFFF.
- **PKT_$SEND_INTERNET's retry ceiling**: `tst.w (0x8,A4) / bne` at 0x00E126A0
  (NOT `ble` - the audit bead said otherwise). 0 selects 0xFFFF, and
  `cmpi.w #-0x1,D4w` at 0x00E1271E then replaces 0xFFFF with `*retry_hint`,
  which PKT_$BLD_INTERNET_HDR always sets to 5. So both 0 and 0xFFFF mean
  "five attempts". The loop test `cmp.w D4w,D3w / bcs` is **unsigned**, and the
  retry delay at 0x00E1277E runs even on the last attempt because it precedes
  the test.
- **NET_IO_$SEND's 9th argument is a two-word out record**
  (`net_io_$send_info_t {port_net, xmit_status}`), written on every path:
  loopback sets `{0, |= 0x8008}` (0x00E0E75C), a real send sets
  `{port->+0x2e, 0}` and hands `&xmit_status` to the driver (0x00E0E82E,
  0x00E0E870). PKT_$SEND_INTERNET gives up only on `{0, 0x2000}`. This is a
  *third* local, distinct from retry_hint/timeout_out - crossing them was the
  source-m8h7 bug.
- **A successful NET_IO_$SEND still falls into NETWORK_$RTNHDR**: D2 (the saved
  header VA) is only cleared on the failure path (0x00E1276E), so the common
  exit at 0x00E127B6 returns the header on both the success and the
  builder-error paths. Reproduce it; it is not a bug in the translation.
- **PKT_$LIKELY_TO_ANSWER builds its `rip_$dest_addr_t` on an uninitialised
  stack slot**: `andi.l #-0x100000,(-0x1a,A6)` then `or.l D0,(-0x1a,A6)` keeps
  the stale top 12 bits of `host_lo`. Harmless because every consumer masks the
  node to 20 bits, but it must be reproduced, not "fixed".

## The packet header (recovered 2026-09-06, source-82m6)

`pkt_$hdr_t` (pkt/pkt.h, 0x5C bytes, **packed - including the union's inner
structs**) is the buffer PKT_$BLD_INTERNET_HDR (0xE1202C) fills and
PKT_$BRK_INTERNET_HDR (0xE12328) parses.

- A **fixed 0x1E-byte part**, then a *variable routing area* whose length is
  the byte at +0x18 (`hdr_size`), then the caller's template. Both routines
  use `total = hdr_size + template_len + 0x1E` and put the template at
  `hdr + hdr_size + 0x1F - 1` (`pea (-0x1,A2,D3*0x1)`).
- `hdr_size` is 4 local / 0x28 internet, +6 for a long request id, +0x10 for
  the 16-byte address. 0x1E + 0x3E = 0x5C, exactly the record's end.
- +0x19 `route_count` indexes the routing area **as words**:
  `move.w (0x1e,A2,D6w*0x1)` with D6 = count*2 is the destination socket.
- +0x24 and +0x1E take the **LOW** word of the destination (`(-0x2e,A6)` is
  +2 within the longword at `(-0x30,A6)`), not the high word.
- `pkt_$inet_addr_t` = {net long, zero word, node long at +6, sock word} = 12
  bytes, appearing twice at +0x2E and +0x3A. The `andi.l #0xff` /
  `andi.l #-0x1000000` / `or.l` triple just zeroes those six bytes and then
  stores the node.
- The internet body at 0xE1217C runs on **every** type-2 path, error ones
  included, and stores the *nexthop* node (D1) not the destination.
- Two length caps that overlap by one byte: `total > 0x3B8` -> 0x11000A
  (`bls`), then `hdr_size + 0x1F + template_len >= 0x3B8` -> 0x110024
  (`bcs`). A total of exactly 0x3B8 passes the first and fails the second.
- Its layout `_Static_assert`s are **unguarded** (no pointer fields), which is
  what caught the missing `packed` on the union's inner structs - m68k aligns
  uint32_t to 2 so the bug was invisible in the target build.

`route_$port_t` +0x48 is `driver_info`, a **uint32_t target VA** (not a C
pointer - a real pointer broke `sizeof == 0x5C` on a 64-bit host and failed
route/test/test_process's `record_layouts`). It points at
`route_$driver_info_t`, whose word at +0x02 is the port's max data length,
used by PKT_$BLD_INTERNET_HDR (0xE1211E/0xE12136) and MSG_$$SEND
(0xE0DAD0/0xE0DAEC).

## MSG send path (recovered 2026-09-06, source-o80r / source-yo76)

- **A5 = 0xE242E4** in MSG_$$SEND. The first 0x14 bytes are the
  `ml_$exclusion_t` MSG_$OPENI/CLOSEI/FORK/SHARE_SOCKET pass; +0x14
  `dpage_va`, +0x18 `dpage_pa`, +0x1C `dpage_in_use`. **Declare the lock and
  the page as two separate objects**: `sizeof(ml_$exclusion_t)` is 0x12 on
  m68k but 0x20 on a 64-bit host, so embedding it moves everything after it.
- **MSG_$$SEND has 15 args**: 0x08 port_num(w), 0x0A routing_key, 0x0E
  dest_node, 0x12 dest_sock(w), 0x14 src_node_or, 0x18 src_node, 0x1C
  src_sock(w), 0x1E pkt_$info_t*, 0x22 request_id(w), 0x24 template, 0x28
  template_len(w), 0x2A data, 0x2E data_len(w), 0x30 net_io_$send_info_t*,
  0x34 status. The old dest_proc/msg_desc/type_val names were all wrong.
- **MSG_$SEND (0xE599FC) has 11 pointer args** and takes its packet info from
  the first 30 bytes of `msg_$data_t` - so that record's `reserved_00[0x1E]`
  is really a `pkt_$info_t` template; the caller's flags word overwrites its
  first word. Both wrappers return `net_io_$send_info_t.xmit_status`, the
  **second** word of the record.
- **Original defect at 0xE0DCA2**: D3 holds `port_num` until `clr.b D3b` at
  0xE0DBE0, but the early-error paths reach the cleanup with D3 still the
  port, so `port_num == -1` (D3b = 0xFF) drops `dpage_in_use` without ever
  having claimed it. Reproduce it.
- The local-delivery record at A6-0x40 **is** a `sock_$pkt_info_t`; the 6-byte
  TIME_$ABS_CLOCK result is written over its `src_addr`/`src_port` pair.
  SOCK_$PUT's boolean becomes bit 15 of `xmit_status`; when it is set the
  socket owns the buffer and nothing is released.
- **SOCK_$PUT's 2nd argument is a `sock_$pkt_info_t *`** - passed straight
  through to SOCK_$PUT_INT_INT which does `movea.l (0xc,A6),A2` then
  `(0x2a,A2)`. sock.h's `void **` is wrong (bead source-bpz8).
- MSG_$FORK loops sockets **1..0xE0 inclusive** (`moveq #0xdf` + dbf with A1
  pre-advanced by 8) and reads both ASIDs as **words**.
- MSG_$TEST_FOR_MESSAGE returns a **byte** boolean (`sne D0b`); both error
  exits leave D0 holding something unrelated (the socket number, then the
  bitmap byte index) - reproduce that rather than inventing 0.
- `app_$receive_rec_t` (app/app.h) is the owner's name for what PKT called
  `pkt_$recv_result_t`: `.reply` / `.data` / `.data_pages` / `.hdr_f12`.

Related: [[feedback-fidelity-gates]], [[domain-pascal-codegen-conventions]].
