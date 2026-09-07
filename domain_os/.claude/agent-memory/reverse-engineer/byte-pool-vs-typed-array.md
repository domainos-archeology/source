---
name: byte-pool-vs-typed-array
description: When every caller computes an explicit byte displacement into a table, declare the table as bytes - a typed array silently rescales the arithmetic.
metadata:
  type: feedback
---

If the machine code reaches a table with an explicit stride constant
(`pbu_index * 0x18`, `pid * 0x0C`, `ws_index * 0x24`), declare the object as
`uint8_t[]` (or index a typed array by element and drop the constant) - never
as a typed array that the C code then also multiplies by the stride.

**Why:** the tree had `extern ec2_$eventcount_t EC2_PBU_ECS_BASE[]` (8 bytes
per element) with callers writing `EC2_PBU_ECS_BASE + pbu_index * 0x18`, which
scales by 8*0x18.  Same bug in `EC1_ARRAY_BASE + pid * 0x0C` and in the mmap
`MMAP_$WS_LIMIT_DATA`/`MMAP_$WS_DATA` views.  Nothing warns; the link succeeds.

**How to apply:** while defining a cell, check every user's index expression
against the disassembly's scaling before choosing the element type.  Fixing the
declaration is the archivist-correct move - it restores the image's behaviour
rather than changing it.
