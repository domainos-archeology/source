; NULLPROC - Null/Idle Process for Domain/OS (SAU2 - M68K)
;
; This is the idle process that runs when no other process is ready to execute.
; It initializes all general-purpose registers to -1 (0xFFFFFFFF) and then
; enters an infinite loop calling STOP, which puts the processor in a low-power
; state waiting for interrupts.
;
; The STOP instruction with status word 0x2000 sets:
;   - Supervisor mode (S=1)
;   - Interrupt priority level 0 (allowing all interrupts)
;
; When an interrupt occurs, the processor exits the STOP state, handles the
; interrupt, and returns to execute the BRA which loops back to STOP.
;
; Address: 0x00e24c60
; Size: 22 bytes

        .text
        .globl  NULLPROC

NULLPROC:
        moveq   #0x0f,d0                ; Initialize loop counter to 15 (16 iterations)
.Lpush_loop:
        move.l  #-1,-(sp)               ; Push 0xFFFFFFFF onto stack
        dbf     d0,.Lpush_loop          ; Decrement d0, loop until d0 == -1

        ; Pop 15 values into registers D0-D7 and A0-A6
        ; This sets all general registers to -1 (0xFFFFFFFF)
        movem.l (sp)+,d0-d7/a0-a6

.Lidle_loop:
        stop    #0x2000                 ; Enter low-power state, supervisor mode, IPL=0
        bra.s   .Lidle_loop             ; Loop back after interrupt returns
