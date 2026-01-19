*
* SVC_$TRAP5 - Domain/OS TRAP #5 System Call Dispatcher
*
* This is the main entry point for all system calls in Domain/OS.
* User programs invoke system calls via TRAP #5 with:
*   - D0.w = syscall number (0-98)
*   - Arguments on user stack (up to 5 longwords)
*
* From: 0x00e7b17c
*
* Memory layout:
*   0x00e7b170: Branch to invalid syscall handler
*   0x00e7b174: Branch to illegal USP handler
*   0x00e7b178: Branch to bad user pointer handler
*   0x00e7b17c: SVC_$TRAP5 main entry
*   0x00e7baf2: SVC_$TRAP5_TABLE (99 handler pointers)
*
* Address space check:
*   0xCC0000 = boundary between user and kernel space
*   USP and all argument pointers must be < 0xCC0000
*

        .text
        .even

*----------------------------------------------------------------------
* Branch stubs for error handlers (these precede main entry)
*----------------------------------------------------------------------

SVC_$INVALID_JUMP:
        bra.w   SVC_$INVALID_SYSCALL    * 0xe7b170: invalid syscall number

SVC_$ILLEGAL_USP_JUMP:
        bra.w   FIM_$ILLEGAL_USP        * 0xe7b174: USP >= 0xCC0000

SVC_$BAD_PTR_JUMP:
        bra.w   SVC_$BAD_USER_PTR       * 0xe7b178: argument pointer >= 0xCC0000

*----------------------------------------------------------------------
* SVC_$TRAP5 - Main system call entry point
*
* Input:
*   D0.w = syscall number (0x00-0x62)
*   USP points to argument frame:
*     (USP+0x04) = arg1
*     (USP+0x08) = arg2
*     (USP+0x0C) = arg3
*     (USP+0x10) = arg4
*     (USP+0x14) = arg5
*
* Processing:
*   1. Validate syscall number (< 0x63)
*   2. Look up handler in SVC_$TRAP5_TABLE
*   3. Validate USP < 0xCC0000
*   4. Validate and copy up to 5 arguments from user stack
*   5. Call handler
*   6. Clean up stack and return via RTE
*----------------------------------------------------------------------

SVC_$TRAP5:
        cmp.w   #$63,D0                 * Check syscall number limit
        bcc.b   SVC_$INVALID_JUMP       * Branch if D0 >= 99

        lsl.w   #2,D0                   * D0 = syscall_num * 4 (table index)
        lea     SVC_$TRAP5_TABLE(PC),A0 * A0 = table base address
        movea.l (0,A0,D0.w),A0          * A0 = handler address from table

        move    USP,A1                  * A1 = user stack pointer
        move.l  #$CC0000,D1             * D1 = user/kernel boundary

        cmpa.l  D1,A1                   * Check USP < 0xCC0000
        bhi.b   SVC_$ILLEGAL_USP_JUMP   * Branch if USP in kernel space

        * Validate and push arg5 (offset 0x14)
        move.l  (0x14,A1),D0            * D0 = arg5
        cmp.l   D0,D1                   * Check arg5 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       * Branch if arg5 >= boundary
        move.l  D0,-(SP)                * Push arg5 to supervisor stack

        * Validate and push arg4 (offset 0x10)
        move.l  (0x10,A1),D0            * D0 = arg4
        cmp.l   D0,D1                   * Check arg4 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       * Branch if arg4 >= boundary
        move.l  D0,-(SP)                * Push arg4

        * Validate and push arg3 (offset 0x0C)
        move.l  (0x0C,A1),D0            * D0 = arg3
        cmp.l   D0,D1                   * Check arg3 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       * Branch if arg3 >= boundary
        move.l  D0,-(SP)                * Push arg3

        * Validate and push arg2 (offset 0x08)
        move.l  (0x08,A1),D0            * D0 = arg2
        cmp.l   D0,D1                   * Check arg2 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       * Branch if arg2 >= boundary
        move.l  D0,-(SP)                * Push arg2

        * Validate and push arg1 (offset 0x04)
        move.l  (0x04,A1),D0            * D0 = arg1
        cmp.l   D0,D1                   * Check arg1 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       * Branch if arg1 >= boundary
        move.l  D0,-(SP)                * Push arg1

        * Call the syscall handler
        jsr     (A0)                    * Call handler(arg1,arg2,arg3,arg4,arg5)

        * Clean up and return to user mode
        adda.w  #$14,SP                 * Remove 5 arguments (20 bytes)
        jmp     FIM_$EXIT               * Return via RTE

*----------------------------------------------------------------------
* SVC_$INVALID_SYSCALL - Invalid syscall number handler
*
* Called when syscall number >= 99.
* Generates a fault with status_$fault_invalid_SVC_code (0x00120007).
*
* From: 0x00e7b28e
*----------------------------------------------------------------------

SVC_$INVALID_SYSCALL:
        lea     SVC_$TRAP5_TABLE_END(PC),A0  * Load return address marker
        cmpa.l  A0,A1                   * Check if we have valid frame
        bne.b   .no_unlink
        unlk    A6                      * Unlink if we had a frame
