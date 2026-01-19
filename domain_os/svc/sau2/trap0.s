*
* SVC_$TRAP0 - Domain/OS TRAP #0 System Call Dispatcher
*
* TRAP #0 handles "simple" system calls (0-31) that take no arguments
* or handle their own argument validation. Unlike TRAP #5, this handler
* does NOT validate user stack pointers or copy arguments.
*
* From: 0x00e7b044
*
* Convention:
*   - D0.w = syscall number (0-31, 0x00-0x1F)
*   - No arguments passed via user stack
*   - Handler called directly, responsible for own argument handling
*
* Note: If syscall number >= 32, control falls through to TRAP1's
* range check, eventually reaching the error handler.
*

        .text
        .even

*----------------------------------------------------------------------
* SVC_$TRAP0 - Simple syscall dispatcher (no argument passing)
*
* Input:
*   D0.w = syscall number (0x00-0x1F)
*
* Processing:
*   1. Validate syscall number < 32
*   2. Look up handler in SVC_$TRAP0_TABLE
*   3. Call handler directly (no args)
*   4. Return via FIM_$EXIT (RTE)
*
* Original bytes at 0xe7b044:
*   b0 7c 00 20    cmp.w #$20,D0
*   64 16          bcc.b SVC_$TRAP1_RANGE_CHECK
*   e5 48          lsl.w #2,D0
*   43 fa 02 90    lea (SVC_$TRAP0_TABLE,PC),A1
*   20 71 00 00    movea.l (0,A1,D0.w),A0
*   4e 90          jsr (A0)
*   4e f9 00 e2 28 bc  jmp FIM_$EXIT
*----------------------------------------------------------------------

SVC_$TRAP0:
        cmp.w   #$20,D0                 * Check syscall number < 32
        bcc.b   SVC_$TRAP1_RANGE_CHECK  * If >= 32, fall through to TRAP1 check

        lsl.w   #2,D0                   * D0 = syscall_num * 4 (table index)
        lea     SVC_$TRAP0_TABLE(PC),A1 * A1 = table base
        movea.l (0,A1,D0.w),A0          * A0 = handler address

        jsr     (A0)                    * Call handler (no arguments)
        jmp     FIM_$EXIT               * Return to user mode via RTE

*----------------------------------------------------------------------
* External references
*----------------------------------------------------------------------

        .extern FIM_$EXIT               * Return from exception (RTE)
        .extern SVC_$TRAP1_RANGE_CHECK  * TRAP1's range validation (fallthrough)
        .extern SVC_$INVALID_SYSCALL    * Invalid syscall handler
        .extern SVC_$UNIMPLEMENTED      * Unimplemented syscall handler

*----------------------------------------------------------------------
* SVC_$TRAP0_TABLE - System call handler table for TRAP #0
*
* 32 entries (0x00-0x1F), each a 32-bit pointer to the handler.
*
* From: 0x00e7b2de
*----------------------------------------------------------------------

        .data
        .even

SVC_$TRAP0_TABLE:
        .long   PROC2_$DELETE           * 0x00: Delete process
        .long   FUN_00e0aa04            * 0x01: Unknown (0xe0aa04)
        .long   SVC_$INVALID_SYSCALL    * 0x02: Invalid
        .long   DTTY_$RELOAD_FONT       * 0x03: Reload display font
        .long   FILE_$UNLOCK_ALL        * 0x04: Unlock all files
        .long   PEB_$ASSOC              * 0x05: Associate PEB
        .long   PEB_$DISSOC             * 0x06: Dissociate PEB
        .long   PROC2_$MY_PID           * 0x07: Get my PID
        .long   SMD_$OP_WAIT_U          * 0x08: SMD operation wait (user)
        .long   TPAD_$RE_RANGE          * 0x09: Tablet pad re-range
        .long   SVC_$INVALID_SYSCALL    * 0x0A: Invalid
        .long   SVC_$UNIMPLEMENTED      * 0x0B: Unimplemented
        .long   SVC_$INVALID_SYSCALL    * 0x0C: Invalid
        .long   ACL_$UP                 * 0x0D: ACL up (increase privilege)
        .long   ACL_$DOWN               * 0x0E: ACL down (decrease privilege)
        .long   SVC_$UNIMPLEMENTED      * 0x0F: Unimplemented
        .long   TPAD_$INQ_DTYPE         * 0x10: Inquire tablet device type
        .long   SVC_$INVALID_SYSCALL    * 0x11: Invalid
        .long   CACHE_$CLEAR            * 0x12: Clear cache
        .long   RIP_$ANNOUNCE_NS        * 0x13: RIP announce name server
        .long   SVC_$UNIMPLEMENTED      * 0x14: Unimplemented
        .long   SVC_$UNIMPLEMENTED      * 0x15: Unimplemented
        .long   SVC_$UNIMPLEMENTED      * 0x16: Unimplemented
        .long   SVC_$INVALID_SYSCALL    * 0x17: Invalid
        .long   PROC2_$DELIVER_PENDING  * 0x18: Deliver pending signals
        .long   PROC2_$COMPLETE_FORK    * 0x19: Complete fork operation
        .long   PACCT_$STOP             * 0x1A: Stop process accounting
        .long   PACCT_$ON               * 0x1B: Enable process accounting
        .long   ACL_$GET_LOCAL_LOCKSMITH * 0x1C: Get local locksmith SID
        .long   ACL_$IS_SUSER           * 0x1D: Check if superuser
        .long   SVC_$INVALID_SYSCALL    * 0x1E: Invalid
        .long   SMD_$N_DEVICES          * 0x1F: Get number of SMD devices

SVC_$TRAP0_TABLE_END:

*----------------------------------------------------------------------
* Handler function references
*----------------------------------------------------------------------

        .extern PROC2_$DELETE
        .extern FUN_00e0aa04
        .extern DTTY_$RELOAD_FONT
        .extern FILE_$UNLOCK_ALL
        .extern PEB_$ASSOC
        .extern PEB_$DISSOC
        .extern PROC2_$MY_PID
        .extern SMD_$OP_WAIT_U
        .extern TPAD_$RE_RANGE
        .extern ACL_$UP
        .extern ACL_$DOWN
        .extern TPAD_$INQ_DTYPE
        .extern CACHE_$CLEAR
        .extern RIP_$ANNOUNCE_NS
        .extern PROC2_$DELIVER_PENDING
        .extern PROC2_$COMPLETE_FORK
        .extern PACCT_$STOP
        .extern PACCT_$ON
        .extern ACL_$GET_LOCAL_LOCKSMITH
        .extern ACL_$IS_SUSER
        .extern SMD_$N_DEVICES

        .end
