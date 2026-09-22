/*
 * PROC2_$STARTUP - First code run by a newly created process
 *
 * Re-emitted from the image (0x00E73454..0x00E73482, 48 bytes).
 *
 * PROC1_$BIND is given the startup_context_t as the initial stack pointer
 * (PROC2_$CREATE 0x00E72898 / PROC2_$FORK), and the record's first
 * longword is the address of its own +0x04 (0x00E7288C `move.l D1,(A0)`
 * with D1 = ctx + 4).  The new process therefore enters here with that
 * longword as its return slot and `(0x8,A6)` = ctx + 4: `context` points
 * at { user_data, entry_point, asid }, and the `move.w (0x8,A2)` at
 * 0x00E73460 reads startup_context_t.asid (+0x0C).  The same pointer is
 * what FIM_$PROC2_STARTUP receives (PROC2_$COMPLETE_VFORK hands it a
 * { user_data, entry_point } pair the same way).
 *
 *   00e7345a  movea.l (0x8,A6),A2
 *   00e73460  move.w (0x8,A2),-(SP) ; PROC1_$SET_ASID   (result slot pushed)
 *   00e7346c  jsr ACL_$CLEAR_SUPER
 *   00e73472  bsr PROC2_$SET_VALID
 *   00e73474  pea (A2) ; jsr FIM_$PROC2_STARTUP
 *
 * References: PROC2_$CREATE 0x00E7289A, PROC2_$FORK 0x00E72D50.
 *
 * Original address: 0x00e73454
 */

#include "proc2/proc2_internal.h"

/* The view of startup_context_t from +0x04 on that this routine is given. */
typedef struct proc2_startup_args_t {
    int32_t     user_data;      /* 0x00: startup_context_t.user_data (+0x04) */
    int32_t     entry_point;    /* 0x04: startup_context_t.entry_point (+0x08) */
    uint16_t    asid;           /* 0x08: startup_context_t.asid (+0x0C) */
} proc2_startup_args_t;

_Static_assert(__builtin_offsetof(proc2_startup_args_t, asid) == 0x08, "proc2_startup_args_t.asid");
_Static_assert(__builtin_offsetof(startup_context_t, asid) -
               __builtin_offsetof(startup_context_t, user_data) == 0x08,
               "startup_context_t.asid sits 8 bytes past user_data");

void PROC2_$STARTUP(void *context)
{
    proc2_startup_args_t *args = (proc2_startup_args_t *)context;   /* A2 */

    PROC1_$SET_ASID(args->asid);        /* 0x00E73460-0x00E7346A */
    ACL_$CLEAR_SUPER();                 /* 0x00E7346C */
    PROC2_$SET_VALID();                 /* 0x00E73472 */
    FIM_$PROC2_STARTUP(context);        /* 0x00E73474-0x00E73476 */
}
