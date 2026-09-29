|
| SVC_$TRAP8 - Domain/OS TRAP #8 Variable-Argument System Call Dispatcher
| plus the SVC_CATCHER fault tail shared by every TRAP dispatcher.
|
| This is the entry point for variable-argument system calls in Domain/OS.
| Unlike TRAP #1-5 and #7 which have fixed argument counts, TRAP #8 uses a
| lookup table to determine how many arguments each syscall expects.
|
| User programs invoke system calls via TRAP #8 with:
|   - D0.w = syscall number (0-55)
|   - Arguments on user stack (variable count per syscall)
|
| From: 0x00E7B240 (SAU2 map: "E7B240  SVC_$TRAP8", in segment SVC_CATCHER at
| 0xE7B044, size 0xE40).  This file covers 0x00E7B240..0x00E7B2DD, i.e. all of
| SVC_$TRAP8 *and* the fault tail that follows it:
|
|   0x00E7B298  SVC_$INVALID_SYSCALL
|   0x00E7B2A0  SVC_$BAD_USER_PTR
|   0x00E7B2A6  SVC_$GENERATE_FAULT
|   0x00E7B2C4  SVC_$ILLEGAL_USP_UNLK
|   0x00E7B2C6  SVC_$ILLEGAL_USP_JMP    (the fixed-arg dispatchers' entry;
|                                        they have no LINK frame to unlk)
|   0x00E7B2CC  SVC_$UNIMPLEMENTED
|
| The tail lives here rather than in trap5.s because SVC_$TRAP8 is the only
| dispatcher that reaches it with 8-bit branches (0xE7B26A, 0xE7B276) - it is
| the block immediately below the tail in the image.  TRAP #0-5 and #7 sit
| further away and reach it through their own `bra.w` stubs, which relocate
| across object files; the 8-bit branches here cannot.  Every other trapN.s
| therefore `.extern`s these labels.  Code at 0x00E7B2DE is data (a pointer
| table beginning 0x00E74398).
|
| Note: TRAP #6 is not used for SVC calls (points to FIM_$UNDEF_TRAP).
|       This dispatcher handles M68K TRAP #8 instruction.
|
| The `bra.w SVC_$BAD_USER_PTR` at 0x00E7B23C, immediately below this entry,
| is the tail of SVC_$TRAP7 (0x00E7B1D8..0x00E7B23F) and is emitted by
| svc/sau2/trap7.s, not here.
|
| Unique features of TRAP8:
|   1. Variable argument counts per syscall (looked up from SVC_$TRAP8_ARGCOUNT)
|   2. Creates stack frame via LINK instruction
|   3. Uses loop to copy and validate arguments
|   4. Negative argcount values skip argument validation entirely
|
| Address space check:
|   0xCC0000 = boundary between user and kernel space
|   USP and all argument pointers must be < 0xCC0000
|
| Tables:
|   SVC_$TRAP8_ARGCOUNT at 0xE7BE4A - byte table of argument counts (56 entries)
|   SVC_$TRAP8_TABLE at 0xE7BD6A - handler addresses (56 entries)
|

        .include "svc/sau2/svc_macros.inc"

        .section ".text.SVC_$TRAP8","ax",@progbits
        .even

|----------------------------------------------------------------------
| SVC_$TRAP8 - Main system call entry point
|
| Input:
|   D0.w = syscall number (0x00-0x37)
|
| Processing:
|   1. Load argcount table address
|   2. Validate syscall number (< 0x38)
|   3. Create stack frame
|   4. Look up argument count for this syscall
|   5. If argcount negative, skip validation (special handling)
|   6. Validate USP range
|   7. Loop: copy and validate each argument
|   8. Look up handler and call it
|   9. Clean up frame and return via RTE
|----------------------------------------------------------------------

        .global SVC_$TRAP8

