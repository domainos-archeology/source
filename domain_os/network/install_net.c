/*
 * NETWORK_$INSTALL_NET - Install a network in the network table
 *
 * Registers a network ID in the network table. If the network already
 * exists, increments its reference count. Otherwise, allocates a new
 * slot and stores the network ID with refcount 1.
 *
 * The network index (1-64) is encoded into bits 4-9 of the info word.
 *
 * Original address: 0x00E0F1E0
 * Original size: 156 bytes
 *
 * Assembly analysis:
 *   00e0f1e0    link.w A6,-0x8
 *   00e0f1e4    movem.l {A5 A2 D4 D3 D2},-(SP)
 *   00e0f1e8    lea (0xe248fc).l,A5        ; Network data base
 *   00e0f1ee    move.l (0x8,A6),D0         ; D0 = net_id
 *   00e0f1f2    movea.l (0xc,A6),A0        ; A0 = info_ptr
 *   00e0f1f6    movea.l (0x10,A6),A1       ; A1 = status
 *   00e0f1fa    bne.b 0x00e0f202           ; If net_id != 0, continue
 *   00e0f1fc    andi.w #-0x3f1,(A0)        ; Clear bits 4-9 (0xFC0F = ~0x3F0)
 *   00e0f200    bra.b 0x00e0f264           ; Jump to ok status
 *
 *   ; Search loop: D3=index (1-64), D2=counter (63 down to 0), D1=first_free
 *   00e0f202    clr.w D1w                  ; first_free = 0
 *   00e0f204    moveq #0x3f,D2             ; counter = 63
 *   00e0f206    moveq #0x1,D3              ; index = 1
 *   00e0f208    lea (0x8,A5),A2            ; A2 = base for iteration
 *
 *   ; Check if net_id matches table[index]
 *   00e0f20c    cmp.l (0x3c,A2),D0         ; Compare with net_id at A2+0x3C
 *   00e0f210    bne.b 0x00e0f230           ; If not equal, check for free slot
 *
 *   ; Found match - increment refcount and encode index
 *   00e0f212    clr.w D4w
 *   00e0f214    move.b D3b,D4b             ; D4 = index (low byte)
 *   00e0f216    andi.w #-0x3f1,(A0)        ; Clear bits 4-9
 *   00e0f21a    lsl.w #0x4,D4w             ; D4 = index << 4
 *   00e0f21c    or.w D4w,(A0)              ; Encode index into info
 *   00e0f21e    clr.l D0
 *   00e0f220    move.w D3w,D0w
 *   00e0f222    lsl.l #0x3,D0              ; D0 = index * 8
 *   00e0f224    move.l (0x38,A5,D0*0x1),D4 ; D4 = refcount
 *   00e0f228    addq.l #0x1,D4             ; D4++
 *   00e0f22a    move.l D4,(0x38,A5,D0*0x1) ; Store incremented refcount
 *   00e0f22e    bra.b 0x00e0f264           ; Jump to ok status
 *
 *   ; Check for free slot
 *   00e0f230    tst.l (0x3c,A2)            ; Is net_id == 0?
 *   00e0f234    bne.b 0x00e0f23c           ; If not, skip
 *   00e0f236    tst.w D1w                  ; Is first_free already set?
 *   00e0f238    bne.b 0x00e0f23c           ; If yes, skip
 *   00e0f23a    move.w D3w,D1w             ; first_free = index
 *
 *   ; Advance to next entry
 *   00e0f23c    addq.w #0x1,D3w            ; index++
 *   00e0f23e    addq.l #0x8,A2             ; A2 += 8
 *   00e0f240    dbf D2w,0x00e0f20c         ; Loop while counter >= 0
 *
 *   ; No match found - check if we have a free slot
 *   00e0f244    tst.w D1w
 *   00e0f246    beq.b 0x00e0f268           ; If no free slot, error
 *
 *   ; Allocate new entry in free slot
 *   00e0f248    clr.w D4w
 *   00e0f24a    move.b D1b,D4b             ; D4 = first_free (low byte)
 *   00e0f24c    andi.w #-0x3f1,(A0)        ; Clear bits 4-9
 *   00e0f250    lsl.w #0x4,D4w             ; D4 = first_free << 4
 *   00e0f252    or.w D4w,(A0)              ; Encode index into info
 *   00e0f254    clr.l D2
 *   00e0f256    move.w D1w,D2w
 *   00e0f258    lsl.l #0x3,D2              ; D2 = first_free * 8
 *   00e0f25a    move.l D0,(0x3c,A5,D2*0x1) ; Store net_id
 *   00e0f25e    moveq #0x1,D4
 *   00e0f260    move.l D4,(0x38,A5,D2*0x1) ; refcount = 1
 *
 *   ; Return status_$ok
 *   00e0f264    clr.l (A1)
 *   00e0f266    bra.b 0x00e0f272
 *
 *   ; Error: too many networks
 *   00e0f268    andi.w #-0x3f1,(A0)        ; Clear bits 4-9
 *   00e0f26c    move.l #0x110018,(A1)      ; status_$network_too_many_networks
 *
 *   00e0f272    movem.l (-0x1c,A6),{D2 D3 D4 A2 A5}
 *   00e0f278    unlk A6
 *   00e0f27a    rts
 */

