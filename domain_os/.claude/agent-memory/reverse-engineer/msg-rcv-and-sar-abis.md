---
name: msg-rcv-and-sar-abis
description: Recovered MSG receive/SAR ABIs, the msg_$reply_hdr_t record, and the TIME itimer/queue argument orders found in the 2026-09-07 pass
metadata:
  type: project
---

Verified against the disassembly while closing source-xtsx, source-n89a and
source-6r3m.

## MSG receive path

- **MSG_$$RCV_INTERNAL (0x00E59548) takes 18 arguments**: 0x08 socket(w),
  0x0A dest_net, 0x0E dest_node, 0x12 dest_sock, 0x16 src_net, 0x1A src_node,
  0x1E src_sock, 0x22 `msg_$hw_addr_t *`, 0x26 msg_type, 0x2A template,
  0x2E template_max(w), 0x30 template_len_ret, 0x34 data, 0x38 data_max(w),
  0x3A data_len_ret, 0x3E ec_param1_ret, 0x42 ec_param2_ret, 0x46 status.
  Callers reserve a **2-byte Pascal result slot nobody reads**.
- The **`msg_$hw_addr_t` already in msg.h IS the 0x22 record**: proto_family
  from reply+0x14, `flags` = `(app_$receive_rec_t.flags_lo & 0x7F80) >> 7`
  (the socket queue depth), proto_type reply+0x15, proto_subtype reply+0x16,
  a single `move.l #0xffff,(0xa,A3)` giving reserved2=0 / reserved3=0xFFFF,
  and `inet_addr[16]` filled **only** when (proto_type, proto_subtype) is
  (2, 0x29).  That case also does `sub.w #0x10,(0x2,A2)` — it shortens the
  **received reply header in place**, not a local.
- **`msg_$reply_hdr_t`** (msg_internal.h, packed): 0x02 template_len,
  0x04 data_len, 0x06 msg_type, 0x08 dest_node, 0x0C dest_sock,
  **0x0E src_node (an odd-longword offset)**, 0x12 src_sock, 0x14/0x15/0x16
  the protocol triple.  It is what `app_$receive_rec_t.reply` points at.
- **MSG_$RCV has 12 args and MSG_$RCVI 16** — the old header had them
  misnamed from argument 6 on.  Both short forms (MSG_$RCV 0x00E59540,
  MSG_$RCV_HW 0x00E599EA, MSG_$RCV_CONTIG 0x00E5979A, MSG_$SAR 0x00E59DC4)
  return **only `hw_addr.proto_family`**, the record's first word.
- **MSG_$RCV_HW's error paths fall into that store** (0x00E59976 and
  0x00E5999A both branch to 0x00E599EA), so `*proto_family_ret` is written
  from an uninitialised local when the socket check fails.  Original defect.
- **MSG_$RCV_CONTIG's 6th argument is a pointer to the buffer POINTER**
  (0x00E59760 `movea.l (A0),A2`).
- `NETBUF_HDR_EC_PARAM1/2` (0x3E0/0x3E2) are the two words SOCK_$PUT stores
  and MSG reads back; they now live in netbuf/netbuf.h.

## MSG_$SARI (0x00E59DD4, 18 args at 0x08..0x4C)

Allocates a **temporary user socket** with SOCK_$ALLOCATE_USER(1, 1, 1,
0x400), takes a request id from PKT_$NEXT_ID, sends through MSG_$$SEND with
**port -1**, `src_node = NODE_$ME` and `src_node_or = -1`, then loops on
`EC_$WAIT({sock_ec, &TIME_$CLOCKH, &FIM_$QUIT_EC[asid]}, {val+1, deadline,
quit+1})`.  Index 1 gives 0x110007 (remote node failed to respond), index 2
gives 0x120010 (process quit) after copying the quit eventcount value into
FIM_$QUIT_VALUE[asid], index 0 receives — and **a receive error or a reply
whose msg_type differs from the request id goes back round the wait**.
SOCK_$CLOSE runs on every path except the allocation failure.
The wait value is incremented at the TOP of the loop, so the first wait
already asks for value+1.  MSG_$SAR builds the packet info by copying
`msg_$data_t.send_template` (30 bytes) and dropping the caller's flags word
on its first word — the same idiom as MSG_$SEND.

