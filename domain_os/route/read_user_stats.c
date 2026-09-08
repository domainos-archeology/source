/*
 * ROUTE_$READ_USER_STATS - Read user-visible routing statistics
 *
 * The USER driver's "get stats" entry (net_io_$driver_t +0x0C).  Looks the
 * port up by socket number, then copies its route_$port_stats_t out: the
 * in-use byte, the two counters at +0x02 and +0x06, and one queue-depth
 * bucket per possible socket queue depth.
 *
 * FIVE argument slots; the third is a word at A6+0x10 that this routine never
 * reads.  See the prototype comment in route/route.h.
 *
 * Address ranges (SR10.2 SAU2 image):
 *   0x00E6A65E-0x00E6A684  prologue and the ROUTE_$FIND_PORT call
 *   0x00E6A686-0x00E6A698  the not-found arm
 *   0x00E6A69A-0x00E6A6C2  port address, stats pointer, the fixed ten bytes
 *   0x00E6A6C4-0x00E6A6DC  the queue-depth bucket copy
 *   0x00E6A6DE-0x00E6A6F6  the byte count and the success status
 *   0x00E6A6F8-0x00E6A700  epilogue
 *
 * Original address: 0x00E6A65E (164 bytes)
 */

#include "route/route_internal.h"
#include "arch/arch.h"

/* The fixed head of a route_$port_stats_t: the flags word plus the two
 * counters, i.e. everything before queue_depth[0]. */
#define STATS_BASE_SIZE     10

_Static_assert(__builtin_offsetof(route_$port_stats_t, queue_depth) ==
                   STATS_BASE_SIZE,
               "the bucket array starts at +0x0A (move.l (0xa,A1),(0xa,A0))");

void ROUTE_$READ_USER_STATS(uint16_t *socket_ptr, uint8_t *stats_buf,
                            uint16_t reserved, int16_t *length_ret,
                            status_$t *status_ret)
{
    int16_t port_index;                 /* D0w */
    route_$port_t *port;                /* D1 */
    route_$port_stats_t *stats;         /* A0, from port->driver_stats */
    int16_t bucket_count;               /* D4w */
    int16_t i;
    uint32_t length_count;              /* the longword read at port+0x34 */

    /* A6+0x10 is an argument slot the image never reads. */
    (void)reserved;

    /*
     * 0x00E6A66E-0x00E6A684.  "clr.l D0 / move.w (A0),D0w" zero-extends the
     * socket word, and the network is the constant 2.  "subq.l #0x2,SP"
     * reserves the callee's word result, which arrives in D0.
     */
    port_index = ROUTE_$FIND_PORT(ROUTE_PORT_TYPE_ROUTING,
                                  (int32_t)(uint32_t)*socket_ptr);

    /* 0x00E6A686-0x00E6A698 */
    if (port_index == -1) {
        *length_ret = 0;
        *status_ret = status_$internet_unknown_network_port;
        return;
    }

    /* 0x00E6A69A-0x00E6A6A8 */
    port = &ROUTE_$PORT_ARRAY[port_index];

    /* 0x00E6A6B0: the stats block is reached through the +0x44 target VA */
    stats = (route_$port_stats_t *)ARCH_VA_TO_PTR(port->driver_stats);

    /*
     * 0x00E6A6B4-0x00E6A6C0: one byte then two longwords.  The image copies
     * the flags word's FIRST BYTE only ("move.b (A0),(A2)"), leaving the
     * caller's second byte untouched, then the two counters at +0x02 and
     * +0x06 with a pair of "(A1)+" moves.
     */
    stats_buf[0] = (uint8_t)(stats->flags >> 8);
    {
        route_$port_stats_t *out = (route_$port_stats_t *)stats_buf;

        out->deep_queue_puts = stats->deep_queue_puts;
        out->failed_puts = stats->failed_puts;
    }

    /*
     * 0x00E6A6C4-0x00E6A6DC: "move.w (0x36,A1),D4w / bmi" - a SIGNED word
     * test, so a negative count copies nothing.  "dbf D0w" with D0 = count
     * runs count+1 times, one longword bucket per pass.
     */
    bucket_count = (int16_t)port->socket2;
    if (bucket_count >= 0) {
        route_$port_stats_t *out = (route_$port_stats_t *)stats_buf;

        /*
         * Both pointers advance by 4 each pass from a base of +0x0A, so pass
         * i copies bucket i.  The image bounds-checks nothing; the count is
         * the port's queue length, which ROUTE_$SERVICE caps at 0x20 and
         * queue_depth[] is sized 0x21 to match.
         */
        for (i = 0; i <= bucket_count; i++) {
            out->queue_depth[i] = stats->queue_depth[i];
        }
    }

    /*
     * 0x00E6A6DE-0x00E6A6F2.  The image computes
     *   (stats_buf + 10) + (longword at port+0x34 + 1) * 4 - stats_buf
     * and stores the low word, i.e. STATS_BASE_SIZE + (count + 1) * 4.  The
     * longword at +0x34 is the same count as the word at +0x36, which is its
     * low half (see route_$port_t).
     */
    length_count = ((uint32_t)port->queue_len_hi << 16) | port->socket2;
    *length_ret = (int16_t)(STATS_BASE_SIZE + (length_count + 1) * 4);

    /* 0x00E6A6F4-0x00E6A6F6 */
    *status_ret = status_$ok;
}
