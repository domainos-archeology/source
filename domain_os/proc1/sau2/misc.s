/*
 * PROC1_$GET_USP, PROC1_$GET_INFO_INT - Miscellaneous assembly functions
 *
 * Original addresses:
 *   PROC1_$GET_USP:      0x00e20f0c
 *   PROC1_$GET_INFO_INT: 0x00e20f12
 */

        .text
        .even

/*
 * External references
 */
        .extern PCBS
        .extern PROC1_$BEGIN_ATOMIC_OP
        .extern PROC1_$END_ATOMIC_OP
        .extern FIM_$GET_USER_SR_PTR

/*
 * PROC1_$GET_USP - Get current user stack pointer
 *
 * Returns the value of the User Stack Pointer register.
 * Must be called in supervisor mode.
 *
 * Returns: USP value in D0
 */
        .globl  PROC1_$GET_USP
        .globl  _PROC1_$GET_USP
PROC1_$GET_USP:
_PROC1_$GET_USP:
        move.l  %usp, %a0               /* Get USP into A0 */
        move.l  %a0, %d0                /* Return in D0 */
        rts

/*
 * PROC1_$GET_INFO_INT - Get process info from interrupt context
 *
 * Retrieves register information for a suspended process by examining
 * its saved state on the stack.
 *
 * Parameters (on stack):
 *   0x10(SP): pid - Process ID
 *   0x12(SP): stack_base - Base of process stack
 *   0x16(SP): stack_top - Top of process stack
 *   0x1A(SP): usr_ret - Pointer to store user SR
 *   0x1E(SP): upc_ret - Pointer to store user PC
 *   0x22(SP): usb_ret - Pointer to store user stack base
 *   0x26(SP): usp_ret - Pointer to store user stack pointer
 */
        .globl  PROC1_$GET_INFO_INT
        .globl  _PROC1_$GET_INFO_INT
PROC1_$GET_INFO_INT:
_PROC1_$GET_INFO_INT:
        bsr.w   PROC1_$BEGIN_ATOMIC_OP  /* Begin atomic operation */

        movem.l %a2-%a4, -(%sp)         /* Save registers */

        /* Get user SR pointer from FIM */
        move.w  0x10(%sp), -(%sp)       /* Push PID */
        jsr     FIM_$GET_USER_SR_PTR
        move.w  (%sp)+, %d1             /* Pop PID, keep in D1 */
        movea.l %d0, %a0                /* A0 = pointer to saved SR */

        /* Load return pointers */
        movem.l 0x1A(%sp), %a1-%a4      /* A1=usr, A2=upc, A3=usb, A4=usp */

        /* Copy user SR and PC */
        move.w  (%a0)+, (%a1)           /* *usr_ret = saved SR */
        move.l  (%a0), (%a2)            /* *upc_ret = saved PC */

        /* Get PCB for this process */
        lsl.w   #2, %d1                 /* PID * 4 */
        lea     PCBS, %a0
        movea.l (0, %a0, %d1*1), %a0    /* A0 = PCB */

        /* Get saved USP from PCB */
        move.l  0x38(%a0), (%a4)        /* *usp_ret = pcb->save_usp */

        /* Walk stack to find stack base
         * Start from saved A6 (frame pointer) and walk until
         * we find a frame outside the valid stack range
         */
        movea.l 0x30(%a0), %a4          /* A4 = saved A6 (frame pointer) */
        movem.l 0x12(%sp), %d0/%a1      /* D0 = stack_base, A1 = stack_top */

walk_frames:
        cmp.l   %a4, %d0                /* Check if frame < stack_base */
        bhi.s   walk_done               /* If so, done */
        cmpa.l  %a4, %a1                /* Check if frame > stack_top */
        bcs.s   walk_done               /* If so, done */
        move.l  %a4, %d1                /* Check alignment */
        btst.l  #0, %d1
        bne.s   walk_done               /* Odd address, done */
        movea.l (%a4), %a4              /* Follow frame link */
        cmp.l   %a4, %d1                /* Check if we're making progress */
        bcs.s   walk_frames             /* Keep walking if frame ptr increased */

walk_done:
        move.l  %a4, (%a3)              /* *usb_ret = stack base found */

        movem.l (%sp)+, %a2-%a4         /* Restore registers */

        bra.w   PROC1_$END_ATOMIC_OP    /* End atomic and return */
