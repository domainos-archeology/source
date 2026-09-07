---
name: reference-engineering-handbook-io-regs
description: The Domain Engineering Handbook PDFs name every SAU I/O register bit-by-bit - check it before declaring a 0xFFxxxx register unnamed
metadata:
  type: reference
---

`~/src/domainos-archeology/docs/apollo-docs/002398-04_Domain_Engineering_Handbook_Rev4_Jan87.pdf`
(and the Rev1 Apr83 edition beside it) has a per-machine chapter that gives
**every** I/O register: address in both the `[8xxx | 0xFFBxxx]` short/long
forms, an ASCII bit diagram, and a legend. The kernel's own maps do not -
`sau2-maps/domain_os.10.*.map` names only the 1KB page bases (`FFB400 MMU`,
`FFB800 PFT`) from the IODEFS section, never individual registers.

Machine -> chapter is via `~/src/domainos-archeology/docs/saus.md`:
SAU2 = DN300/DN320/DN330, chapter 7 ("DN3xx", pp. 7-18..7-28).

Extract the text with the stdlib recipe in [[reference-apollo-pdf-docs]].
The OCR runs registers together, so search for a distinctive bit label
(e.g. "PTT  Parity  Error") and print a wide window backwards - the register
*name and address* sit a paragraph *before* the bit legend, and Apollo lists
bit legends bottom-up (lowest bit first).

Reading the diagrams: addresses are byte addresses of a big-endian word, so
a `btst #5` on the byte at an odd/even address is testing word bit 13 - see
[[mmu-io-registers]] for the worked case at 0xFFB40A.
