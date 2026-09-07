---
name: mmu-io-registers
description: SAU2 MMU page 0xFFB400 register names and bit layouts, from the Domain Engineering Handbook DN3xx chapter
metadata:
  type: project
---

Apollo's own names for the MMU page on SAU2 (DN300/320/330), Domain
Engineering Handbook 002398-04 Rev4, pp. 7-23..7-27. Recorded in
`domain_os/mmu/mmu.h`'s register table.

- `0xFFB400` PID/PRIV/POWER (word). bit0 enable MMU, bit1 enable PTT access,
  D/P domain bits, F = "FP trap occurred" (DN330 only, R/O, cleared by a
  write to the FPU owner register).
- `0xFFB402` **FPU Owner Register** (write-only, ASID of the FPU owner).
  mmu.h historically calls this `MMU_POWER_REG`; bus_err.s calls it
  `FP_HW_OWNER`, which is the accurate one.
- `0xFFB403` MMU Status Register (DN330). bit0 `0 -> Stingray 020 board`
  (so **1 means DN300/320**, the 68010 "type 1" MMU), bit1 PTT access
  enabled, bit2 orderly shutdown, bit3 MMU error (timeout or parity, valid
  only while bit5 set), bit4 normal mode, bit5 bus/MMU timeout or MMU parity
  error, bit6 page fault, bit7 access violation. *Any write clears bits 5-7.*
- `0xFFB404..0xFFB407` Memory Control/Status (DN330, byte writeable):
  failing A(21:2), byte parity error flags (0 => error), B-port/DMA access,
  parity interrupt enable, write bad parity.
- `0xFFB405` / `0xFFB406` are the DN300/320 memory control and memory status
  registers instead (different machine, same page).
- `0xFFB408..0xFFB409` LEDS / hardware-revision register.
- `0xFFB40A` **MMU Parity Register** (word): bit15 write wrong MMU parity,
  bit14 MMU parity fault enable (PFE), bit13 PTT parity error (CLR),
  bit12 PFT parity error (CLR), bits11..0 PFTX = failing PFT index.
  "Bus error occurs on parity operation if MMU PFE is set, and bits 12 and
  13 were clear."
- `0xFFB40C..0xFFB40F` diagnostic loopback of SBUS signals (TBD in the doc).

**The big-endian trap that made this hard.** `FIM_$BUS_ERR` btsts bits 5 and
4 of the *byte* at 0xFFB40A; those are word bits 13 and 12, i.e. PTT and PFT
parity. Its `move.w #0x4000` is not a magic ack value - it is PFE set, WWP
clear, and 0 written into the two CLR bits. Cross-checked against the SR10.4
status texts for 0x00070004/5/6 ("ptt parity error", "pft parity error",
"mmu timeout"), which independently order the two bits. Bead source-nx70.