## SOCK

- **SOCK_$PUT's second argument is a `sock_$pkt_info_t *` passed by value all
  the way down**; the only extra dereference is 0x00E161D0
  `movea.l (A2),A0` / `move.b (0x15,A3),(0xf,A0)`, which stamps queue_count
  into the **header buffer** at +0x0F through `pkt_info->hdr`.
- `sock_$pkt_info_t.hdr` is now a **uint32_t target VA** (a `void *` made the
  0x40-byte record 8 bytes long on a 64-bit host and forced the layout
  asserts under `#if ARCH_M68K`).  Consumers use ARCH_VA_TO_PTR.
  `app_$receive_rec_t.reply/.data` still have the same problem — bead filed.
- **SOCK_MAX_NUMBER is 0xE0, not 0xDF**: SOCK_$INIT runs 224 passes from
  socket 1 (0x00E2FDF8) and SOCK_$PUT_INT accepts up to 0xE0 (`bls`).
  SOCK_$GET, SOCK_$OPEN and SOCK_$CLOSE bounds-check nothing at all.

## TIME

- **time_$clock_to_itimer / time_$itimer_to_clock take the DESTINATION
  first** (`movea.l (0x8,A6),A1` is the store target).  Both are 48-bit
  shifts by one through a temporary, so source and destination may alias.
  The "itimer" form is the same 6-byte clock_t record counting in units of
  two ticks.
- **TIME_$Q_ADD_CALLBACK is (queue, when, is_absolute, now, callback,
  callback_arg, flags, interval, qelem, status)**.  `when` supplies the
  expiry; `now` is what gets added when is_absolute is 0 AND what is handed
  to TIME_$Q_ENTER_ELEM (0x00E16E34).  sio/i_tstart.c passes NULL for `when`
  where 0x00E1C8DC pushes a real 6-byte local — a live NULL deref.
- **TIME_$VTQ is a real 64-entry array at 0xE2A4A0**, Pascal 1-based.
  0xE29198 + 0x12FC + 0xC == 0xE2A4A0, which is why four different
  instruction sequences reach the same array; 0xE2A7A0 (TIME_$RTEQ) fixes
  the extent at 64.  TIME_$CPU_LIMIT_DB (0xE29198) and the two halves of
  TIME_$ITIMER_DB (0xE297F0 real, 0xE29E48 virtual) are each **58** records
  of 0x1C bytes — matching PROC2_UID_TABLE_SIZE.
- The three timer callbacks are reached through TIME_$Q_SCAN_QUEUE's
  **deferred** path (0x00E16F4C), which passes the ADDRESS OF A LOCAL holding
  `&elem->callback_arg`, so their single argument is a `uint32_t **`.
- Their signal/status arguments are **Pascal by-reference constant cells**
  behind each rts: 0xE58A92/0xE58A94 = {0x000E, 0x000D0007},
  0xE58AF2/0xE58AF4 = {0x001D, 0x000D0008}, 0xE58B52/0xE58B54 =
  {0x001B, 0x000D000B}.  The signal numbers 0x1D and 0x1B do NOT match
  proc2/proc2.h's assumed BSD table (26 and 24) — reproduce them literally.
- `ori #0x700,SR` … `andi #-0x701,SR` in TIME_$SET_TIME_OF_DAY and
  TIME_$ADJUST_TIME_OF_DAY are SET_IPL7()/SET_IPL0(), not save/restore.
- Both of those write a 32-bit tick count into a 48-bit record with
  `clr.w (rec)` + `move.l Dn,(rec+2)` — **straddling the high/low split**, so
  a count above 0xFFFF reaches `high`.  Writing only `low` loses it.
- TIME_$ADJUST_TIME_OF_DAY writes `old_delta` **unconditionally** and never
  nil-checks it, and rewrites the RTC even when the delta is zero.

Related: [[msg-pkt-notes]], [[feedback-fidelity-gates]].