SVC_$TRAP8:
        lea     (SVC_$TRAP8_ARGCOUNT:w,%pc),%a1 | 00e7b240  43 fa 0c 08 -> 0xE7BE4A
        | 00e7b244  b0 7c 00 38: CMP.W #imm,D0 (see svc_macros.inc)
        cmp_w_imm 0x38, 0               | Check syscall number < 56
        bcc.b   SVC_$TRAP8_INVALID      | 00e7b248  64 44 -> 0xE7B28E

        link    %a6,#0                  | 00e7b24a  4e 56 00 00
        clr.w   %d1                     | 00e7b24e  42 41
        move.b  (0,%a1,%d0:w),%d1       | 00e7b250  12 31 00 00  D1 = argcount
        blt.b   SVC_$TRAP8_NO_ARGS      | 00e7b254  6d 24 -> 0xE7B27A

        | Normal path: validate and copy arguments
        move    %usp,%a1                | 00e7b256  4e 69
        swap    %d0                     | 00e7b258  48 40  save syscall# high
        move.w  %d1,%d0                 | 00e7b25a  30 01  D0.w = loop counter
        lsl.w   #2,%d1                  | 00e7b25c  e5 49  D1 = argcount * 4
        lea     (0x8,%a1,%d1:w),%a1     | 00e7b25e  43 f1 10 08  end of args + 4

        move.l  #0xCC0000,%d1           | 00e7b262  22 3c 00 cc 00 00
        cmpa.l  %d1,%a1                 | 00e7b268  b3 c1
        bhi.b   SVC_$ILLEGAL_USP_UNLK   | 00e7b26a  62 58 -> 0xE7B2C4

        | Argument copy loop (copies from highest to lowest address)
SVC_$TRAP8_ARG_LOOP:
        movea.l -(%a1),%a0              | 00e7b26c  20 61
        move.l  %a0,-(%sp)              | 00e7b26e  2f 08
        cmp.l   %a0,%d1                 | 00e7b270  b2 88
        dbls    %d0,SVC_$TRAP8_ARG_LOOP | 00e7b272  53 c8 ff f8
        bls.b   SVC_$BAD_USER_PTR       | 00e7b276  63 28 -> 0xE7B2A0

        swap    %d0                     | 00e7b278  48 40  restore syscall#

SVC_$TRAP8_NO_ARGS:
        | Look up and call handler
        lsl.w   #2,%d0                  | 00e7b27a  e5 48  D0 = syscall# * 4
        lea     (SVC_$TRAP8_TABLE:w,%pc),%a1 | 00e7b27c  43 fa 0a ec -> 0xE7BD6A
        movea.l (0,%a1,%d0:w),%a0       | 00e7b280  20 71 00 00
        jsr     (%a0)                   | 00e7b284  4e 90

        | Clean up and return to user mode
        unlk    %a6                     | 00e7b286  4e 5e
        jmp     FIM_$EXIT               | 00e7b288  4e f9 00 e2 28 bc

|----------------------------------------------------------------------
| SVC_$TRAP8_INVALID - syscall number >= 0x38 (0x00E7B28E)
|
| The LINK frame is torn down only when A1 still holds SVC_$TRAP8_TABLE; on
| the range-check path A1 is the argcount table and no frame was built yet.
| Falls into SVC_$INVALID_SYSCALL.
|----------------------------------------------------------------------

        .global SVC_$TRAP8_INVALID
SVC_$TRAP8_INVALID:
        lea     (SVC_$TRAP8_TABLE:w,%pc),%a0 | 00e7b28e  41 fa 0a da -> 0xE7BD6A
        cmpa.l  %a0,%a1                 | 00e7b292  b3 c8
        bne.b   SVC_$INVALID_SYSCALL    | 00e7b294  66 02 -> 0xE7B298
        unlk    %a6                     | 00e7b296  4e 5e
        | Fall through to SVC_$INVALID_SYSCALL

|----------------------------------------------------------------------
| SVC_$INVALID_SYSCALL - Invalid syscall number handler (0x00E7B298)
|
| Reached from every dispatcher's range check (TRAP #0-5 and #7 branch here
| through their own bra.w stubs).
|----------------------------------------------------------------------

        .global SVC_$INVALID_SYSCALL
SVC_$INVALID_SYSCALL:
        move.l  #0x00120007,%d0         | 00e7b298  20 3c 00 12 00 07
                                        | status_$fault_invalid_SVC_code
        bra.b   SVC_$GENERATE_FAULT     | 00e7b29e  60 06 -> 0xE7B2A6

|----------------------------------------------------------------------
| SVC_$BAD_USER_PTR - Bad user pointer handler (0x00E7B2A0)
|
| Called when a syscall argument pointer >= 0xCC0000.
|----------------------------------------------------------------------

        .global SVC_$BAD_USER_PTR
SVC_$BAD_USER_PTR:
        move.l  #0x0012000B,%d0         | 00e7b2a0  20 3c 00 12 00 0b
                                        | status_$fault_protection_boundary_violation
        | Fall through to SVC_$GENERATE_FAULT

