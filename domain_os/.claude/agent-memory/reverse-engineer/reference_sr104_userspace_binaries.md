---
name: reference-sr104-userspace-binaries
description: /etc/netmain and friends in the SR10.4 install tree name kernel statistics fields the kernel itself never names - the way to recover counter names
metadata:
  type: reference
---

When a kernel statistics block is copied out verbatim (RING_$GET_STATS,
ASKNODE replies, DISK/WIN stats), the kernel never names its fields.  The
user-space consumer does, and the SR10.4 distribution is unpacked in this
checkout:

  ~/src/domainos-archeology/sr10.4-install/install/ri.apollo.os.v.10.4/etc/

`strings -a` on **netmain** yields the full "Error counts for <node>" display,
which formats the ASKNODE ring statistics record field by field and gave every
name in `ring_$stats_t`'s receive half (bead source-1a5o).  Also there:
netmain_srvr, netmain_chklog, lcnet, netmain_note.  netmain additionally
carries the Winchester and network-service stat displays and a set of
`*_RDHELP` help topics (RCVBPH_RDHELP, RCVESB_RDHELP, XMIT_BPH_RDHELP,
XMIT_ESB_RDHELP, DISK_CRC_RDHELP...) whose prose explains what each counter
means.

`~/src/domainos-archeology/sr10.4-install/install/ri.apollo.os.v.10.4/sys/ins/`
has the public `.ins.pas` / `.ins.ftn` inserts, but nothing for ring/network
internals.  `~/src/domainos-archeology/sau2-headers/` only has asknode.h and
netman.h and neither carries counter names.

**How to apply:** before declaring a field name unrecoverable, grep the strings
of the matching /etc tool.  Pair the recovered name list with anchors from the
image - a counter bumped next to a `pea (status,PC)` whose code the status
database names - to pin the order rather than guessing it.

Related: [[status-code-database]], [[ring-xns-subsystems]].
