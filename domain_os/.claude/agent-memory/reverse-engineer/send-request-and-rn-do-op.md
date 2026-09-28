---
name: send-request-and-rn-do-op
description: REM_FILE_$SEND_REQUEST's frame map and status exits, REM_FILE_$RN_DO_OP's per-opcode payload routing, and RIP_$INIT's two constant cells
metadata:
  type: project
---

Recovered while closing beads source-ldrp, source-0i5f, source-s5n9,
source-bkr7 and source-xrdt.

## REM_FILE_$SEND_REQUEST (0x00E60FD8)

Thirteen arguments; `link.w A6,-0xd8` plus a nine-register movem, so the
epilogue is `movem.l (-0xfc,A6)`.  Frame map (A6 displacements):
-0xD0 sock, -0xCE pkt_id, -0xCC reply_id, -0xCA retry_count, -0xC8
template_len, -0xC6 send_data_len, -0xC4/-0xC2 PKT_$SEND_INTERNET's two word
outputs (only **-0xC2**, the timeout, is read back), -0xC0 the data-page
request, -0xBE conn_state, -0xBC socket wait value, -0xB8 the quit snapshot,
-0xB4 deadline, -0xB0 status, -0xAC the socket eventcount, -0xA8 send data
pointer, -0xA4 bulk VA, -0xA0 bulk destination, -0x9C the RTN_HDR argument,
-0x98 bulk handle, -0x88 the 88-byte FIM cleanup record, -0x30 the
`app_$receive_rec_t`.

Status exits, all of them:
`0x000F0001` type-9 process (0x00E61002) - **before the socket**, so
`*packet_id` is not written; `0x000F0004` not-capable/non-local (0x00E6102A),
send failure and retry exhaustion (0x00E611BC); `0x00120010 | bit 31` on quit
(`bset.b #7` on the status's first byte, 0x00E614AA); `0x00110007` when
PKT_$LIKELY_TO_ANSWER says no (0x00E6150C); the server's own status from
`response+0x04` when `response[3] == request[3] + 1`, else `0x000F0003`.

The two CRASH_SYSTEM cells are `0x00E61530` = **0x00110005** and
`0x00E61534` = **0x00110001** - not 0x000F0004.  The SR10.2 table calls
0x00110005 "no available socket" (SR10.4 renamed it "receive process failed
to start"), which is the tree's single name for the code.

Conn states: 0 initial, 1 one timeout seen, 2 diskless mother (never gives up
and never probes), 3 confirmed answering.  A "server busy" reply (response's
first word 0xFFFF) sets state 3 first, so **after a busy reply the call can
never reach the 0x00110007 probe** - it ends on the retry budget with
0x000F0004.

`bulk_handle` (A6-0x98) is read on the first receive-error pass before it is
ever written - an original hazard, preserved.

## REM_FILE_$RN_DO_OP (0x00E61538)

Its two ACL status tests are `tst.w (0x6,A0)` - only the **low word** of the
reply status longword at response+0x04.

The request record's tail from +0x8E is a union keyed on the DIR opcode at
+0x03:
- **0x58 (LIST)**: source VA is the LONG at **+0x8E**, byte count the WORD at
  **+0x92** (the tree had them swapped); inline copy target +0xB0; bulk reply
  VA +0xAC with bulk_max 0x400; cap `base_len + len > 0x122`.
- **0x3C (GET_ENTRY)**: dest offset WORD +0x8E, byte count WORD +0x90, source
  LONG +0x92; the copy target is `record + word(+0x8E) + 0x96` and A2 is left
  holding `record + word(+0x8E)`; cap 0x108.
- **0x42 (READ_DIR)**: reply max LONG +0x96 clamped to 0x400 with an
  **unsigned** compare, reply VA LONG +0x9A.
- **0x3E (READ_LINK)**: uses the 0x3C view - VA +0x92, length +0x90.
- anything else: extra_len 0, extra_data = the record, bulk_max 0, bulk_data
  = the reply buffer.

**On the 0x58 inline path A2 is never loaded**, so the `extra_data` the send
gets is the caller's leftover register.  extra_len is 0 there, so nothing
reads it.

`ACL_$IN_SUBSYS` is tested with `tst.b D0b` - cast its int16_t result to
int8_t before the sign test.

## RIP_$INIT (0x00E2FBD0)

`lea (0xe3502c).l,A5` sets A5 to a **different** block from RIP_$DATA
(0xE26258): the map calls 0xE3502C "D E3502C RIP_WIRED size = 4", a four-byte
zero cell inside OS_INIT_DATA, and `pea (A5)` hands it to PKT_$SEND_INTERNET
as the 2-byte request template.  The data argument is `pea (0x14c,PC)` =
**0x00E2FDEC**, a zero longword just past the function's `rts`.  Neither is
NULL.  The three locks are reached through the literal 0xE26258, not A5.

The reply is read through `app_$receive_rec_t`: route port = **rec+0x18**
(`hdr_f06`), the VA handed to NETBUF_$RTN_HDR = rec+0x04 masked with
0xFFFFFC00, the page vector for PKT_$DUMP_DATA = rec+0x08.  RIP_$UPDATE_INT
takes **six** arguments (long, addr, word, word, boolean-byte-in-word,
status); the two calls differ only in the trailing boolean, and the compiler
merges two adjacent zero arguments into one `clr.l`, which is why the two
call sites look like they push different shapes.

`route_$port_t.active` in RIP_$FIND_NEXTHOP is tested `moveq #0x3c,D6 /
btst.l D5,D6` - a **bit number** selecting into 0x3C, i.e. active in
{2,3,4,5}, not a mask AND.

Related: [[rem-file-client-buffer-and-layouts]], [[route-rip-sock-layouts]],
[[rip-server-reemission]].
