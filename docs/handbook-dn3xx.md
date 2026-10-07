# Domain Engineering Handbook Rev 4 (Jan 1987), chapter 7: DN300, DN320, DN330

Transcribed from the scanned PDF
`~/src/domainos-archeology/docs/apollo-docs/002398-04_Domain_Engineering_Handbook_Rev4_Jan87.pdf`,
PDF pages 139 to 177 (printed pages 7-1 to 7-39), by Claude Fable 5.1
reading the page images, 2026-10-06.  Hex values are as printed; where a
scan glyph was ambiguous the reading is marked `(?)`.  The tesseract
sidecar `docs/apollo-docs/ocr/002398-04_...txt` is the searchable copy;
this file is the checked one for the register tables.

## 7-1  Address space

### DN300 / DN320 (24-bit, SAU2 68010)

| physical | what              | virtual                          |
|----------|-------------------|----------------------------------|
| 100400   | traps             | 0                                |
| 400      | PROM              | 400 to 7FFF (one-to-one)         |
| 100800   | phys mem          | 100800 to 7FFFFF                 |
| 700000   | ptt               | 700000 to 7FFFFF                 |
| 100000   | MD STK, DATA      | E00000                           |
| 20000    | displ_mem         | FC0000 to FDFFFF                 |
| B000     | FPU ctl           | FF7000                           |
| B400     | FPU cmd           | FF7400                           |
| B800     | FPU cs            | FF7800                           |
| 9400     | disp 1            | FF9800                           |
| 9800     | ring 2            | FF9C00                           |
| 9000     | DMA ctl           | FFA000                           |
| 9C00     | FLP, WIN, CAL     | FFA800                           |
| 8800     | timers            | FFAC00                           |
| 8400     | sios              | FFB000                           |
| 8000     | mmu               | FFB400                           |
| 4000     | pft               | FFB800 to FFF7FF                 |

### DN330 (26-bit, 68020)

| physical | what                    | virtual                         |
|----------|-------------------------|---------------------------------|
| 400      | prom                    | 400 to 3FFF (one-to-one)        |
| 4000     | pft                     | 3FFB800 to 3FFF7FF              |
| 8000     | mmu                     | 3FFB400                         |
| 8400     | sios                    | 3FFB000                         |
| 8800     | timers                  | 3FFAC00                         |
| 9800     | ring                    | 3FF9C00                         |
| 9C00     | disk, tape, cal         | 3FFA800                         |
| A400     | pbu ctl                 | 3FF7C00 (DSP90/DN560)           |
| A800     | lpr                     | 3FF8000 (DSP90)                 |
| BC00     | VME control             | 3FF9400 (DN560)                 |
| E000     | color_sup               | 3FF6000 (DN560)                 |
| E400     | color_user              | 3FF6400 (DN560)                 |
| E800     | color_wcs               | 3FF6800 (DN560)                 |
| F000     | displ_sup               | 3FF9800                         |
| F400     | displ_user              | 3FFA000                         |
| F800     | displ_wcs               | 3FFA400                         |
| 10000    | iomap                   |                                 |
| 3FF5000 to 3FF5FFF (DSP90/DN560) |      |                                 |
| 14000    | prom2                   |                                 |
| 20000    | displ_mem               | 3FC0000 to 3FDFFFF (DN560)      |
| 40000    | color_mem               |                                 |
| 3FA0000 to 3FBFFFF (DN560) |            |                                 |
| 70000    | pbu i/o ref             |                                 |
| 3FE0000 to 3FEFFFF (DSP90/DN560) |      |                                 |
| 80000    | pbu 1st half            |                                 |
| 100000   | mem: md data            | 3D00000                         |
| 100400   | mem: traps              | 0                               |
| 100800   | mem                     | 100800                          |
| 380000   | pbu 2nd half            | (DSP90/DN560)                   |
| 400000   | ptt                     | 400000 to 7FFFFF                |

