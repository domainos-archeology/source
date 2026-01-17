/*
 * ASKNODE_$WHO - List nodes on network
 *
 * Primary function for listing all nodes on the network.
 * Tries WHO_REMOTE first (using topology information), and falls
 * back to WHO_NOTOPO if that fails with "operation not defined on hardware".
 *
 * Original address: 0x00E65F78
 * Size: 100 bytes
 *
 * Assembly analysis:
 *   link.w   A6,-0x4
 *   movem.l  {A5 A4 A3 A2},-(SP)
 *   lea      (0xe82408).l,A5        ; Load packet info base
 *   movea.l  (0x8,A6),A2            ; param_1 = node_list
 *   movea.l  (0xc,A6),A3            ; param_2 = max_count
 *   movea.l  (0x10,A6),A4           ; param_3 = count
 *   pea      (-0x4,A6)              ; &status
 *   pea      (A4)
 *   pea      (A3)
 *   pea      (A2)
 *   move.l   #0xe2e0a0,-(SP)        ; &ROUTE_$PORT
 *   move.l   #0xe245a4,-(SP)        ; &NODE_$ME
 *   bsr.w    WHO_REMOTE
 *   lea      (0x18,SP),SP
 *   cmpi.l   #0x11001d,(-0x4,A6)    ; status_$network_operation_not_defined_on_hardware
 *   bne.b    done
 *   ; Retry with WHO_NOTOPO
 *   pea      (-0x4,A6)
 *   pea      (A4)
 *   pea      (A3)
 *   pea      (A2)
 *   move.l   #0xe2e0a0,-(SP)
 *   move.l   #0xe245a4,-(SP)
 *   bsr.b    WHO_NOTOPO
 * done:
 *   movem.l  (-0x14,A6),{A2 A3 A4 A5}
 *   unlk     A6
 *   rts
 */

#include "asknode/asknode_internal.h"

void ASKNODE_$WHO(int32_t *node_list, int16_t *max_count, uint16_t *count)
{
    status_$t status;

    /*
     * First try WHO_REMOTE which uses network topology for efficient
     * query routing across multi-hop networks.
     */
    ASKNODE_$WHO_REMOTE(&NODE_$ME, &ROUTE_$PORT, node_list, max_count, count, &status);

    /*
     * If the topology-based query fails because the operation is not
     * supported on the hardware (e.g., simple ring network without
     * topology support), fall back to WHO_NOTOPO which uses broadcast.
     */
    if (status == status_$network_operation_not_defined_on_hardware) {
        ASKNODE_$WHO_NOTOPO(&NODE_$ME, &ROUTE_$PORT, node_list, max_count, count, &status);
    }
}
