---
name: rem-file-opcodes
description: The REM_FILE request opcode table re-derived from the image - request+0x02 is the constant 0x80, request+0x03 the opcode, and every value
metadata:
  type: project
---

Re-derived for bead source-8joj.  Every REM_FILE_$* builder writes the same
two bytes into its request record - `move.b #-0x80,(rec+0x02)` and
`move.b #op,(rec+0x03)` - and REM_FILE_$SERVER reads the opcode back at
request+0x03 (`move.b (-0x435,A6),D0b`, request based at A6-0x438).  The reply
carries **opcode+1** at response+0x03 (`addq.b #1,D0b` at 0x00E637EE), so
reply codes are the odd successors.

| op | builder | server |
|----|---------|--------|
| 0x00 | TEST (clr.b) | 0x00E63E40 |
| 0x04 | SET_ATTRIBUTE | __set_attribute |
| 0x08 | TRUNCATE | __truncate_delete |
| 0x0A | LOCK (plain) | 0x00E63B08 |
| 0x0C | UNLOCK | 0x00E63C8C |
| 0x10 | NEIGHBORS | 0x00E63ADC |
| 0x12 | UNLOCK_ALL | 0x00E63D18 (also the diskless-crash message) |
| 0x14 | PURIFY | 0x00E63E16 |
| 0x16 | LOCAL_READ_LOCK | 0x00E63DDA |
| 0x18 | SET_DEF_ACL | 0x00E63E48 |
| 0x1A | LOCAL_VERIFY | 0x00E63E02 |
| 0x1C | NAME_GET_ENTRYU | 0x00E63DFA |
| 0x1E | GET_SEG_MAP | 0x00E63EA4 |
| 0x20 | INVALIDATE | 0x00E63F30 |
| 0x22 | NAME_ADD_HARD_LINKU | 0x00E63F6E |
| 0x24 | GENERATE_UID | __generate_uid |
| 0x26 | CREATE_TYPE_PRESR10 | 0x00E63FD6 |
| 0x28 | DROP_HARD_LINKU | 0x00E63FC6 |
| 0x64 | ACL_IMAGE | ACL_$SERVER |
| 0x66 | SET_ACL | ACL_$SERVER |
| 0x68 | ACL_CREATE | ACL_$SERVER |
| 0x6A | ACL_SETIDS | ACL_$SERVER |
| 0x6C | ACL_CHECK_RIGHTS | ACL_$SERVER |
| 0x7C | RESERVE | 0x00E63F50 |
| 0x7E | CREATE_TYPE | 0x00E64020 |
| 0x80 | FILE_SET_PROT | __set_prot_attrib |
| 0x82 | FILE_SET_ATTRIB | __set_prot_attrib |
| 0x84 | LOCK_EXT | 0x00E63B08 (shares 0x0A's handler) |
| 0x86 | CREATE_AREA | 0x00E640A8 |
| 0x88 | DELETE_AREA | 0x00E640EC |
| 0x8A | GROW_AREA | 0x00E64106 |

Every code is even.  **0x2A..0x5C go to DIR_$SERVER and 0x64..0x77 to
ACL_$SERVER** before the compare chain is reached, so REM_FILE has no builders
for the DIR codes (0x3C/0x3E/0x42/0x58 are the four the server special-cases
for their netbuf).

**Three builders send two requests**: CREATE_TYPE (0x24 then 0x7E),
CREATE_TYPE_PRESR10 (0x24 then 0x26) and ACL_CREATE (0x24 then 0x68) - 0x24
is the shared "ask the remote node for a UID" step.  REM_FILE_$LOCK picks
0x84 or 0x0A from its boolean at (0x14,A6); they are two different requests,
not a fallback.

The table lives once, in rem_file/rem_file_internal.h as `REM_FILE_OP_*`;
rem_file/server.c keeps only the four `SERVER_OP_DIR_*` codes.

Related: [[rem-file-subsystem]], [[feedback-shared-records]].