.no_unlink:
        move.l  #$00120007,D0           * status_$fault_invalid_SVC_code
        bra.b   SVC_$GENERATE_FAULT

*----------------------------------------------------------------------
* SVC_$BAD_USER_PTR - Bad user pointer handler
*
* Called when a syscall argument pointer >= 0xCC0000.
* Generates a fault with status_$fault_protection_boundary_violation.
*
* From: 0x00e7b2a0
*----------------------------------------------------------------------

SVC_$BAD_USER_PTR:
        move.l  #$0012000B,D0           * status_$fault_protection_boundary_violation
        * Fall through to SVC_$GENERATE_FAULT

*----------------------------------------------------------------------
* SVC_$GENERATE_FAULT - Common fault generation code
*
* Input:
*   D0 = fault status code
*
* Sets up fault frame and calls FIM_$GENERATE to deliver the fault.
*----------------------------------------------------------------------

SVC_$GENERATE_FAULT:
        move.w  PROC1_$CURRENT,D1       * Get current process index
        lsl.w   #2,D1                   * D1 = index * 4
        lea     OS_STACK_BASE,A0        * A0 = stack base array
        movea.l (0,A0,D1.w),A1          * A1 = current process stack
        lea     (-0x08,A1),SP           * Set SP for fault frame
        move.l  D0,-(SP)                * Push status code
        jsr     FIM_$GENERATE           * Generate the fault
        unlk    A6                      * Clean up (not reached normally)
        jmp     FIM_$ILLEGAL_USP        * Final handler

*----------------------------------------------------------------------
* SVC_$UNIMPLEMENTED - Unimplemented syscall handler
*
* Called for syscalls that are not yet implemented.
* Generates a fault with status_$fault_unimplemented_SVC (0x0012001c).
*
* From: 0x00e7b2cc
*----------------------------------------------------------------------

SVC_$UNIMPLEMENTED:
        lea     SVC_$TRAP5_TABLE_END(PC),A0  * Load return address marker
        cmpa.l  A0,A1                   * Check frame
        bne.b   .no_unlink2
        unlk    A6
.no_unlink2:
        move.l  #$0012001C,D0           * status_$fault_unimplemented_SVC
        bra.b   SVC_$GENERATE_FAULT

*----------------------------------------------------------------------
* External references
*----------------------------------------------------------------------

        .extern FIM_$EXIT               * Return from exception (RTE)
        .extern FIM_$ILLEGAL_USP        * Illegal USP handler
        .extern FIM_$GENERATE           * Fault generation
        .extern PROC1_$CURRENT          * Current process index
        .extern OS_STACK_BASE           * Process stack base array

*----------------------------------------------------------------------
* SVC_$TRAP5_TABLE - System call handler dispatch table
*
* 99 entries (0x00-0x62), each a 32-bit pointer to the handler.
* Invalid/unimplemented syscalls point to SVC_$INVALID_SYSCALL
* or SVC_$UNIMPLEMENTED.
*
* From: 0x00e7baf2
*----------------------------------------------------------------------

        .data
        .even

