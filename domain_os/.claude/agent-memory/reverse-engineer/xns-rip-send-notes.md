---
name: xns-rip-send-notes
description: Recovered XNS send-path ABIs - the 0x48-byte xns_$os_send_rec_t head, the channel flags word's byte-btst bits, MAC_OS_$SEND's port-table argument, PKT_$BRK_INTERNET_HDR's fourteenth argument, and RIP_$SERVER's frame map
metadata:
  type: project
---

Verified against the disassembly while closing source-tvrs, source-2ptk and
source-0fks.

- **`xns_$os_send_rec_t` (0x48 bytes) is a `mac_os_$send_pkt_t` shifted down by
  four from +0x18 on, with a 24-byte address head.** +0x00 is a destination
  `xns_$net_addr_t` and +0x0C a source one: XNS_IDP_$OS_SEND copies exactly
  those 24 bytes into the IDP header at **+0x06** (`moveq #0x17,D1 /
  move.b (A3)+,(A4)+ / dbf` at 0x00E18336), and the connected alternative at
  0x00E18318-0x00E18332 fills the *same* header bytes from chan+0xA4 and
  chan+0xB0. +0x18 is a `mac_os_$buf_desc_t`, +0x24 hdr_prebuilt, the low byte
  of the +0x2C word is the IDP packet type, +0x34 data_length (its **low
  word** is added to the IDP length by `add.w (0x36,A3),D1w`), +0x38 four
  pages.

- **`xns_$channel_t.flags` (chan +0x3A) is read with BYTE btst on the HIGH
  half**: `btst.b #5,(0xda,A2)` is word bit **13** (CONNECT) and
  `btst.b #3` is word bit **11** (build the IDP header). XNS_IDP_$OS_OPEN puts
  open-option bit *n* into word bit *n+11* (`move.b (0x3,A1),D1b / lsl.b #0x3`
  at 0x00E18180), which is why they never collide with the AS_ID in bits 5..10
  (written with a WORD `andi.w #-0x7e1 / lsl.w #0x5`).

- **MAC_OS_$SEND's first argument is the XNS state's own port table**, not a
  ROUTE port: `pea (0x48,A5,D1)` with D1 = port*12 (0x00E18474) is
  `&state->ports[port].mac_socket` (ports at +0x40, stride 0x0C, socket at
  +0x08).  ROUTE_$PROCESS does the same at 0x00E876FA.

- **XNS_IDP_$OS_SEND's third argument is a length, not a checksum** (cleared
  with `clr.w` at 0x00E18268, handed straight to MAC_OS_$SEND's `bytes_sent`
  at 0x00E18460), and the checksum is computed only when the header does NOT
  already hold 0xFFFF (`cmpi.w #-1,(A3) / beq` at 0x00E1842E).

- **`xns_$idp_header_t` and `xns_$net_addr_t` need `__attribute__((packed))`.**
  m68k aligns 32-bit scalars on 2-byte boundaries, so the unpacked
  declarations happened to be right for the target while putting
  `dest_network` at +0x08 on a 64-bit host - which silently broke any host
  test.  The same trap applies to every wire record in this tree.

- **`sock_$pkt_info_t.flags` bit 1 = XNS ("standard") routing.**  Both readers
  spell it as a byte btst on the low half: ROUTE_$PROCESS 0x00E874EA and
  RIP_$SERVER 0x00E68A28.  When set, the frame's IDP header is already in
  front of the caller and no PKT_$BRK_INTERNET_HDR call is needed.

- **PKT_$BRK_INTERNET_HDR (0x00E12328) takes FOURTEEN arguments**, offsets
  0x08, 0x0C, 0x0E, 0x12, 0x16, 0x1A, 0x1E, 0x22, 0x26, 0x2A, 0x2E, 0x32,
  0x34, 0x38 (its one caller pops 0x34).  Argument 2 at (0xC,A6) is **never
  read** - RIP_$SERVER pushes `sock_$pkt_info_t.hdr_len` there anyway
  (0x00E68ABE).  Argument 13 is the length in/out and argument 14 the status.

- **PKT_$SAR_INTERNET's tenth argument (resp_buf) is written, not read**:
  `movea.l (0x24,A6),A0 / move.w D3w,(0x8,A0)` at 0x00E7205E stores the
  attempt count before status 0x00110007.  Both callers pass a local record
  (ASKNODE_$INTERNET_INFO A6-0xD8, REM_NAME A6-0x28); passing NULL faults.

- **RIP_$SERVER frame (link -0x538)**: A6-0x70 `sock_$pkt_info_t`, A6-0x20 the
  30-byte IDP header copy, A6-0x4D0 the 0x21E payload buffer, A6-0x2B0 the
  packet-info record, A6-0x4FC/-0x500/-0x51C/-0x4F4/-0x4F8/-0x51A the
  BRK address outputs, A6-0x518 id, A6-0x516 length, A6-0x4EC status,
  A6-0x514 the entry count, A6-0x508 the header VA for NETBUF_$RTN_HDR.
  The whole frame is now the record `rip_$server_frame_t` and all three
  dispatch arms are translated - see [[rip-server-reemission]].

- **`stcode.js <db> <code>` does not take a code argument** - it only dumps the
  whole database.  Grep its output for `(3b0008)` instead.  Module 0x3B is
  "OS / XNS IDP"; the eight xns.h identifiers that disagreed with it were
  renamed to the database text (source-v1lr, values unchanged): 0x3B0002
  no_os_sockets, 0x0008 illegal_buffer_spec, 0x000B
  listen_network_not_connected, 0x000D idp_socket_table_full, 0x0010
  no_client_for_packet, 0x0013 network_unreachable, 0x001A
  connect_to_broadcast, 0x001B source_must_be_this_node.

Related: [[netbuf-notes]], [[msg-pkt-notes]], [[feedback-fidelity-gates]].
