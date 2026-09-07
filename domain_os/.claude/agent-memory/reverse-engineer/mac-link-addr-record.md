---
name: mac-link-addr-record
description: The MAC packet descriptor is one record for send and receive, and its head is a 24-byte variable-length link address (word count + up to 11 words)
metadata:
  type: project
---

`mac_os_$send_pkt_t` and `mac_os_$rcv_pkt_t` are the **same Pascal record**
(link address 0x00..0x17, boolean 0x18, buffer chain 0x1C, frame type 0x30,
payload length 0x38, payload pages 0x3C).  RING_$SEND_OS's loopback path
proves it by copying a caller's send record whole into the record it hands
MAC_OS_$DEMUX (0x00E77E76, `moveq #0x12` + `move.l (A1)+,(A4)+` = 0x4C bytes).

Bytes 0x00..0x17 are **one** `mac_os_$link_addr_t` = `{uint16 n_words;
uint16 addr[11];}` (source-txfx, settled 2026-09-07).

**Why:** two count-driven copy loops give the shape directly - MAC_$DEMUX
0x00E0BC82..0x00E0BC9A and MAC_$RECEIVE 0x00E0BE52..0x00E0BE6A: copy the word
at +0x00, then that many words from +0x02.  The 24-byte extent comes from the
boolean at +0x18 (0x00E0BC68 / 0x00E0BE4E) plus MAC_$SEND's single
6-longword record assignment at 0x00E0BBC2, and independently from
MAC_$DEMUX's staging record, where the count is at A6-0x2E and the next field
written is at A6-0x16 (0x00E0BCBC) - 0x16 bytes = 11 words.

**How to apply:** a 6-longword `move.l (A0)+,(A1)+` block copy at the head of
a record is a whole-record assignment, not a run of separate fields; look for
a count-driven loop elsewhere in the subsystem before calling the bytes
unknown.  Counts this image stores are only 2 (Apollo ring node id, ARP
net_type 0/3 and ring_$receive_packet 0x00E76528) and 3 (6-byte IEEE 802
address, ARP net_type 4/5); RING_$SEND_OS accepts only 2 (0x00E77D7E ->
0x00310012).  No copy loop is bounded by 11 - that overrun is original.

**Real negatives worth remembering:** there is no ethernet, token-ring or
FDDI driver in the SAU2 image (`ETHERNET_$INIT` 0x00E78004 is a stub
returning `status_$io_controller_not_in_system`; `gsk search` finds no
802/TOKEN/FDDI namespace), and the SR10.4 user-space `/sys/ins` tree ships no
`mac.ins.pas`, so MAC_$ is not a user-visible interface there.  See
[[reference_sr104_userspace_binaries]] for when that tree *does* help.

Keeping `net_type`/`src_id`/`_r06` as an anonymous-union arm on
`mac_os_$rcv_pkt_t` let the record be retyped without touching
`ring/receive_packet.c`, which another agent owned - see
[[feedback_shared_worktree]].
