/*
 * RIP_$AGE - Age routing table entries
 *
 * Called periodically to age routing table entries.  Routes transition
 * through states based on their expiration times:
 *
 *   VALID -> AGING -> EXPIRED -> UNUSED
 *
 * After aging, routing updates are sent to propagate changes.
 *
 * Original address: 0x00E155C0 (214 bytes)
 */

#include "rip/rip_internal.h"

#include "time/time.h"     /* TIME_$CLOCKH */
/*
 * RIP_$AGE - Age routing table entries
 *
 * 1. Acquire the RIP lock (0x00E155CE bsr.w 0x00E154A4)
 * 2. For each of the 64 entries (0x00E155D2 moveq #0x3f), for each of its two
 *    route slots (0x00E155DC moveq #0x1):
 *      - skip the slot when the state nibble is 0 (unused)
 *      - skip it when TIME_$CLOCKH <= expiration, signed
 *      - state 1 (VALID):   metric != 0 -> re-arm the timer, state := AGING
 *        state 2 (AGING):   metric := infinity, raise the change flag,
 *                           re-arm the timer, state := EXPIRED
 *        anything else:     state := UNUSED
 * 3. Release the lock (0x00E15672 bsr.w 0x00E154C4)
 * 4. RIP_$SEND_UPDATES(false) then RIP_$SEND_UPDATES(true)
 *
 * TWO details the obvious transcription gets wrong, both from bead
 * source-9oyx:
 *
 * SLOT ORDER.  The inner loop's `bne.b` at 0x00E155E2 tests the condition
 * codes left by `clr.w D2w` (0x00E155DE) on the first pass and by
 * `addq.w #0x1,D2w` (0x00E15664) on the second, so the FIRST slot visited is
 * the one at `lea (0x17c,A2)` - entry+0x18, routes[1] - and the SECOND is
 * `lea (0x168,A2)` - entry+0x04, routes[0].  D4 is `st` for the first and
 * `clr.b` for the second, which is what selects std_recent_changes
 * (0x00E15642 `st (0xc86,A5)`) versus recent_changes (0x00E15648
 * `st (0xc88,A5)`).
 *
 * CLOCK RE-READS.  TIME_$CLOCKH is kept in A0 as an ADDRESS
 * (0x00E155D4 `movea.l #0xe2b0d4,A0`) and dereferenced afresh at each use:
 * the comparison at 0x00E15600, the VALID re-arm at 0x00E15620 and the
 * AGING re-arm at 0x00E1564C.  A tick that lands between two of them gives
 * a route an expiry one tick later than the comparison that selected it,
 * and the loop is long enough (128 slots under a spin lock) for that to be
 * observable.  Reproduced by reading the global at each of the three
 * points rather than caching it.
 *
 * The timeout period is RIP_ROUTE_TIMEOUT (0x168 = 360 ticks, about 6
 * minutes).
 */
void RIP_$AGE(void)
{
    int entry_idx;          /* D0: 63 downto 0 */
    int slot;               /* which of the two slots this pass handles */
    rip_$entry_t *entry;    /* A1, stepped by 0x2C at 0x00E1566A */
    rip_$route_t *route;    /* A2/A3 */
    uint8_t state;          /* D5: the flags byte's top two bits */
    boolean is_std;         /* D4 */

    /* 0x00E155CE */
    RIP_$LOCK();

    /* 0x00E155D2-0x00E155DA */
    entry = &RIP_$WIRED_DATA.info[0];
    for (entry_idx = RIP_TABLE_SIZE - 1; entry_idx >= 0; entry_idx--) {

        for (slot = 0; slot < RIP_ROUTES_PER_ENTRY; slot++) {
            /*
             * 0x00E155E2-0x00E155F0.  Pass 0 takes the `lea (0x17c,A2)`
             * arm with D4 true, pass 1 the `lea (0x168,A2)` arm with D4
             * false - routes[1] before routes[0].
             */
            if (slot == 0) {
                is_std = true;                  /* 0x00E155E4 st D4b */
                route  = &entry->routes[1];     /* 0x00E155E6 lea (0x17c,A2) */
            } else {
                is_std = false;                 /* 0x00E155EC clr.b D4b */
                route  = &entry->routes[0];     /* 0x00E155EE lea (0x168,A2) */
            }

            /* 0x00E155F4-0x00E155FE */
            state = (uint8_t)((route->flags & RIP_STATE_MASK) >> RIP_STATE_SHIFT);
            if (state == RIP_STATE_UNUSED) {
                continue;
            }

            /*
             * 0x00E15600-0x00E15604: `move.l (A0),D3 / cmp.l (A2),D3 / ble`
             * - a fresh read of TIME_$CLOCKH, compared signed.
             */
            if ((int32_t)TIME_$CLOCKH <= (int32_t)route->expiration) {
                continue;
            }

            /* 0x00E15606-0x00E15614: the three-way dispatch. */
            if (state == RIP_STATE_VALID) {
                /*
                 * 0x00E15616-0x00E15636.  A metric of 0 is a direct route
                 * and does not age.
                 */
                if (route->metric != 0) {
                    /* 0x00E15620: TIME_$CLOCKH read again. */
                    route->expiration = TIME_$CLOCKH + RIP_ROUTE_TIMEOUT;
                    /* 0x00E1562A andi.b #0x3f / 0x00E15630 ori.b #-0x80 */
                    route->flags = (uint8_t)((route->flags & ~RIP_STATE_MASK) |
                                             (RIP_STATE_AGING << RIP_STATE_SHIFT));
                }
            } else if (state == RIP_STATE_AGING) {
                /* 0x00E15638 */
                route->metric = RIP_INFINITY;

                /* 0x00E1563E-0x00E1564A */
                if (is_std < 0) {
                    RIP_$WIRED_DATA.std_recent_changes = (int8_t)0xFF;   /* 0x00E15642 */
                } else {
                    RIP_$WIRED_DATA.recent_changes = (int8_t)0xFF;       /* 0x00E15648 */
                }

                /* 0x00E1564C: TIME_$CLOCKH read a third time. */
                route->expiration = TIME_$CLOCKH + RIP_ROUTE_TIMEOUT;
                /* 0x00E15656 ori.b #-0x40: 0x80 -> 0xC0, i.e. EXPIRED. */
                route->flags |= RIP_STATE_MASK;
            } else {
                /* 0x00E1565E andi.b #0x3f: back to UNUSED. */
                route->flags &= (uint8_t)~RIP_STATE_MASK;
            }
        }

        entry++;                                /* 0x00E1566A lea (0x2c,A1),A1 */
    }

    /* 0x00E15672 */
    RIP_$UNLOCK();

    /* 0x00E15676-0x00E1568A: `clr.w -(SP)` then `st -(SP)`. */
    RIP_$SEND_UPDATES(false);
    RIP_$SEND_UPDATES(true);
}
