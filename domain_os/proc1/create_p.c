/*
 * PROC1_$CREATE_P - Create a new process
 * Original address: 0x00e15148
 *
 * Creates a new process with the specified entry point and type.
 * Allocates a stack, binds a PCB, sets the type, and resumes the process.
 *
 * Parameters:
 *   funcptr - Entry point function for the new process
 *   type - Packed type value:
 *          Low 16 bits: stack size
 *          High 16 bits: process type (determines working set params)
 *   status_ret - Status return pointer
 *
 * Returns:
 *   Process ID on success, undefined on failure
 */

#include "proc1.h"

/* External functions */
extern void *PROC1_$ALLOC_STACK(int16_t size, status_$t *status_ret);
extern uint16_t PROC1_$BIND(void *funcptr, void *stack1, void *stack2,
                             uint16_t ws_param, status_$t *status_ret);
extern void PROC1_$FREE_STACK(void *stack);
extern void PROC1_$RESUME(uint16_t pid, status_$t *status_ret);

/*
 * Process types and their working set parameters:
 * Type 3, 4, 5, 10: ws_param = 5
 * Type 8: ws_param = 6
 * Others: ws_param = 0
 */

uint16_t PROC1_$CREATE_P(void *funcptr, uint32_t type, status_$t *status_ret)
{
    void *stack;
    uint16_t pid;
    uint16_t stack_size;
    uint16_t proc_type;
    uint16_t ws_param;

    /* Unpack type parameter */
    stack_size = (uint16_t)(type & 0xFFFF);
    proc_type = (uint16_t)(type >> 16);

    /* Allocate stack */
    stack = PROC1_$ALLOC_STACK(stack_size, status_ret);
    pid = proc_type;  /* Default return value on error */

    if ((int16_t)*status_ret != 0) {
        return pid;
    }

    /* Determine working set parameter based on process type */
    switch (proc_type) {
        case 3:
        case 4:
        case 5:
        case 10:
            ws_param = 5;
            break;
        case 8:
            ws_param = 6;
            break;
        default:
            ws_param = 0;
            break;
    }

    /* Bind process to PCB */
    pid = PROC1_$BIND(funcptr, stack, stack, ws_param, status_ret);

    if ((int16_t)*status_ret != 0) {
        /* Bind failed - free the stack */
        PROC1_$FREE_STACK(stack);
        return proc_type;
    }

    /* Set process type in type table */
    PROC1_$TYPE[pid] = proc_type;

    /* Resume the process to start execution */
    PROC1_$RESUME(pid, status_ret);

    return pid;
}
