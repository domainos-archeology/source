---
name: ring-stats-counter-names
description: The receive half of ring_$stats_t named from /etc/netmain, pinned by two logged status codes and by the RING_$SWDIAG_DATA mirror sitting a uniform 0x1A lower
metadata:
  type: project
---

Closes bead source-1a5o.  `RING_$GET_STATS` copies the 0x3C block out
verbatim, so the kernel never names it; `strings -a` on
`sr10.4-install/install/ri.apollo.os.v.10.4/etc/netmain` does - its
"Error counts for <node>" display formats the ASKNODE ring statistics record
field by field and names exactly eleven receive fields, one long and ten
words, which is exactly what 0x1C..0x33 holds.

Three are pinned directly by the image:

- **+0x1C `rcvcnt`** - the only long in the receive half, `addq.l #1,(0x1c,A3)`
  once per accepted packet (0x00E75EEC).
- **+0x26 `rcvbus`** - the rcv_csr bit-6 arm logs status **0x00110013,
  "receive bus error"** (cell 0x00E76044) and CRASH_SYSTEMs before bumping it.
- **+0x32 `rcvhcsum`** - the header-checksum arm logs **0x00110010,
  "bad checksum"** (cell 0x00E76040).

Taking netmain's fields in row order (rcvcnt, rcveor, rcvcrc, rcvtim),
(rcvbus, rcvmodem, rcvpkt, rcvovr, rcvapar), (rcvxerr, rcvhcsum) lands both
anchors on their proven offsets, so:

| off | name | rcv_csr bit | swdiag mirror |
|-----|------|-------------|---------------|
| 0x1C | rcvcnt (long) | - accepted packets | - |
| 0x20 | rcveor   | 5 | +0x06 |
| 0x22 | rcvcrc   | 8 | +0x08 |
| 0x24 | rcvtim   | 9 | +0x0A |
| 0x26 | rcvbus   | 6 (crashes) | +0x0C, never written |
| 0x28 | rcvmodem | 3 | +0x0E |
| 0x2A | rcvpkt   | 10 or 11; the split goes to RING_$RCV_ESB (bit 10, 0xE261BA) / RING_$RCV_BIPHASE (bit 11, 0xE261B8) | +0x10 |
| 0x2C | rcvovr   | none - DMA overrun | +0x12, never written |
| 0x2E | rcvapar  | 0 | +0x14 |
| 0x30 | rcvxerr  | 7 | +0x16 |
| 0x32 | rcvhcsum | header checksum | - |

**Every swdiag mirror is its stats counter minus 0x1A**, so
RING_$SWDIAG_DATA+0x06 is the same ten-word block; the two mirrors the receive
path never writes (+0x0C, +0x12) are exactly the two counters the CSR decode
never bumps.  That uniformity is the strongest confirmation of the ordering.

The **transmit** half at 0x00..0x1B is still guessed - netmain names it
xmit_call / xmitcnt (the two longs at +0x02/+0x06) plus nine words (xmit_nack,
xmit_wack, xmit_orun, xmit_tim, xmit_apar, xmit_bus, xmit_nortn, xmit_modem,
xmit_error) but nothing in the image pins their order yet.  Bead source-11rf;
look for 0x00110014 "transmit bus error" and 0x00110016 "memory parity error
during transmit" next to a counter bump.

Related: [[ring-xns-subsystems]], [[reference-sr104-userspace-binaries]].
