---
name: ring-info-record
description: The 122-byte ring_info_t (ASKNODE request 0x1F) recovered from its responder, and the NETWORK_ dispatcher's reply-record shape at frame-0x1E8
metadata:
  type: project
---

Bead source-4omb.  `NETWORK_$RING_INFO` (0x00E1039A) only *copies* the record,
so the layout comes from the **responder**: `NETWORK_$PROCESS_PAGING_REQUEST`
(FUN_00e10628, renamed from sr10.4-install/sau7/domain_os.map) case 0x0E at
0x00E11246..0x00E112D4.

**The NETWORK_ dispatcher's reply record lives at parent_frame-0x1E8:**
+0x00 reply type word (request+1), +0x02 status longword, +0x06 payload.  So a
store at `(-0x1E2+N,A0)` is payload offset N.  The payload's first word is
*per-command*, not a shared header field (8 for several other cases, 3 for
0x0E) - do not call it a version.

ring_info_t = 0x7A bytes: `_unknown_00` word (const 3), `diskless`
(NETWORK_$DISKLESS), unwritten pad byte, `mother_node`, **RING_$STATS[0]**
(0xE261E0, 0x3C) at +0x08, **NETWORK_$FAILURE_REC** (0xE24BF4, 0x10) at +0x44,
**RING_$SWDIAG_DATA** (0xE261C2, 0x1E) at +0x54, then xmit_biphase /
rcv_biphase / xmit_esb / rcv_esb at +0x72..+0x79.

Two things this pass settled that are reusable:

- **`ring_$swdiag_t` is 0x1E bytes, not the 0x18 ring/ring.h declares** - the
  copy runs 0xE261C2..0xE261DF, i.e. right up to RING_$STATS[0].  +0x18 is
  `rcvhcsum` (the uniform -0x1A mirror of stats+0x32).  Bead source-twut.
- **`network_$failure_rec_t` +0x04 and +0x0C are named backwards** in
  network/network.h: 0x00E10414 stores NODE_$ME into +0x04 and 0x00E1042E the
  failure type into +0x0C.  Bead source-oowv.

**network.h cannot include ring/ring.h** - ring.h includes network.h for the
status_$network_* codes.  When a network record embeds a ring record, spell the
fields out in a nested anonymous packed struct and cite ring/ring.h as the
authority; do not create a second top-level type name.

**A5 in the NETWORK_ module is 0x00E248FC** (`lea (0xe248fc).l,A5`,
0x00E10402).  A5+0x2F8 = NETWORK_$FAILURE_REC, +0x310 = MOTHER_NODE,
+0x33C = INFO_RQST_CNT, +0x342 = ALLOWED_SERVICE, +0x350 = DISKLESS.

netmain's "Error counts for <node>" display shows **25** counters against
ring_$stats_t's 22; the extra three plus the "rcv esb" line are exactly this
record's four tail words, in netmain's own print order.

Related: [[ring-stats-counter-names]], [[reference-sr104-domain-os-map]],
[[reference-sr104-userspace-binaries]], [[module-base-inherited-a5]].
