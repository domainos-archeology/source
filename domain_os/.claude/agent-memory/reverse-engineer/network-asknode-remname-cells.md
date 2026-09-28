---
name: network-asknode-remname-cells
description: NETWORK_$GET_NET's high-word index, the ASKNODE 0xE645BE/0xE645C0 constant pool, and REM_NAME_$REGISTER_SERVER's two ignored arguments
metadata:
  type: project
---

Recovered closing beads source-9orn, source-6f9k, source-dgfb and source-xprl
(2026-09-08).

## NETWORK_$GET_NET (0x00E0F2CC): the index is in the HIGH word

`move.w #0x3f0,D0w` (0x00E0F2DC) then `and.w (0x8,A6),D0w` (0x00E0F2E4).
A6+0x08 is the **high half** of the longword first argument - the only caller,
ast_$force_activate_segment, pushes a full longword at 0x00E021FA and masks the
low 20 bits of the same value at 0x00E021E6.  So

    index = ((net_addr >> 16) & 0x3F0) >> 4      /* bits 20..25 */

The table entry stride is 8 (`lsl.w #0x3`) and the net_id sits at A5+0x3C with
A5 = 0xE248FC, i.e. entry+4 with entry 0 at A5+0x38.  Index 0 is the "no
network" arm (returns 0 / status_$ok); an empty slot returns 0 with
status_$network_unknown_network = **0x00110017** (0x00E0F304).

A `move.w (d,A6)` on an argument slot is always worth this check: on a
longword argument it reads the high half, on a word argument it reads the
whole thing, and only the caller settles which.

## ASKNODE constant pool 0x00E645BE..0x00E645C3

Two cells wedged between ASKNODE_$INFO's `rts` (0x00E645BC) and
ASKNODE_$GET_INFO's `link.w` (0x00E645C4).  `gsk read 0xE645B0 32`:

    00e645b0  2f 2e 00 0c 2f 2e 00 08  61 30 4e 5e 4e 75 00 98
    00e645c0  ff ff ff ff 4e 56 00 00  2f 2e 00 1c 2f 2e 00 18

  * 0xE645BE = **0x0098** - ASKNODE_$INTERNET_INFO's `resp_len`, the reply-data
    ceiling handed to PKT_$SAR_INTERNET.  Only ASKNODE_$INFO uses it
    (`pea (0x18,PC)` at 0x00E645A4); GET_INFO takes it from its caller.
  * 0xE645C0 = **0xFFFFFFFF** - `req_len`, and the -1 is load bearing:
    INTERNET_INFO's request-0x1F arm tests `*req_len != -1` twice to decide
    whether to re-derive a route with DIR_$FIND_NET and retry.  Both wrappers
    point at this one cell (`pea (0x12,PC)` at 0x00E645AC, `pea (-0x1a,PC)` at
    0x00E645D8).

Labelled in Ghidra as ASKNODE_$DEFAULT_RESP_LEN / ASKNODE_$DEFAULT_REQ_LEN.
The SAU2 map exports no symbol for either.  Both are read-only, so each C file
carries its own `static const` copy under the same name - which means the two
wrappers cannot be `#include`d into one test program.

## REM_NAME_$REGISTER_SERVER (0x00E4A4AE) takes two ignored pointers

The whole body is `lea (0xe7dbb8).l,A0 / move.l (0x00e2b0d4).l,(0x28,A0) /
st (0x3c,A0)` - neither argument is read - but all three call sites push two
longwords, so the prototype carries them.  They are a (network, node) pair:

  * 0x00E69152/0x00E69158 RIP_$ANNOUNCE_NS -> (&ROUTE_$PORT, &NODE_$ME).
    Note 0xE2E0A0 is `ROUTE_$PORT` in the SAU2 map (aliased NETWORK_$ME and
    DCTE_$NULL there), not just ROUTE_$PORT_ARRAY.
  * 0x00E68DF2/0x00E68DF6 RIP_$PROCESS_REQUEST ring path -> (&reg_network,
    &reg_node_id).
  * 0x00E68E04/0x00E68E08 its internet path -> (&src_node_or, &src_node).

Reminder that fixes the reading of every such pair: **the LAST push is
argument 1** (verified against REM_FILE_$SEND_REQUEST, whose A6+0x08
`addr_info` is what the caller pushes last).  A bead that lists operands in
push order lists them backwards relative to the C signature.
