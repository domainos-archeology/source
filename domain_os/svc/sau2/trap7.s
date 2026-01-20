*
* SVC_$TRAP7 - Domain/OS TRAP #7 Variable-Argument System Call Dispatcher
*
* This is the entry point for variable-argument system calls in Domain/OS.
* Unlike TRAP #1-6 which have fixed argument counts, TRAP #7 uses a lookup
* table to determine how many arguments each syscall expects.
*
* User programs invoke system calls via TRAP #7 with:
*   - D0.w = syscall number (0-55)
*   - Arguments on user stack (variable count per syscall)
*
* From: 0x00e7b240
*
* Memory layout:
*   0x00e7b23c: Branch to bad user pointer handler
*   0x00e7b240: SVC_$TRAP7 main entry
*
* Unique features of TRAP7:
*   1. Variable argument counts per syscall (looked up from SVC_$TRAP7_ARGCOUNT)
*   2. Creates stack frame via LINK instruction
*   3. Uses loop to copy and validate arguments
*   4. Negative argcount values skip argument validation entirely
*
* Address space check:
*   0xCC0000 = boundary between user and kernel space
*   USP and all argument pointers must be < 0xCC0000
*
* Tables:
*   SVC_$TRAP7_ARGCOUNT at 0xe7be4a - byte table of argument counts (56 entries)
*   SVC_$TRAP7_TABLE at 0xe7bd6a - handler addresses (56 entries)
*

        .text
        .even

*----------------------------------------------------------------------
* Branch stub for error handler (precedes main entry)
*----------------------------------------------------------------------

SVC_$TRAP7_BAD_PTR_JUMP:
        bra.w   SVC_$BAD_USER_PTR       * 0xe7b23c: argument pointer >= 0xCC0000

*----------------------------------------------------------------------
* SVC_$TRAP7 - Main system call entry point
*
* Input:
*   D0.w = syscall number (0x00-0x37)
*
* Processing:
*   1. Load argcount table address
*   2. Validate syscall number (< 0x38)
*   3. Create stack frame
*   4. Look up argument count for this syscall
*   5. If argcount negative, skip validation (special handling)
*   6. Validate USP range
*   7. Loop: copy and validate each argument
*   8. Look up handler and call it
*   9. Clean up frame and return via RTE
*----------------------------------------------------------------------

        .global SVC_$TRAP7

SVC_$TRAP7:
        lea     SVC_$TRAP7_ARGCOUNT,A1  * A1 = argcount table (0xe7be4a)
        cmp.w   #$38,D0                 * Check syscall number limit
        bcc.b   SVC_$TRAP7_INVALID      * Branch if D0 >= 56

        link    A6,#0                   * Create stack frame
        clr.w   D1                      * Clear high byte of D1
        move.b  (0,A1,D0.w),D1          * D1 = argcount for this syscall
        bmi.b   SVC_$TRAP7_NO_ARGS      * If negative, skip arg validation

        * Normal path: validate and copy arguments
        move    USP,A1                  * A1 = user stack pointer
        swap    D0                      * Save syscall# in high word
        move.w  D1,D0                   * D0.w = argcount (loop counter)
        lsl.w   #2,D1                   * D1 = argcount * 4 (byte offset)
        lea     (0x8,A1,D1.w),A1        * A1 = USP + argcount*4 + 8 = end of args + 4

        move.l  #$CC0000,D1             * D1 = user/kernel boundary
        cmpa.l  D1,A1                   * Check end of args < boundary
        bhi.b   SVC_$TRAP7_ILLEGAL_USP  * Branch if in kernel space

        * Argument copy loop (copies from highest to lowest address)
SVC_$TRAP7_ARG_LOOP:
        movea.l -(A1),A0                * A0 = next argument (pre-decrement)
        move.l  A0,-(SP)                * Push argument to supervisor stack
        cmp.l   A0,D1                   * Check arg < boundary
        dbls    D0,SVC_$TRAP7_ARG_LOOP  * Loop while valid and count > 0
        bls.b   SVC_$TRAP7_BAD_PTR_JUMP * If last arg invalid, error

        swap    D0                      * Restore syscall# to low word

SVC_$TRAP7_NO_ARGS:
        * Look up and call handler
        lsl.w   #2,D0                   * D0 = syscall_num * 4 (table index)
        lea     SVC_$TRAP7_TABLE,A1     * A1 = handler table (0xe7bd6a)
        movea.l (0,A1,D0.w),A0          * A0 = handler address from table
        jsr     (A0)                    * Call handler

        * Clean up and return to user mode
        unlk    A6                      * Restore frame
        jmp     FIM_$EXIT               * Return via RTE

*----------------------------------------------------------------------
* Error handlers
*----------------------------------------------------------------------

SVC_$TRAP7_INVALID:
        * Check if we're in the right table range (for error reporting)
        lea     SVC_$TRAP7_TABLE,A0     * A0 = handler table base
        cmpa.l  A0,A1                   * Compare with current position
        bne.b   SVC_$TRAP7_INVALID_2    * If not at start, different error
        unlk    A6                      * Clean up frame
SVC_$TRAP7_INVALID_2:
        move.l  #$00120007,D0           * status_$fault_invalid_SVC_code
        bra.b   SVC_$TRAP7_ERROR

SVC_$TRAP7_ILLEGAL_USP:
        unlk    A6                      * Clean up frame
        jmp     FIM_$ILLEGAL_USP        * Jump to illegal USP handler

*----------------------------------------------------------------------
* SVC_$TRAP7_ERROR - Common error handling path
*
* Handles errors by looking up the fault handler from a table and
* transferring control to it.
*----------------------------------------------------------------------

SVC_$TRAP7_ERROR:
        move.w  (PROC1_$FAULT_INDEX),D1 * Get fault handler index
        lsl.w   #2,D1                   * D1 *= 4 (table index)
        lea     (PROC1_$FAULT_TABLE),A0 * A0 = fault handler table
        movea.l (0,A0,D1.w),A1          * A1 = fault handler address
        lea     (-8,A1),SP              * Adjust stack
        move.l  D0,-(SP)                * Push error code
        jsr     FIM_$FAULT              * Call fault handler
        unlk    A6                      * Clean up (may not be reached)
        jmp     FIM_$FAULT_RETURN       * Return from fault

*----------------------------------------------------------------------
* External references
*----------------------------------------------------------------------

        .extern FIM_$EXIT               * Return from exception (RTE)
        .extern FIM_$ILLEGAL_USP        * Illegal USP handler
        .extern FIM_$FAULT              * Fault handler
        .extern FIM_$FAULT_RETURN       * Fault return handler
        .extern SVC_$BAD_USER_PTR       * Bad user pointer handler (in trap5.s)
        .extern SVC_$TRAP7_TABLE        * Handler table (defined in svc_tables.c)
        .extern SVC_$TRAP7_ARGCOUNT     * Argument count table (defined in svc_tables.c)
        .extern PROC1_$FAULT_INDEX      * Current fault handler index
        .extern PROC1_$FAULT_TABLE      * Fault handler table

        .end