|----------------------------------------------------------------------
| SVC_$GENERATE_FAULT - Common fault generation code (0x00E7B2A6)
|
| Input:
|   D0 = fault status code
|
| Resets SP to the current process's OS stack (OS_STACK_BASE[PROC1_$CURRENT]
| minus 8), pushes the status and calls FIM_$GENERATE to deliver the fault.
|----------------------------------------------------------------------

| OS_STACK_BASE is PROC1_$DATA.os_stack_base (proc1/proc1.h): the map's
| OS_STACK_BASE at 0xE25C18 is A5+0x730 of the PROC1_ block, element pid at
| +pid*4.  An alias keeps the operand below resolvable (source-l2yd).
        .set    OS_STACK_BASE, PROC1_$DATA + 0x730

        .global SVC_$GENERATE_FAULT
SVC_$GENERATE_FAULT:
        move.w  PROC1_$CURRENT,%d1      | 00e7b2a6  32 39 00 e2 06 08
        lsl.w   #2,%d1                  | 00e7b2ac  e5 49  D1 = index * 4
        lea     OS_STACK_BASE,%a0       | 00e7b2ae  41 f9 00 e2 5c 18
        movea.l (0,%a0,%d1:w),%a1       | 00e7b2b4  22 70 10 00
        lea     (-0x8,%a1),%sp          | 00e7b2b8  4f e9 ff f8
        move.l  %d0,-(%sp)              | 00e7b2bc  2f 00  push status
        jsr     FIM_$GENERATE           | 00e7b2be  4e b9 00 e2 14 a8

|----------------------------------------------------------------------
| SVC_$ILLEGAL_USP_UNLK - 0x00E7B2C4
|
| Reached two ways: by falling out of the jsr above (FIM_$GENERATE does not
| normally return), and by SVC_$TRAP8's `bhi.b` at 0x00E7B26A when the TRAP #8
| argument frame runs past 0xCC0000 - that path has a live LINK frame, which
| is what the `unlk` is for.
|----------------------------------------------------------------------

        .global SVC_$ILLEGAL_USP_UNLK
        .global SVC_$ILLEGAL_USP_JMP
SVC_$ILLEGAL_USP_UNLK:
        unlk    %a6                     | 00e7b2c4  4e 5e
SVC_$ILLEGAL_USP_JMP:
        jmp     FIM_$ILLEGAL_USP        | 00e7b2c6  4e f9 00 e2 15 8a

|----------------------------------------------------------------------
| SVC_$UNIMPLEMENTED - Unimplemented syscall handler (0x00E7B2CC)
|
| Sits in the TRAPn handler tables for syscalls that are not implemented, so
| it is entered with the dispatcher's LINK frame live when A1 still holds the
| TRAP #8 handler table; that frame is torn down before the fault is raised.
|----------------------------------------------------------------------

        .global SVC_$UNIMPLEMENTED
SVC_$UNIMPLEMENTED:
        lea     (SVC_$TRAP8_TABLE:w,%pc),%a0 | 00e7b2cc  41 fa 0a 9c -> 0xE7BD6A
        cmpa.l  %a0,%a1                 | 00e7b2d0  b3 c8
        bne.b   SVC_$UNIMPLEMENTED_2    | 00e7b2d2  66 02 -> 0xE7B2D6
        unlk    %a6                     | 00e7b2d4  4e 5e
SVC_$UNIMPLEMENTED_2:
        move.l  #0x0012001C,%d0         | 00e7b2d6  20 3c 00 12 00 1c
                                        | status_$fault_unimplemented_SVC
        bra.b   SVC_$GENERATE_FAULT     | 00e7b2dc  60 c8 -> 0xE7B2A6

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | 0xE228BC: return from exception (RTE)
        .extern FIM_$GENERATE           | 0xE214A8: fault generation
        .extern FIM_$ILLEGAL_USP        | 0xE2158A: illegal USP handler
        .extern PROC1_$CURRENT          | 0xE20608: current process index
        .extern PROC1_$DATA             | 0xE254E8: PROC1_ block (OS_STACK_BASE = +0x730)
        .extern SVC_$TRAP8_TABLE        | 0xE7BD6A: handler table (svc_tables.c)
        .extern SVC_$TRAP8_ARGCOUNT     | 0xE7BE4A: argument count table (svc_tables.c)

        .end