#include "network/network_internal.h"

/*
 * The `info` argument is the address of a LONGWORD whose most significant
 * half holds the network-info word: 0x00E0F1F2 loads it with
 * `movea.l (0xc,A6),A0` and then reaches it with word operations
 * (`andi.w #-0x3f1,(A0)` / `or.w D4w,(A0)`), and both callers hand it the
 * address of a longword field - AST_$LOAD_AOTE passes &aote->vol_uid
 * (0x00E01D0E passes the same cell in AST_$LOOKUP_WITH_HINTS, which then
 * does `andi.l #-0x100000,(A0)` on it).  Working on the whole longword with
 * a shifted mask keeps the behaviour identical on a little-endian host.
 */
#define NETWORK_INDEX_MASK_L  ((uint32_t)NETWORK_INDEX_MASK << 16)
#define NETWORK_INDEX_SHIFT_L (NETWORK_INDEX_SHIFT + 16)

void NETWORK_$INSTALL_NET(uint32_t net_id, uint32_t *info_ptr, status_$t *status)
{
    uint16_t first_free;
    uint16_t i;

    /* Special case: net_id == 0 means "no network" / local */
    if (net_id == 0) {
        /* Clear network index bits (4-9) */
        *info_ptr = *info_ptr & ~NETWORK_INDEX_MASK_L;
        *status = status_$ok;
        return;
    }

    /* Search for existing entry or first free slot */
    first_free = 0;

    /*
     * 0x00E0F204-0x00E0F240: "moveq #0x3f,D2" plus "dbf D2w" is 64 passes and
     * the index D3 starts at 1, so the slots visited are 1..64 - the tree used
     * to stop at 63 (bead source-ovis).
     */
    for (i = 1; i <= NETWORK_MAX_NET_INDEX; i++) {
        /* Check if this entry matches */
        if (NETWORK_$NET_TABLE[i].net_id == net_id) {
            /* Found matching entry - encode index and increment refcount */
            /* 0x00E0F212: the index is taken as a byte, then shifted */
            *info_ptr = (*info_ptr & ~NETWORK_INDEX_MASK_L) |
                        ((uint32_t)(uint8_t)i << NETWORK_INDEX_SHIFT_L);
            NETWORK_$NET_TABLE[i].refcount++;
            *status = status_$ok;
            return;
        }

        /* Track first free slot */
        if (NETWORK_$NET_TABLE[i].net_id == 0 && first_free == 0) {
            first_free = i;
        }
    }

    /* No matching entry found */
    if (first_free == 0) {
        /* No free slots available */
        *info_ptr = *info_ptr & ~NETWORK_INDEX_MASK_L;
        *status = status_$network_too_many_networks_in_internet;
        return;
    }

    /*
     * Allocate a new entry in the first free slot.  0x00E0F248 takes only the
     * LOW BYTE of the index ("move.b D1b,D4b") before shifting it left four
     * places, so index 64 sets bit 10 of the info word rather than staying
     * inside the 0x3F0 field; that overflow is reproduced, not corrected.
     */
    *info_ptr = (*info_ptr & ~NETWORK_INDEX_MASK_L) |
                ((uint32_t)(uint8_t)first_free << NETWORK_INDEX_SHIFT_L);
    NETWORK_$NET_TABLE[first_free].net_id = net_id;
    NETWORK_$NET_TABLE[first_free].refcount = 1;
    *status = status_$ok;
}
