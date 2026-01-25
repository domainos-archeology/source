/*
 * NETWORK_$GET_NET - Get network ID for a network address
 *
 * Looks up the network ID from the network table using the network index
 * encoded in bits 4-9 of the network address.
 *
 * Original address: 0x00E0F2CC
 * Original size: 70 bytes
 *
 * Assembly analysis:
 *   00e0f2cc    link.w A6,0x0
 *   00e0f2d0    pea (A5)
 *   00e0f2d2    lea (0xe248fc).l,A5        ; Network data base
 *   00e0f2d8    movea.l (0xc,A6),A0        ; A0 = net_id_out
 *   00e0f2dc    move.w #0x3f0,D0w          ; Mask for bits 4-9
 *   00e0f2e0    movea.l (0x10,A6),A1       ; A1 = status
 *   00e0f2e4    and.w (0x8,A6),D0w         ; D0 = net_addr & 0x3F0
 *   00e0f2e8    lsr.w #0x4,D0w             ; D0 = index (0-63)
 *   00e0f2ea    bne.b 0x00e0f2f0           ; If index != 0, continue
 *   00e0f2ec    clr.l (A0)                 ; *net_id_out = 0
 *   00e0f2ee    bra.b 0x00e0f2fe           ; Jump to ok status
 *   00e0f2f0    move.w D0w,D1w
 *   00e0f2f2    lsl.w #0x3,D1w             ; D1 = index * 8
 *   00e0f2f4    tst.l (0x3c,A5,D1w*0x1)    ; Test net_id at table[index]
 *   00e0f2f8    beq.b 0x00e0f302           ; If 0, network not found
 *   00e0f2fa    move.l (0x3c,A5,D1w*0x1),(A0) ; *net_id_out = table[index].net_id
 *   00e0f2fe    clr.l (A1)                 ; *status = status_$ok
 *   00e0f300    bra.b 0x00e0f30a
 *   00e0f302    clr.l (A0)                 ; *net_id_out = 0
 *   00e0f304    move.l #0x110017,(A1)      ; *status = status_$network_unknown_network
 *   00e0f30a    movea.l (-0x4,A6),A5
 *   00e0f30e    unlk A6
 *   00e0f310    rts
 *
 * The table is at A5 + 0x38 (refcount) and A5 + 0x3C (net_id):
 *   A5 = 0xE248FC
 *   refcount base = 0xE248FC + 0x38 = 0xE24934
 *   net_id base   = 0xE248FC + 0x3C = 0xE24938
 *
 * Entry layout: 8 bytes per entry (refcount:4, net_id:4)
 */

#include "network/network_internal.h"

void NETWORK_$GET_NET(uint32_t net_addr, uint32_t *net_id_out, status_$t *status)
{
    uint16_t index;

    /* Extract network index from bits 4-9 */
    index = NETWORK_GET_INDEX(net_addr);

    if (index == 0) {
        /* Index 0 is special - represents "no network" or local */
        *net_id_out = 0;
        *status = status_$ok;
        return;
    }

    /* Look up the network ID in the table */
    if (NETWORK_$NET_TABLE[index].net_id == 0) {
        /* Network not found in table */
        *net_id_out = 0;
        *status = status_$network_unknown_network;
        return;
    }

    /* Return the network ID */
    *net_id_out = NETWORK_$NET_TABLE[index].net_id;
    *status = status_$ok;
}