So on the DN330 the kernel's "0xE00000" world lives at virtual 0x3D00000
(0x100000 + 0x2F00000, the window COLD's fixup table applies), the I/O
page at 0x3FFxxxx, and the page translation table at 0x400000 (the
0x700000 to 0x400000 fixup).  On the DN300/DN320 the same memory is at
0xE00000 and the I/O page at 0xFFxxxx.

## 7-2  Cache registers (DN330 only)

### CACR, cache control register (MOVEC to/from 002)

Bits 31..8 zero; bits 7..0:

| bit | name | meaning                                              |
|-----|------|------------------------------------------------------|
| 3   | CC   | clear entire cache (write only)                      |
| 2   | CE   | clear entry addressed by CAAR (write only)           |
| 1   | FC   | freeze cache (enabled, but no replacing data) (R/W)  |
| 0   | EC   | enable cache (R/W)                                   |

### CAAR, cache address register (MOVEC to/from 802)

Bits 31..0: cache function address, with the low bits the index.

(COLD's `movec %a0,%cacr` with 9 = CC + EC: clear, then enable.)

## 7-3  Configuration

SBUS with CPU and MMU on one side; on the bus: MAIN MEM, BOOT PROM, SIO
(keyboard, SIO 1, SIO 2), REAL TIME CLK, MMU CTL; on the second bus
segment: DISP MEM, DISP CTL, DMA CTL, RING CTL, DSK/FLPY CTL (monitor,
network, disk, floppy).

## 7-4  Disk (floppy/Winchester) controller

Address: physical 9C00, virtual 0FFA800.

| offset | write                 | read                        |
|--------|-----------------------|-----------------------------|
| +00    | ANSI command          | attention status            |
| +02    | ANSI parm out         | ANSI parm in                |
| +04    |                       | drive # of status           |
| +06    | sector                | controller stat-hi          |
| +07    |                       | controller stat-lo          |
| +08    | cylinder-hi           |                             |
| +09    | cylinder-lo           |                             |
| +0A    | head                  |                             |
| +0C    | interrupt control     |                             |
| +0E    | controller cmd        |                             |
| +10    |                       | floppy status               |
| +12    | floppy write data     | floppy read data            |
| +14    | floppy control        |                             |
| +20    | calendar control      |                             |
| +22    | calendar write data   |                             |
| +24    |                       | calendar read data          |

### Controller status (9C06 / 0FFA806)

| mask | bit | meaning                        | reset by                          |
|------|-----|--------------------------------|-----------------------------------|
| 8000 | 15  | controller busy                | self clearing                     |
| 4000 | 14  | drive busy (from bus)          | self clearing                     |
| 2000 | 13  | drive attention (from bus)     | cntr, if status avail enab        |
| 1000 | 12  | status available interrupt     | read attn status reg              |
| 0800 | 11  | end of operation interrupt     | write to ctlr cmd reg             |
| 0400 | 10  | floppy interrupting            | read floppy status reg            |
| 0080 | 07  | timeout                        | write to ctlr cmd reg             |
| 0040 | 06  | overrun                        | write to ctlr cmd reg             |
| 0020 | 05  | CRC error                      | write to ctlr cmd reg             |
| 0010 | 04  | controller bus parity error    | write to ctlr cmd reg             |
| 0008 | 03  | illegal configuration          | write to ctlr cmd reg             |
| 0004 | 02  | status timeout                 | read attention status register    |
| 0002 | 01  | parity error during DMA        | write to controller command reg   |

## 7-5  ANSI commands (9000 / 0FFA800)

### Parameter out commands

| cmd | name                   | parameter                                   |
|-----|------------------------|---------------------------------------------|
| 40  | attention control      | bit 7 = 0 enable attention, 1 disable       |
| 41  | write control          | bit 7 = 0 write protect, 1 write enable     |
| 42  | load cyl addr high     | MSB of cylinder address                     |
| 43  | load cyl addr low      | LSB of cylinder address                     |
| 44  | select head            | head number (mandatory)                     |
| 50  | load attribute number  | attribute number (optional)                 |
| 51  | load attribute         | attribute                                   |
| 53  | read control           | bits 7,6 = 0x nominal strobe, 10 early, 11 late |
| 54  | offset control         | bits 7,6 = 0x no offset, 10 forward, 11 reverse |
| 55  | spin control           | bit 7 = 0 spin down, 1 spin up              |
| 56  | load sect/trk high     | MSB of sectors/track                        |
| 57  | load sect/trk medium   | MedSB of sectors/track                      |
| 58  | load sect/trk low      | LSB of sectors/track                        |
| 59  | load bytes/sect high   | MSB of bytes/sector                         |
| 5A  | load bytes/sect medium | MedSB of bytes/sector                       |
| 5B  | load bytes/sect low    | LSB of bytes/sector                         |
| 6B  | load read permit high  | MSB of read enable on cyl >=                |
| 6C  | load read permit low   | LSB of read enable cyl                      |
| 6D  | load write permit high | MSB of write enable on cyl >=               |
| 6E  | load write permit low  | LSB of write enable cyl                     |
| 6F  | load test byte         | test byte                                   |

## 7-6  Parameter in commands

| cmd | name                   | returns                         |
|-----|------------------------|---------------------------------|
| 00  | report illegal command | general status                  |
| 01  | clear fault            | general status                  |
| 02  | clear attention        | general status                  |
| 03  | seek                   | * general status                |
| 04  | rezero                 | * general status                |
| 0D  | report sense byte 2    | sense byte 2                    |
| 0E  | report sense byte 1    | sense byte 1                    |
| 0F  | report general status  | general status (mandatory)      |
| 10  | report drive attribute | drive attribute (optional)      |
| 11  | set attention          | * general status                |
| 14  | selective reset        | * general status                |
| 15  | seek to landing zone   | * general status                |
| 16  | reformat track         | * general status                |
| 29  | report cyl addr high   | MSB of cylinder address         |
| 2A  | report cyl addr low    | LSB of cylinder address         |
| 2B  | report read permit high| MSB of cylinder address         |
| 2C  | report read permit low | LSB of cylinder address         |
| 2D  | report wrt permit high | MSB of cylinder address         |
| 2E  | report wrt permit low  | LSB of cylinder address         |
| 2F  | report test byte       | test byte                       |

`*` time dependent command, attention set on completion.

### Attention status (9000 / 0FFA800)

| bit | mask | meaning            | cleared by               |
|-----|------|--------------------|--------------------------|
| 7   | 80   | normal completion* | clear attention command  |
| 6   | 40   | busy               | self clearing            |
| 5   | 20   | read sense byte 2  | see sense byte 2         |
| 4   | 10   | read sense byte 1  | see sense byte 1         |
| 3   | 08   | illegal parameter  | clear fault command      |
| 2   | 04   | illegal command    | clear fault command      |
| 1   | 02   | control bus error* | clear fault command      |
| 0   | 01   | not ready          | self clearing            |

`*` zero to one transition sets attention.

### Sense byte 1 (mand/opt)

| bit | mask | meaning               |      |
|-----|------|-----------------------|------|
| 7   | 80   | vendor unique errors  | * O  |
| 6   | 40   | other errors          | * O  |
| 5   | 20   | command reject        | * M  |
| 4   | 10   | speed error           | * O  |
| 3   | 08   | R/W permit violation  | * O  |
| 2   | 04   | power fault           | * O  |
| 1   | 02   | read/write fault      | * M  |
| 0   | 01   | seek error            | * M  |

## 7-7  Sense byte 2, interrupt control, controller commands, floppy control

### Sense byte 2 (printed "SENSE BYTE 1" again on the page; mand/opt)

| bit | mask | meaning                        |      |
|-----|------|--------------------------------|------|
| 7   | 80   | vendor unique attns            | * O  |
| 6   | 40   | in write-protected area        | M    |
| 5   | 20   | attr. table modified           | * O  |
| 4   | 10   | dev rsrved to alt port         | O    |
| 3   | 08   | forced release                 | * O  |
| 2   | 04   | dev rsrved to this port        | O    |
| 1   | 02   | ready transition               | * M  |
| 0   | 01   | initial state                  | * M  |

`*` zero to one transition sets attention.

### Interrupt control (9C0C / 0FFA80C), byte

| bit | meaning                     |
|-----|-----------------------------|
| 3   | enable end of op int        |
| 2   | enable status avail int     |
| 1   | enable attention int        |
| 0   | overall interrupt enable    |

### Controller commands (9C0E / 0FFA80E)

| cmd | meaning                            |
|-----|------------------------------------|
| 00  | no-op                              |
| 01  | read record                        |
| 02  | write record                       |
| 03  | format track                       |
| 04  | seek                               |
| 05  | execute ANSI command sequence      |
| 06  | execute drive select sequence      |
| 07  | execute attention in sequence      |
| 08  | select head                        |

Any command clears the controller status register.

### Floppy control (9C14 / 0FFA814)

Bits 1 and 0: bit 1 = 1 enables the floppy interrupt; bit 0 = 0 read,
1 write.

## 7-8  Display control and status register (DCSR)

### Display registers (9400 / 0FF9800)

| offset | write             | read            |
|--------|-------------------|-----------------|
| +00    | display control   | display status  |
| +02    | DEB               |                 |
| +04    | WSSY              |                 |
| +06    | WSSX              |                 |
| +08    | DCY               |                 |
| +0A    | DCX               |                 |
| +0C    | WSDY              |                 |
| +0E    | WSDX              |                 |

### Display control register (9400 / 0FF9800)

| mask | bit | meaning                                   |
|------|-----|-------------------------------------------|
| 8000 | 15  | GO (start BLT operation)                  |
| 0020 | 5   | interrupt at end of frame                 |
| 0010 | 4   | interrupt at end of BLT operation         |
| 0008 | 3   | increment Y coordinate                    |
| 0004 | 2   | increment X coordinate                    |
| 0002 | 1   | fill mode BLT operation                   |
| 0001 | 0   | enable display (blank if reset)           |

### Display status register (9400 / 0FF9800)

| mask | bit | meaning                     |
|------|-----|-----------------------------|
| 8000 | 15  | BLT operation in progress   |
| 0080 | 7   | end of frame interrupt      |
| 0002 | 1   | reserved                    |
| 0001 | 0   | reserved                    |

### BLT registers

Each has an address used for reading and a separate address for writing.

Destination count Y register (9414 / 0FF9814): bits 15..9 ones, bits
8..0 = CCCCCCCCC = -1 - ABS(WDSY - WDEY), the two's complement of the
number of lines in the height of the destination block.

## 7-9  BLT registers (continued)

Destination count X register (9416 / 0FF9816): bits 5..0 = CCCCCC =
-1 - ABS(WDSX/16 - WDEX/16), the two's complement of the number of
16-bit aligned words involved in the X coordinate.

Destination end bit register (941C / 0FF981C): bits 3..0 = EEEE =
WDEX mod 16, the bit number in the word of the last bit of X.

## 7-10  DMA controller

DMAC page at 9000 / 0FFA000.  The DMA controller is a Motorola M68450.

| channel | registers | use                 |
|---------|-----------|---------------------|
| 0       | 9000-903F | ring receive header |
| 1       | 9040-907F | ring receive data   |
| 2       | 9080-90BF | ring transmit       |
| 3       | 90C0-90FF | winchester/floppy   |

Register summary (for each channel):

| offset | register                         | access   |
|--------|----------------------------------|----------|
| +00    | channel status register (CSR)    | R/W      |
| +01    | channel error register (CER)     | R        |
| +04    | device control register (DCR)    | R/W      |
| +05    | operation control register (OCR) | R/W      |
| +06    | sequence control register (SCR)  | R/W      |
| +07    | channel control register (CCR)   | R/W      |
| +0A    | memory transfer counter (MTC)    | R/W      |
| +0C    | memory address register (MAR)    | R/W      |
| +14    | device address register          | not used |
| +1A    | base transfer counter (BTC)      | R/W      |
| +1C    | base address register (BAR)      | R/W      |
| +25    | normal interrupt vector          | not used |
| +27    | error interrupt vector           | not used |
| +29    | memory function code reg (MFCR)  | R/W      |
| +2D    | channel priority register (CPR)  | R/W      |
| +31    | device function code register    | not used |
| +39    | base function code register (BFCR)| R/W    |
| +FF    | general control register         | not used |

### Channel status register (CSR), 9000 / 0FFA000

| bit | name | meaning                                              |
|-----|------|------------------------------------------------------|
| 7   | COC  | 1 = channel operation complete (*)                   |
| 6   | BTC  | 1 = block transfer complete and continue (*)         |
| 5   | NDT  | 1 = normal device termination (**)                   |
| 4   | ERR  | 1 = error as coded in CER (**)                       |
| 3   | ACT  | 1 = channel active                                   |
| 2   | 0    |                                                      |
| 1   | PTC  | 1 = PCL transition occurred (*)                      |
| 0   | PCS  | state of input PCL line                              |

`(*)` bit cleared by writing a 1 to CSR.  `(**)` ditto, and clearing
also clears CER.

## 7-11  CER, DCR, OCR

### Channel error register (CER), 9001 / 0FFA001

Bits 7..5 zero; bits 4..0 = error code:

| code | meaning                                          |
|------|--------------------------------------------------|
| 00   | no error                                         |
| 01   | configuration error                              |
| 02   | operation timing error                           |
| 03   | (undefined, reserved)                            |
| 05   | address error: memory address or memory counter  |
| 06   | address error: device address                    |
| 07   | address error: base address or base counter      |
| 09   | bus error: memory address or memory counter      |
| 0A   | bus error: device address                        |
| 0B   | bus error: base address or base counter          |
| 0D   | count error: memory address or memory counter    |
| 0E   | count error: device address                      |
| 0F   | count error: base address or base counter        |
| 10   | external abort                                   |
| 11   | software abort                                   |

### Device control register (DCR), 9004 / 0FFA004 (value 28)

| bits | name | meaning                                        |
|------|------|------------------------------------------------|
| 7..6 | XRM  | 0 = burst mode transfers                       |
| 5..4 | DTYP | 1 0 = device with ACK, implicitly addressed    |
| 3    | DPS  | 1 = 16-bit port                                |
| 2    | 0    |                                                |
| 1..0 | PCL  | 0 0 = PCL = status input                       |

### Operation control register (OCR), 9005 / 0FFA005 (value 92)

| bits | name  | meaning                                          |
|------|-------|--------------------------------------------------|
| 7    | DIR   | 0 = transfer from memory to device, 1 = device to memory |
| 6    | 0     |                                                  |
| 5..4 | SIZE  | 0 1 = word transfers                             |
| 3    | CHAIN | 0 0 = chain operation disabled                   |
| 1..0 | REQG  | 1 0 = REQ line initiates transfer                |

## 7-12  SCR, CCR, MTC, MAR, DAR, BTC

### Sequence control register (SCR), 9006 / 0FFA006 (value 04)

Bits 7..4 zero; bits 3..2 MAC: 0 1 = memory address reg counts up;
bits 1..0 DAC: 0 0 = N/A (device address reg).

### Channel control register (CCR), 9007 / 0FFA007

| bit | name | meaning                  |
|-----|------|--------------------------|
| 7   | STR  | 1 = start operation      |
| 6   | CNT  | 1 = continue operation   |
| 5   | HLT  | 1 = halt operation       |
| 4   | SAB  | 1 = software abort       |
| 3   | INT  | 1 = enable interrupts    |
| 2..0| 0    |                          |

Memory transfer counter (MTC), 900A-900B / 0FFA00A-0FFA00B: bits 15..0
word count (e.g. 512 to transfer a page).

Memory address register (MAR), 900C-900F / 0FFA00C-0FFA00F: 32 bits,
HIGH at 9018(?)/UP-MID 901A/LO-MID 901C/LO 901E as printed; load with
MOVEP.L A0,9018.  (The page prints the MAR byte addresses as 9018..901E
under a 900C..900F heading; the MOVEP form is the one the code uses.)

Device address register (DAR), 9014-9017 / 0FFA014-0FFA017: not used.

Base transfer counter (BTC), 901A-901B / 0FFA01A-0FFA01B: same as memory
transfer counter.

## 7-13  BAR, vectors, MFCR, CPR, DFCR, BFCR

Base address register (BAR), 901C-901F / 0FFA01C-0FFA01F: same as memory
address register.  Normal interrupt vector register (9025 / 0FFA025) and
error interrupt vector register (9027 / 0FFA027): not used.

Memory function code register (MFCR), 9029 / 0FFA029: bits 7..2 zero,
bits 1..0 = F F: 0 0 = ring transmit data, 0 1 = ring transmit header.
Function code not used on other channels.

Channel priority register (CPR), 902D / 0FFA02D: bits 1..0 = P P:
channel 0 = 0 0 ring receive header (highest); channel 1 = 0 1 ring
receive data; channel 2 = 1 0 ring transmit; channel 3 = 1 1
winchester/floppy (lowest).

Device function code register (DFCR, 9031 / 0FFA031) and base function
code register (BFCR, 9039 / 0FFA039): not used.

## 7-14  Fault frame: bus/address error stack frame (DN300/320)

| offset | contents                 |              |
|--------|--------------------------|--------------|
| +00    | status register          | short frame  |
| +02    | program counter          |              |
| +06    | frame format / VOR       |              |
| +08    | special status word      | bus error frame starts here |
| +0A    | fault address (long)     |              |
| +0E    | internal register        |              |
| +10    | data output buffer       |              |
| +12    | internal register        |              |
| +14    | data input buffer        |              |
| +16    | internal register        |              |
| +18    | instruction register     |              |
| +1A    | internal registers       |              |
| +3A    | end                      |              |

## 7-15  Bus/address error stack frame (DN330)

Medium frame (format 1010 0000 0000 1000 at +06):

| offset | contents                |
|--------|-------------------------|
| +00    | status register         |
| +02    | program counter         |
| +06    | format word 1010 0000 0000 1000 |
| +08    | internal register       |
| +0A    | special status word     |
| +0C    | inst pipe stage c       |
| +0E    | inst pipe stage b       |
| +10    | data fault address      |
| +14    | internal registers      |
| +18    | data output buffer      |
| +1C    | internal registers      |
| +20    | end                     |

Long frame (format 1011 0000 0000 1000 at +06): the same through +1C,
then +24 stage B address, +28 internal registers, +2C data input
buffer, +30 internal registers, to +5C.

Real fault address: DF = data fault address; FB = if medium then PC + 4,
if long then stage B address; FC = if medium then PC + 2, if long then
stage B addr - 2.

Coprocessor mid-instruction frame: SR at 0, PC at 2, 9xxx at 6, instr
addr at 8, internal registers at C to 14.

## 7-16  Frame format / vector offset word and special status word (DN300/320)

Frame format word (DN300/320): bits 15..12 FMT, bit 11..10 zero, bits
9..0 vector offset.  0000 = four-word format (SR, PC, VOR); 1000 =
29-word 68010 format.

Frame format word (DN330): 0000 = 4-word format (SR, PC, VOR); 0001 =
throwaway (4 words); 0010 = instruction (trapv, chkv) (6 words); 1000 =
29-word 68010 bus error; 1001 = coprocessor mid-instruction (10 words);
1010 = 68020 medium bus error (16 words); 1011 = 68020 long bus error
(46 words).

Special status word (DN300/320), bits:

| bit   | name | meaning                                                  |
|-------|------|----------------------------------------------------------|
| 15    | RR   | 0 = processor will rerun bus cycle on RTE; 1 = software has completed the bus cycle prior to RTE |
| 13    | IF   | 1 = instruction fetch                                    |
| 12    | DF   | 0 = data store from DOB, 1 = data fetch to DIB           |
| 11    | RM   | 1 = read-modify-write cycle                              |
| 10    | HB   | 1 = high byte (valid iff BY on)                          |
| 9     | BY   | 1 = byte transfer                                        |
| 8     | RW   | 0 = write, 1 = read                                      |
| 2..0  | FCN  | 001 user data, 010 user program, 101 supervisor data, 110 supervisor program, 111 interrupt acknowledge |

## 7-17  Special status word (DN330) and fault types

| bit   | name | meaning                                   |
|-------|------|-------------------------------------------|
| 15    | FC   | fault on stage C of instruction pipe      |
| 14    | FB   | fault on stage B of instruction pipe      |
| 13    | RC   | rerun flag for stage C                    |
| 12    | RB   | rerun flag for stage B                    |
| 8     | DF   | fault/rerun flag for data cycle           |
| 7     | RM   | read-modify-write cycle                   |
| 6     | RW   | 0 = write, 1 = read                       |
| 5..4  | SIZ  | 00 longword, 01 byte, 10 word, 11 3 byte  |
| 2..0  | FCN  | 001 user data, 010 user program, 101 supervisor data, 110 supervisor prog, 111 CPU space ref |

Fault types: group 0 (reset, bus error, address error): current
instruction is aborted.  Group 1 (trace, interrupt, illegal
instruction, privilege instruction): exception occurs before next
instruction.  Group 2 (TRAP, TRAPV, CHK, zero divide): processed by
normal instruction execution.  Group 0 has the highest priority.

## 7-18  Fault vectors

Exception vector at 100400 / 0.  (+ = new on 68020, & = new on 68010,
- = unused.)

| vector | address | assignment                        | frame length was / is (020s) | FF |
|--------|---------|-----------------------------------|------------------------------|----|
| 00     | 000     | reset: initial SSP                | -                            |    |
|        | 004     | reset: initial PC                 | -                            |    |
| 02     | 008     | bus error                         | 3A / 20,5C                   | 8,A,B |
| 03     | 00C     | address error                     | 3A / 20,5C                   | 8,A,B |
| 04     | 010     | illegal instruction               | 8 / 8                        | 0  |
| 05     | 014     | zero divide                       | 8 / C                        | 0,2 |
| 06     | 018     | CHK instruction                   | 8 / C                        | 0,2 |
| 07     | 01C     | TRAPV instruction                 | 8 / C                        | 0,2 |
| 08     | 020     | privilege violation               | 8 / 8                        | 0  |
| 09     | 024     | trace                             | 8 / C                        | 0,2 |
| 0A     | 028     | unimp. instruction (A-line)       | 8 / 8                        | 0  |
| 0B     | 02C     | unimp. instruction (F-line)       | 8 / 8                        | 0  |
| 0C     | 030     | (unassigned, reserved)            | -                            |    |
| + 0D   | 034     | coprocessor protocol violation    | - / 14                       | 9  |
| & 0E   | 038     | invalid stack format              | 8 / 8                        | 0  |
| 0F     | 03C     | uninitialized vector interrupt    | -                            |    |
| 10-17  | 040     | (unassigned, reserved)            | -                            |    |
| 18     | 060     | spurious interrupt                | 8 / 8                        | 0  |
| & 19-1F| 064     | level 1-7 auto-vector             |                              |    |
| & 19   | 064     | SIO (rcv and xmit)                | 1                            |    |
| & 1A   | 068     | keyboard input                    | 2                            |    |
| & 1B   | 06C     | ring                              | 3                            |    |
| & 1C   | 070     | display                           | 4                            |    |
| & 1D   | 074     | disk/floppy                       | 5                            |    |
| & 1E   | 078     | timers 1,2,3                      | 6                            |    |
| & 1F   | 07C     | parity error                      | 7                            |    |
| 20-2F  | 080     | TRAP instruction vectors          | 8 / 8                        |    |
| + 30   | 0C0     | FP branch or set on unordered cond (FPBSUN) |                    |    |
| + 31   | 0C4     | FP inexact result (FPINEX)        |                              |    |
| + 32   | 0C8     | FP divide by zero (FPDIVZ)        |                              |    |
| + 33   | 0CC     | FP underflow (FPUNFL)             |                              |    |
| + 34   | 0D0     | FP operand error (FPOPER)         |                              |    |
| + 35   | 0D4     | FP overflow (FLOVFL)              |                              |    |
| + 36   | 0D8     | FP signalling NAN (FPSNAN)        |                              |    |
| 37-3F  | 0DC     | (unassigned, reserved)            |                              |    |

## 7-19  Floating-point format

Single: bit 31 sign, bits 30..23 exponent plus 127, bits 22..0 mantissa
(implied normalization bit above bit 22).  Double: bit 31 sign, bits
30..20 exponent plus 1023, 52-bit mantissa across two longwords.

## 7-20  Floating-point control register (DN330)

Bits 31..16 zero.  Low word, bits 15..0: 7..6 unused(?) per the
figure; rounding mode (rnd) bits 5..4: 00 to nearest, 01 toward zero,
10 toward minus infinity, 11 toward plus infinity; rounding precision
(prec) bits 7..6: 00 extended, 01 single, 10 double, 11 undefined
(reserved; reactive precision on FPX); exception enables: inexact
decimal input (inex1) bit 8, inexact operation (inex2) bit 9, divide by
zero (dz) bit 10, underflow (unfl) bit 11, overflow (ovfl) bit 12,
operand error (operr) bit 13, signalling not a number (snan) bit 14,
branch/set on unordered condition (bsun) bit 15.

## 7-21  Floating-point status register (DN330); FPU (DN320)

Condition code byte (bits 31..24): negative bit 27, zero 26, infinity
25, not a number or unordered 24.  Quotient byte (bits 23..16): sign of
quotient bit 23, seven least significant bits of quotient 22..16.
Exception status byte (bits 15..8): bsun 15, snan 14, operr 13, ovfl 12,
unfl 11, dz 10, inex2 9, inex1 8.  Accrued exception byte (bits 7..3):
iop 7, ovfl 6, unfl 5, dz 4, inex 3; bits 2..0 unused.

FPU (DN320): the FPU is essentially the same as PEB without the cache
(and different microcode).  Refer to the PEB section of chapter 8,
DN400, DN420, DN600.

## 7-22  Memory control/status registers (MCSR)

### Memory control register (DN300/320), 8005 / 0FFB405, byte

| bits | meaning                                  |
|------|------------------------------------------|
| 7..3 | LEDS (top/right .. bottom/left)          |
| 2    | 1 = enable parity error traps            |
| 1    | 1 = force right byte parity              |
| 0    | 1 = force left byte parity               |

### Memory status register (DN300/320), 8006 / 0FFB406, word

| bits  | meaning                              |
|-------|--------------------------------------|
| 15..4 | failing PPN                          |
| 3     | 1 = parity error traps enabled       |
| 2     | 1 = right byte parity error          |
| 1     | 1 = left byte parity error           |
| 0     | 1 = parity during DMA cycle          |

Writing MSR clears the parity error condition.

## 7-23  Memory control/status (DN330); LEDs/hardware rev; MMU (DN300/320)

### Memory control/status (DN330), 8004-8007 / 3FFB404-3FFB407, byte writeable

Bits 31..12 (R/O) failing A(21:2), 20 bits; bits 11..8 (CLR) byte parity
error flags (0 = error); bit 3 (R/W) failing addr bit 1(?) / 1 = B port
access; bit 2: 1 = DMA access; bit 1: 1 = parity interrupt enable; bit
0: 1 = write bad parity.  (The bit numbering of the low nibble is as the
figure's tree reads: bits 3..0 = B port access, DMA access, parity
interrupt enable, write bad parity.)

### LEDs / hardware rev register (DN330), 8008-8009 / 3FFB408-3FFB409

Bits 15..8 = LEDS xxxx (LEDs in the high nibble), bits 7..0 = hardware
revision.

### Memory management unit (MMU) (DN300/320)

PID/PRIV/POWER at 8000 / FFB400, MMU status at 8002 / FFB402.

PID/PRIV register, 8000 / 0FFB400, word: bit 15 zero; bits 14..8 ASID;
bits 7..3 dashes (unused); bit 2 D = domain (0 = domain 0, 1 = domain
1); bit 1 P = 1 enables PTT access; bit 0 M = 1 enables the MMU.

## 7-24  MMU status register; MMU (DN330)

### MMU status register, 8002 / 0FFB402, byte

| bit | meaning                     |
|-----|-----------------------------|
| 7   | 1 = access violation        |
| 6   | 1 = page fault              |
| 5   | 1 = bus timeout             |
| 4   | 1 = normal mode             |
| 3   | 1 = interrupt pending       |
| 2   | 1 = unused                  |
| 1   | 1 = PTT access enabled      |
| 0   | 1 = MMU enabled             |

### Memory management unit (MMU) (DN330)

PID/PRIV/POWER at 8000 / 3FFB400, MMU status at 8002 / 3FFB403.

PID/PRIV register, 8000 / 03FFB400, word: bit 15 zero (W/O group bits
15..8); bits 14..8 ASID; bits 7..4 dashes; bit 3 F (R/O) = FP trap, 1 if
a trap occurred, cleared by a write to the FPU owner register; bit 2 D =
domain (0 = domain 0, 1 = domain 1); bit 1 P = 1 enables PTT access; bit
0 M = 1 enables the MMU (R/W; was W/O).

## 7-25  FPU owner register and MMU status register (DN330)

### FPU owner register, 8002 / 3FFB402, byte (W/O)

Bit 7 X unused; bits 6..0 = ASID of FPU owner.

### MMU status register, 8003 / 03FFB403, byte

| bit | meaning                                                          |
|-----|------------------------------------------------------------------|
| 7   | 1 = access violation                                             |
| 6   | 1 = page fault                                                   |
| 5   | 1 = bus/MMU timeout or MMU parity error (was bus timeout only)   |
| 4   | 1 = normal mode                                                  |
| 3   | 1 = MMU error (timeout or parity error), valid only if bit 5 set (was int pending) |
| 2   | orderly shutdown (toggle)                                        |
| 1   | 1 = PTT access enabled                                           |
| 0   | 0 = Stingray 020 board (R/O) (was MMU enabled; changed for O/S)  |

Any write to the register clears bits 5-7.

## 7-26  MMU parity register (DN330), 800A-800B

| bits   | meaning                                                 |
|--------|---------------------------------------------------------|
| 15     | 1 = write wrong MMU parity (both PFT and PTT) (R/W)     |
| 14     | 1 = MMU parity fault enable (MMU PFE)                   |
| 13     | 1 = PTT parity error (CLR)                              |
| 12     | 1 = PFT parity error (CLR)                              |
| 11..0  | PFTX (PFT parity error index) (R/O)                     |

Bus error occurs on parity error in normal MMU operation if MMU PFE is
set and bits 12 and 13 WERE clear.  Diagnostic loopback register
(800C-800F): loopback of SBUS signals, TBD.

## 7-27  Page frame table entry (PFTE) and page translation table entry (PTTE)

### PFTE (type "pfte" in mmpft.pvt.pas), 32 bits

```
 31      24 23        16 15      8 7      0
 AAAAAAAA  SDWRXPPPP   EMUGLLLL  LLLLLLLL
```

| field | meaning                                  |
|-------|------------------------------------------|
| A..A  | address space ID (0-127) (.elsid)        |
| S     | supervisor domain (.elccess)             |
| D     | DOMAIN (0 or 1)                          |
| W     | write access                             |
| R     | read access                              |
| X     | execute access                           |
| P..P  | excess virtual page number (.xsvpn)      |
| E     | end of chain (.eoc)                      |
| M     | page modified (.bbmod)                   |
| U     | page referenced (.used)                  |
| G     | page is global (.global)                 |
| L..L  | PFT hash thread (.link)                  |

PFT at 4000 / FFB800 through 8000 / FFF800.  There is one entry per
physical page of memory.  (This matches the local MAME
apollo_dn300_mmu.cpp reading used in the cold-start review: link bits
0-11, global 12, used 13, mod 14, eoc 15, xsvpn 16-19, X 20, R 21, W 22,
domain 23, elaccess 24, ASID 25-31.)

### PTTE (type "ppn_t" in base.ins.pas), 16 bits

Bits 15..13 XXX junk, ignore; bits 12..0 = physical page number (PPN).
Page translation table at n/a / 700000 through n/a / 800000.  One PTTE
every 1024 bytes in the table.

PEB: refer to the FPU section.

## 7-28  PROM entry points; ring registers

### PROM entry points

| address | form   | name        | meaning                                   |
|---------|--------|-------------|-------------------------------------------|
| 100     | dc.w   | 2,0         | 2 = swallow, no aux info                  |
| 104     | ac     | getc        | returns char in D1                        |
| 108     | ac     | putc        | prints char in D1                         |
| 10C     | ac     | init_dsk    | initialize disk                           |
| 110     | ac     | read_dsk    | read a record from disk                   |
| 114     | ac     | reload_font | reload font                               |
| 118     | ac     | pollc       | returns char in D1, else -1 in d1.w       |
| 11C     | ac     | quiet_ret   | quiet return to prom                      |

### Ring registers, page at 9800 / 0FF9C00

| offset | write            | read                 |
|--------|------------------|----------------------|
| +00    | XMIT command     | XMIT status          |
| +02    | RCV command      | RCV status           |
| +04    | TMASK / unused   | TMASK / unused       |
| +06    | DIAG command     | DIAG status          |
| +08    | RING ID (long)   | RING ID (long)       |
| +0C    |                  | unused               |
| +0E    |                  | unused               |
| +10    |                  | ID3 / unused         |
| +12    |                  | ID2 / unused         |
| +14    |                  | ID1 / unused         |
| +16    |                  | ID0 / unused         |

## 7-29  Transmit command, receive command, transmit status

### Transmit command, 9800 / 0FF9C00

4000 transmit interrupt enable; 2000 transmit enable (start the
transmit); 1000 force transmit.  Notes: to start a transmit normally
use 6000; to force transmit use 7000; to stop a transmit that has
already started, clear the transmit enable bit; writing anything to
this register clears the transmit interrupt.

### Receive command, 9802 / 0FF9C02

4000 enable interrupt; 2000 enable receive (start the receive).  Notes:
to start a normal receive use 6000; to stop a receive that has already
started, clear the receive enable bit.

### Transmit status, 9800 / 0FF9C00

| mask | meaning                                                      |
|------|--------------------------------------------------------------|
| 8000 | interrupt pending                                            |
| 4000 | interrupt enabled                                            |
| 2000 | busy                                                         |
| 1000 | disconnected                                                 |
| 0800 | bi-phase error                                               |
| 0400 | elastic store buffer error                                   |
| 0200 | no return (a complete pkt frame never arrived)               |
| 0100 | crc error                                                    |
| 0080 | ack parity error (0 = no error, 1 = error detected)          |
| 0040 | external error (err during DMA, e.g. parity, bus-error)      |
| 0020 | protocol error (the pkt hdr with FROM ID never came back)    |
| 0010 | icopy (somebody intended to COPY, was willing to rcv)        |
| 0008 | ack byte errbit (somebody (anybody!) set the "error detected" bit) |
| 0004 | copy (somebody did COPY the pkt)                             |
| 0002 | wack                                                         |
| 0001 | underrun (DMA didn't keep up with xmit data rate)            |

Notes: a successful transmit will have a transmit status of 0014; a
WACK will have a transmit status of 0012.

## 7-30  Receive status, diagnostic status

### Receive status, 9802 / 0FF9C02

| mask | meaning                                                      |
|------|--------------------------------------------------------------|
| 8000 | interrupt pending                                            |
| 4000 | interrupt enabled                                            |
| 2000 | busy                                                         |
| 1000 | disconnected                                                 |
| 0800 | bi-phase error                                               |
| 0400 | elastic store buffer error                                   |
| 0200 | timeout (the hdr of a msg was seen, but it never ended)      |
| 0100 | crc error                                                    |
| 0080 | ack parity error (0 = no error, 1 = error detected)          |
| 0040 | external error (err during DMA, e.g. parity, bus-error)      |
| 0020 | DMA end of range                                             |
| 0010 | icopy (somebody before me intended to COPY)                  |
| 0008 | ack byte errbit (somebody before me set the "error detected" bit) |
| 0004 | copy (somebody before me did COPY the pkt)                   |
| 0002 | wack (somebody before me WACKed the ptk)                     |
| 0001 | overrun (DMA didn't keep up with rcv data rate)              |

### Diagnostic status, 9806 / 0FF9C06

| mask | meaning                                                      |
|------|--------------------------------------------------------------|
| 8000 | interrupt pending (bad_pkt_cnt_overflow interrupt)           |
| 4000 | interrupt enabled (bad_pkt_cnt_overflow interrupt)           |
| 2000 | connected to the network                                     |
| 1000 | sticky bi-phase error (error seen since bit was cleared)     |
| 0800 | delay on (the delay is enabled)                              |
| 0400 | sticky good_seen (good pkt seen since bit was cleared)       |
| 0200 | sticky elastic store bfr err (error seen since bit was cleared) |
| 01FF | bad packet count (9-bit counter for 1st detecting errs)      |

Notes: the counter is the number of times this node found an error in a
packet going by (regardless of packet target node ID), found the error
bit in the ackbyte clear, and so was the first to set the error bit to a
one.  The bad_pkt_cnt interrupt occurs when the counter counts from 255
to 256 (i.e. first uses its highest order bit).  The counter sticks at
511 if more than 511 errors are seen.  Writing anything to the
diagnostic command register (word) clears the interrupt and all sticky
bits.

## 7-31  Diagnostic command, TMASK

### Diagnostic command, 9806 / 0FF9C06

| mask | meaning                                                       |
|------|---------------------------------------------------------------|
| 8000 | dma test (loop xmit DMA to rcv DMA)                           |
| 4000 | enable interrupt (bad_pkt_cnt overflow interrupt)             |
| 2000 | connect (to the network)                                      |
| 1000 | disconnect (from the network)                                 |
| 0800 | delay off (disable the delay)                                 |
| 0400 | delay on (enable the delay)                                   |
| 0200 | snoop (accept all pkts but only set ackbyte for packets actually addressed to me) |

Writing anything to the register (word) clears the interrupt and all
sticky bits in the diagnostic status register.

### TMASK, 9804 / 0FF9C04

80 broadcast; 40 hardware diagnostic; 20 thank you; 10 please; 08
paging; 04 user; 02 software diagnostic; 01 xtype3.  Except for
BROADCAST these bits are software defined.

## 7-32  Serial I/O interface

SIO page at 8400 / 0FFB000.  The SIO lines are implemented with a
Signetics SC2681 DUART; the display keyboard interface with a Motorola
MC6850.  When both SIO lines are being used it is possible to have
incompatible baud rates due to limitations of the SC2681 chip: one SIO
line can't have a baud rate from group A while the other is set from
group B (group A: 50, 7200; group B: 75, 150, 2000, 19.2K).

| phys | virt    | read                                   | write                              |
|------|---------|----------------------------------------|------------------------------------|
| 8400 | 0FFB000 | mode reg A (MRA)                       | mode reg A (MRA)                   |
| 8402 | 0FFB002 | status reg A (SRA)                     | clock select reg A (CSRA)          |
| 8404 | 0FFB004 |                                        | command reg A (CRA)                |
| 8406 | 0FFB006 | rcv hld reg A (RHRA)                   | transmit hld reg A (THRA)          |
| 8408 | 0FFB008 | input port change reg (IPCR)           | aux control reg (ACR)              |
| 840A | 0FFB00A | interrupt status reg (ISR)             | interrupt mask reg (IMR)           |
| 8410 | 0FFB010 | mode reg B (MRB)                       | mode register B (MRB)              |
| 8412 | 0FFB012 | status reg B (SRB)                     | clock select reg B (CSRB)          |
| 8414 | 0FFB014 |                                        | command reg B (CRB)                |
| 8416 | 0FFB016 | rcv hld reg B (RHRB)                   | transmit hld reg B (THRB)          |
| 841A | 0FFB01A | input port register (IPR)              | output port config reg (OPCR)      |
| 841C | 0FFB01C |                                        | set output port reg (OPR)          |
| 841E | 0FFB01E |                                        | reset output port reg (OPR)        |
| 8420 | 0FFB020 | display keyboard status / command register |                              |
| 8422 | 0FFB022 | display keyboard data I/O register     |                                    |

## 7-33  Mode register A (8400 / 0FFB000)

First access: bits 1..0 bits per char (00 = 5, 01 = 6, 10 = 7, 11 = 8);
bit 2 parity type (0 even, 1 odd); bits 4..3 parity mode (00 check
parity, 01 force parity, 10 no parity, 11 special multidrop mode); bit 5
error mode (0 report error on each char, 1 accumulate error info since
last reset err command); bit 6 RX int select (0 interrupt on receiver
ready, 1 interrupt on input FIFO full); bit 7 (1 = drop RTS (OP0) when
input FIFO is full).

Second and subsequent accesses (until mode register pointer reset):
bits 3..0 stop bits (0111 = 1, 1000 = 1.5, 1111 = 2); bit 4 CTS enable
(0 transmit regardless of CTS (IP0), 1 wait for CTS to transmit); bit 5
TX RTS control (0 leave RTS as is, 1 drop RTS (OP0) after transmitter
disabled); bits 7..6 channel mode (00 normal, 01 auto echo, 10 local
loop, 11 remote loop).

## 7-34  Status register A, clock select register A

### Status register A (8402 / 0FFB002), read-only

| bit | meaning                                                         |
|-----|-----------------------------------------------------------------|
| 0   | 1 = input data ready (reset by reading RHR)                     |
| 1   | 1 = input FIFO full (reset when RHR read and no data in shift reg) |
| 2   | 1 = transmitter ready (reset when THR loaded)                   |
| 3   | 1 = transmitter underrun (reset when THR loaded)                |
| 4   | 1 = rcver overrun (reset by reset error status cmd)             |
| 5   | 1 = rcv parity error (reset by reset error status cmd)          |
| 6   | 1 = receive framing error (reset by reset err status cmd)       |
| 7   | 1 = break received (reset by ???)                               |

### Clock select register A (8402 / 0FFB002), write-only

Bits 7..4 receive clock, bits 3..0 transmit clock:

| code | ACR[7]=0 | ACR[7]=1 | code | ACR[7]=0 | ACR[7]=1 |
|------|----------|----------|------|----------|----------|
| 0    | 50       | 75       | 8    | 2400     | 2400     |
| 1    | 110      | 110      | 9    | 4800     | 4800     |
| 2    | 134.5    | 134.5    | A    | 7200     | 1800(??) |
| 3    | 200      | 150      | B    | 9600     | 9600     |
| 4    | 300      | 300      | C    | 38.4K    | 19.2K    |
| 5    | 600      | 600      | D    | timer    | timer    |
| 6    | 1200     | 1200     | E    | IP4-16X  | IP4-16X  |
| 7    | 1050     | 2000     | F    | IP4-1X   | IP4-1X   |

## 7-35  Command register A, holding register A, input port change register

### Command register A (8404 / 0FFB004), write-only

Bit 0: 1 = enable receiver; bit 1: 1 = disable receiver; bit 2: 1 =
enable transmitter; bit 3: 1 = disable transmitter; bits 6..4 command:
000 no-op, 001 reset mode register pointer, 010 reset and disable
receiver, 011 reset transmitter, 100 reset error status, 101 reset
break-change interrupt, 110 start transmitting break, 111 stop
transmitting break; bit 7 must be zero.

### Receive/transmit holding register A (8406 / 0FFB006)

Read = top byte in input FIFO; write = byte of data to transmit.

### Input port change register (8408 / 0FFB008), read-only

Bits 7..4 = change in IPx, bits 3..0 = state of IPx: IP0 clear to send
(CTS) A, IP1 CTS B, IP2 data carrier detect (DCD) A, IP3 DCD B.

## 7-36  Auxiliary control register, interrupt status and mask registers

### Auxiliary control register (8408 / 0FFB008), write-only

Bit 0 enable int on change in IP0; bit 1 IP1; bit 2 IP2; bit 3 IP3;
bits 6..4 counter/timer stuff; bit 7 baud rate generator set select
(see clock select reg).

### Interrupt status register (840A / 0FFB00A), read-only

Bit 0 transmitter ready A; bit 1 receiver ready or input FIFO full A;
bit 2 beginning or end of break A; bit 3 counter ready (not used); bit
4 transmitter ready B; bit 5 receiver ready or input FIFO full B; bit 6
beginning or end of break B; bit 7 input port change status.

### Interrupt mask register (840A / 0FFB00A), write-only

Each bit enables the corresponding bit in the interrupt status register.

Mode register B (8410 / 0FFB010): see mode register A.  Status/clock
select register B (8412 / 0FFB012): see status/clock select register A.

## 7-37  Channel B registers, input port, output port configuration and set

Command register B (8414 / 0FFB014): see command register A.
Receive/transmit holding register B (8416 / 0FFB016): see A.

Input port register (841A / 0FFB01A), read-only: IP7..IP0; IP0 CTS A,
IP1 CTS B, IP2 DCD A, IP3 DCD B, IP4-IP7 undefined.

Output port configuration register (841A / 0FFB01A), write-only: load
with 0.  This selects OP0 = ready to send (RTS) A, OP1 = RTS B, OP2 =
data terminal ready (DTR) A, OP3 = DTR B, OP4-OP6 unused, OP7 = speaker
control.

Set output port register (OPR) (841C / 0FFB01C), write-only: OP7..OP0;
OP0 RTS A, OP1 RTS B, OP2 DTR A, OP3 DTR B, OP4-OP6 unused, OP7 = turn
off speaker.

## 7-38  Reset output port register, display keyboard status register

Reset output port register (OPR) (841E / 0FFB01E), write-only: the same
bits; OP7 = turn on speaker.

### Display keyboard status register (8420 / 0FFB020), read-only

| bit | meaning                                            |
|-----|----------------------------------------------------|
| 0   | receive data register full                         |
| 1   | transmit data register empty                       |
| 2   | no DCD (always 0)                                  |
| 3   | no CTS (always 0)                                  |
| 4   | receive framing error                              |
| 5   | receive overrun (reset by reading data)            |
| 6   | receive parity error                               |
| 7   | interrupt request (cleared by data read or write)  |

## 7-39  Display keyboard command register and data register

### Display keyboard command register (8420 / 0FFB020), write-only

Bits 1..0 clock: 00 clock/1, 01 clock/16, 10 clock/64, 11 master reset.
Bits 4..2 format: 000 = 7 bits, even parity, 2 stop bits; 001 = 7 bits,
odd parity, 2 stop bits; 010 = 7 bits, even parity, 1 stop bit; 011 = 7
bits, odd parity, 1 stop bit; 100 = 8 bits, 2 stop bits; 101 = 8 bits,
1 stop bit; 110 = 8 bits, even parity, 1 stop bit; 111 = 8 bits, odd
parity, 1 stop bit.  Bits 6..5: 00 = set RTS, disable transmitter
interrupt; 01 = set RTS, enable; 10 = reset RTS, disable; 11 = set RTS,
transmit break, disable transmitter interrupt.  Bit 7: 1 = enable
receiver interrupts (receive data register full, overrun, loss of DCD).

### Display keyboard data register (8422 / 0FFB022)

Read = empties receive data register; write = loads transmit data
register.