SVC_$TRAP5_TABLE:
        .long   SVC_$INVALID_SYSCALL    * 0x00: Reserved
        .long   MST_$MAP_AREA           * 0x01: Map memory area
        .long   SVC_$INVALID_SYSCALL    * 0x02: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x03: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x04: Invalid
        .long   ACL_$RIGHTS             * 0x05: Get ACL rights
        .long   SVC_$INVALID_SYSCALL    * 0x06: Invalid
        .long   ASKNODE_$INFO           * 0x07: Query node info
        .long   DISK_$AS_READ           * 0x08: Async disk read
        .long   DISK_$AS_WRITE          * 0x09: Async disk write
        .long   SVC_$INVALID_SYSCALL    * 0x0A: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x0B: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x0C: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x0D: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x0E: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x0F: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x10: Invalid
        .long   SVC_$UNIMPLEMENTED      * 0x11: Unimplemented
        .long   SVC_$UNIMPLEMENTED      * 0x12: Unimplemented
        .long   SVC_$UNIMPLEMENTED      * 0x13: Unimplemented
        .long   SVC_$UNIMPLEMENTED      * 0x14: Unimplemented
        .long   SVC_$UNIMPLEMENTED      * 0x15: Unimplemented
        .long   TPAD_$INQUIRE           * 0x16: Tablet pad inquire
        .long   TPAD_$SET_MODE          * 0x17: Tablet pad set mode
        .long   VFMT_$MAIN              * 0x18: Volume format
        .long   VOLX_$GET_INFO          * 0x19: Get volume info
        .long   VTOC_$GET_UID           * 0x1A: Get VTOC UID
        .long   NETLOG_$CNTL            * 0x1B: Network log control
        .long   PROC2_$GET_UPIDS        * 0x1C: Get user PIDs
        .long   SVC_$INVALID_SYSCALL    * 0x1D: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x1E: Invalid
        .long   SVC_$UNIMPLEMENTED      * 0x1F: Unimplemented
        .long   SVC_$UNIMPLEMENTED      * 0x20: Unimplemented
        .long   SVC_$UNIMPLEMENTED      * 0x21: Unimplemented
        .long   SVC_$UNIMPLEMENTED      * 0x22: Unimplemented
        .long   MST_$GET_UID_ASID       * 0x23: Get UID/ASID
        .long   MST_$INVALIDATE         * 0x24: Invalidate MST
        .long   FILE_$INVALIDATE        * 0x25: Invalidate file
        .long   SVC_$INVALID_SYSCALL    * 0x26: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x27: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x28: Invalid
        .long   MST_$SET_TOUCH_AHEAD_CNT * 0x29: Set touch ahead
        .long   OS_$CHKSUM              * 0x2A: Checksum
        .long   FILE_$GET_SEG_MAP       * 0x2B: Get segment map
        .long   SVC_$INVALID_SYSCALL    * 0x2C: Invalid
        .long   SVC_$UNIMPLEMENTED      * 0x2D: Unimplemented
        .long   FILE_$UNLOCK_PROC       * 0x2E: Unlock process
        .long   SVC_$UNIMPLEMENTED      * 0x2F: Unimplemented
        .long   SVC_$UNIMPLEMENTED      * 0x30: Unimplemented
        .long   DIR_$ADDU               * 0x31: Add directory (user)
        .long   DIR_$DROPU              * 0x32: Drop directory (user)
        .long   DIR_$CREATE_DIRU        * 0x33: Create directory (user)
        .long   DIR_$ADD_BAKU           * 0x34: Add backup (user)
        .long   SVC_$INVALID_SYSCALL    * 0x35: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x36: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x37: Invalid
        .long   DIR_$ADD_HARD_LINKU     * 0x38: Add hard link (user)
        .long   SVC_$INVALID_SYSCALL    * 0x39: Invalid
        .long   RIP_$UPDATE             * 0x3A: RIP update
        .long   DIR_$DROP_LINKU         * 0x3B: Drop link (user)
        .long   ACL_$CHECK_RIGHTS       * 0x3C: Check ACL rights
        .long   DIR_$DROP_HARD_LINKU    * 0x3D: Drop hard link (user)
        .long   ROUTE_$OUTGOING         * 0x3E: Route outgoing
        .long   SVC_$INVALID_SYSCALL    * 0x3F: Invalid
        .long   SVC_$UNIMPLEMENTED      * 0x40: Unimplemented
        .long   SVC_$INVALID_SYSCALL    * 0x41: Invalid
        .long   NET_$GET_INFO           * 0x42: Get network info
        .long   DIR_$GET_ENTRYU         * 0x43: Get directory entry (user)
        .long   AUDIT_$LOG_EVENT        * 0x44: Audit log event
        .long   FILE_$SET_PROT          * 0x45: Set file protection
        .long   TTY_$K_GET              * 0x46: TTY kernel get
        .long   TTY_$K_PUT              * 0x47: TTY kernel put
        .long   PROC2_$ALIGN_CTL        * 0x48: Alignment control
        .long   SVC_$INVALID_SYSCALL    * 0x49: Invalid
        .long   XPD_$READ_PROC          * 0x4A: Read process
        .long   XPD_$WRITE_PROC         * 0x4B: Write process
        .long   DIR_$SET_DEF_PROTECTION * 0x4C: Set default protection
        .long   DIR_$GET_DEF_PROTECTION * 0x4D: Get default protection
        .long   ACL_$COPY               * 0x4E: Copy ACL
        .long   ACL_$CONVERT_FUNKY_ACL  * 0x4F: Convert funky ACL
        .long   DIR_$SET_PROTECTION     * 0x50: Set protection
        .long   FILE_$OLD_AP            * 0x51: Old access protocol
        .long   ACL_$SET_RE_ALL_SIDS    * 0x52: Set RE all SIDs
        .long   ACL_$GET_RE_ALL_SIDS    * 0x53: Get RE all SIDs
        .long   FILE_$EXPORT_LK         * 0x54: Export lock
        .long   FILE_$CHANGE_LOCK_D     * 0x55: Change lock
        .long   XPD_$READ_PROC_ASYNC    * 0x56: Read process async
        .long   SVC_$INVALID_SYSCALL    * 0x57: Invalid
        .long   SMD_$MAP_DISPLAY_MEMORY * 0x58: Map display memory
        .long   SVC_$INVALID_SYSCALL    * 0x59: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x5A: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x5B: Invalid
        .long   SVC_$INVALID_SYSCALL    * 0x5C: Invalid
        .long   SVC_$UNIMPLEMENTED      * 0x5D: Unimplemented
        .long   SMD_$UNMAP_DISPLAY_MEMORY * 0x5E: Unmap display memory
        .long   SVC_$UNIMPLEMENTED      * 0x5F: Unimplemented
        .long   RIP_$TABLE_D            * 0x60: RIP table
        .long   XNS_ERROR_$SEND         * 0x61: XNS error send
        .long   SVC_$UNIMPLEMENTED      * 0x62: Unimplemented

SVC_$TRAP5_TABLE_END:

        .end
