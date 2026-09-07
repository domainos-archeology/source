---
name: rip-server-reemission
description: RIP_$SERVER's 0x538 frame record, the nested RIP_$PROCESS_REQUEST, the RIP packet wire format and the function's two original defects
metadata:
  type: project
---

Recovered while closing bead source-4nvz (rip/server.c re-emitted block for
block against 0x00E68864-0x00E68E24).

- **The RIP wire packet is `{command:word, entries[{network:4, metric:2}]}`**,
  capped at 90 entries = 0x21E bytes.  `RIP_$PACKET_LENGTH(n) == 6n + 2`,
  computed entirely in 16 bits, and RIP_$SERVER rejects any payload whose
  length disagrees.  Both the request (through payload_va) and the reply
  (A6-0x290) use this shape, and so does the response arm's update loop
  (`(-0x4d4,A2)` / `(-0x4d0,A2)` with A2 = A6 + 6 + 6i).

- **`RIP_$PROCESS_REQUEST` (0x00E688C8) is a nested procedure**: one boolean
  at (0x8,A6), everything else via `move.l (A6),D6`.  Parent fields it
  touches: entry_count A6-0x514, response_count A6-0x512, payload_va A6-0x4F0,
  response A6-0x290.  Emitted as a static taking `rip_$server_frame_t *`.
  The routing table is walked with stride 0x2C from 0xE263BC, 64 slots, state
  = `(route->flags & 0xC0) >> 6` tested with `btst.l D1,#6` (VALID or AGING).
  The STD side reads routes[1] (+0x27 metric) and clamps the metric UP to 0x10
  with an **unsigned longword** compare; the internet side reads routes[0]
  (+0x13) and answers 0x11 for an unknown network.

- **`is_std` is sock_$pkt_info_t.flags bit 1** (SOCK_PKT_FLAG_XNS).  Negative
  selects the "STD" globals - ROUTE_$STD_N_ROUTING_PORTS, RIP_$STD_RECENT_CHANGES,
  RIP_$BROADCAST(true) - **and routes[1]**.  rip_internal.h's comment calling
  routes[0] "standard" is the wrong way round; rip.h's `is_std` parameter name
  is right.

- **The reply address is header+0x12** (the IDP source `{network, host,
  socket}`), passed as RIP_$SEND's first argument with `pea (-0xe,A6)`.  The
  request arm's broadcast check reads the IDP **destination** host at
  header+0x0A/0x0C/0x0E, and the response arm reloads the network from
  header+0x06 (dest_network) on the XNS path, from PKT_$BRK_INTERNET_HDR's
  arg 3 on the other.

- **Two original defects, preserved (bead source-u9wy).**  (1) A6-0x4F0
  (payload_va) is written only at 0x00E68A5A, inside the XNS arm, yet
  RIP_$PROCESS_REQUEST loads it on both paths - an internet-borne request with
  entry_count > 0 reads through an uninitialised frame slot.  (2) The
  name-register arm's node-id mask at 0x00E68DE0 applies the +6 accessor of
  the 10-byte `{network, host_hi, host_lo}` record to a pointer that is
  already four bytes into it, so it masks src_socket plus two frame bytes the
  header copy never writes.

- **32-bit VA fields need `ARCH_PTR_TO_VA` / `ARCH_VA_TO_PTR`** (arch/arch.h).
  A frame slot declared `uint32_t` truncates a 64-bit host pointer and the
  host test segfaults.  The test sets `ARCH_HOST_VA_BASE` just below a single
  static arena and carves every object the code dereferences through a VA out
  of it.

Related: [[route-rip-sock-layouts]], [[xns-rip-send-notes]], [[feedback-fidelity-gates]].
