/*
 * PROC1_$CREATE_P - Create a level-1 process
 * Original address: 0x00e15148 (182 bytes including the 16-byte jump table)
 *
 * Re-emitted from the disassembly.  A5 = 0x00E254E8 (the PROC1_ data
 * block); the only cell touched is PROC1_$TYPE (0xC42,A5 + pid*2).
 *
 * Frame: (0x8,A6) entry, (0xC,A6) stack size (word), (0xE,A6) process type
 * (word), (0x10,A6) status.  Every caller pushes the two words as one
 * longword (`move.l #0x800000f,-(SP)' at 0x00E2F854: stack 0x800, type
 * 0xF), so the C prototype keeps the packed form: the HIGH word is the
 * stack size and the LOW word is the type.
 * Locals: (-0x4,A6) the stack, (-0x6,A6) the working-set parameter.
 * Registers: D2 = type until a pid exists, then the pid; D3 = pid; A2 =
 * status.
 *
 * 0x00E15148  link.w A6,-0xc / movem.l D2/D3/A2/A5,-(SP) / lea A5
 * 0x00E15156  D2 = type; A2 = status
 * 0x00E1515E  stack = PROC1_$ALLOC_STACK(stack_size, status)   (addq #8)
 * 0x00E15170  tst.w (0x2,A2) / bne 0x00E151F2      low status word != 0: exit
 * 0x00E15176  D0 = type - 3; cmpi.w #8 / bcc 0x00E151AA     (unsigned)
 * 0x00E15180  jmp via the word table at 0x00E1518A:
 *               type 3,4,5 -> 0x00E1519A  ws_param = 5
 *               type 6,7   -> 0x00E151AA  ws_param = 0
 *               type 8     -> 0x00E151A2  ws_param = 6
 *               type 9     -> 0x00E151AA  ws_param = 0
 *               type 10    -> 0x00E1519A  ws_param = 5
 *             image bytes at 0x00E1518A: 0010 0010 0010 0020 0020 0018 0020 0010
 * 0x00E151AE  pid = PROC1_$BIND(entry, stack, stack, ws_param, status)
 *             (`move.l (SP),-(SP)' duplicates the stack; lea (0x14,SP),SP)
 * 0x00E151CA  tst.w (0x2,A2) / beq 0x00E151DA
 * 0x00E151D0  PROC1_$FREE_STACK(stack); bra 0x00E151F2   (no cleanup: unlk)
 * 0x00E151DA  PROC1_$TYPE[pid] = type; D2 = pid
 * 0x00E151E8  PROC1_$RESUME(pid, status)                 (result slot, no cleanup)
 * 0x00E151F2  D0 = D2 / movem.l / unlk / rts
 *
 * Quirks reproduced:
 *   - only the low word of the status is tested after PROC1_$ALLOC_STACK
 *     and PROC1_$BIND;
 *   - on either failure the function returns the TYPE word (D2 has not
 *     been overwritten with the pid yet).
 *
 * Parameters:
 *   entry      - entry point of the new process
 *   type       - stack size in the high word, process type in the low word
 *   status_ret - status return
 *
 * Returns:
 *   the new pid, or the type word when creation failed
 */

#include "proc1/proc1_internal.h"

/* 0x00E1519A / 0x00E151A2: the working-set parameters the table selects */
#define PROC1_WS_PARAM_STD     5
#define PROC1_WS_PARAM_TYPE8   6

uint16_t PROC1_$CREATE_P(void *entry, uint32_t type, status_$t *status_ret)
{
    uint16_t stack_size = (uint16_t)(type >> 16);      /* (0xC,A6) */
    uint16_t proc_type  = (uint16_t)(type & 0xFFFFu);  /* (0xE,A6), D2 */
    uint16_t result;                                   /* D2 */
    uint16_t pid;                                      /* D3 */
    uint16_t ws_param;                                 /* (-0x6,A6) */
    void *stack;                                       /* (-0x4,A6) */

    result = proc_type;

    /* 0x00E1515E: PROC1_$ALLOC_STACK(stack_size, status_ret) */
    stack = PROC1_$ALLOC_STACK(stack_size, status_ret);

    /* 0x00E15170: tst.w (0x2,A2) - low word only */
    if ((*status_ret & 0xFFFF) != 0) {
        return result;
    }

    /* 0x00E15176..0x00E15186: jump table on type - 3, unsigned range 0..7 */
    switch (proc_type) {
    case 3:
    case 4:
    case 5:
    case 10:
        /* 0x00E1519A */
        ws_param = PROC1_WS_PARAM_STD;
        break;
    case 8:
        /* 0x00E151A2 */
        ws_param = PROC1_WS_PARAM_TYPE8;
        break;
    default:
        /* 0x00E151AA: types 6, 7, 9 and everything outside 3..10 */
        ws_param = 0;
        break;
    }

    /* 0x00E151AE..0x00E151C8: PROC1_$BIND(entry, stack, stack, ws_param, status_ret) */
    pid = PROC1_$BIND(entry, stack, stack, ws_param, status_ret);

    /* 0x00E151CA: tst.w (0x2,A2) - low word only */
    if ((*status_ret & 0xFFFF) != 0) {
        /* 0x00E151D0 */
        PROC1_$FREE_STACK(stack);
        return result;
    }

    /* 0x00E151DA..0x00E151E2: PROC1_$TYPE[pid] = type */
    PROC1_$TYPE[pid] = proc_type;

    /* 0x00E151E6 */
    result = pid;

    /* 0x00E151E8: PROC1_$RESUME(pid, status_ret) */
    PROC1_$RESUME(pid, status_ret);

    /* 0x00E151F2 */
    return result;
}
