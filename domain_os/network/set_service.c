/*
 * NETWORK_$SET_SERVICE - Configure network services
 *
 * Sets or modifies the network service configuration. Supports four
 * operations: OR bits, AND NOT bits, SET value, and SET remote pool.
 *
 * Original address: 0x00E0F45E
 *
 * Assembly analysis shows this function:
 * 1. Acquires spin lock at network data base + 0x2A4
 * 2. Switches on operation code (0-3)
 * 3. For ops 0-2, validates against diskless restrictions
 * 4. For op 3, sets remote pool via MMAP_$REMOTE_POOL
 * 5. If services enabled and routing ports exist, sets routing bit
 * 6. Notifies the port-0 driver through route_$driver_info_t.set_service
 *    when ROUTE_$PORT_ARRAY[0].port_type (0xE2E0CE) is still zero
 */

#include "network/network_internal.h"
#include "route/route.h"
#include "arch/arch.h"

void NETWORK_$SET_SERVICE(int16_t *op_ptr, uint32_t *value_ptr, status_$t *status_p)
{
    ml_$spin_token_t token;
    int16_t op;
    uint32_t value;         /* A6-0x98: the argument longword, read once */
    uint16_t new_service;   /* A6-0x94 */

    op = *op_ptr;
    value = *value_ptr;

    token = ML_$SPIN_LOCK(&NETWORK_$LOCK);

    /*
     * 0x00E0F490: cmpi.w #0x4,D0w / bcc -> invalid, then a four-entry word
     * jump table at 0x00E0F4A2.
     *
     * Note which half of `value` each arm uses.  Operations 0..2 read the
     * HIGH word (`move.w (-0x98,A6)`), operation 3 the LOW word
     * (`move.w (-0x96,A6)` at 0x00E0F518): the argument is a two-word record
     * whose first word is the service flags and whose second is the pool
     * size.
     */
    switch (op) {
    case NETWORK_OP_OR_BITS:
        /*
         * 0x00E0F4AA: or.w D0w,(0x342,A5).  No diskless check on this arm.
         */
        new_service = (uint16_t)(NETWORK_$SERVICE_FLAGS | (uint16_t)(value >> 16));
        goto update_service;

    case NETWORK_OP_AND_NOT_BITS:
        /* 0x00E0F4B6: not.w D0w / and.w (0x342,A5),D0w */
        new_service = (uint16_t)(NETWORK_$SERVICE_FLAGS & ~(uint16_t)(value >> 16));
        goto check_diskless;

    case NETWORK_OP_SET_VALUE:
        /* 0x00E0F4CC */
        new_service = (uint16_t)(value >> 16);
        goto check_diskless;

    case NETWORK_OP_SET_REMOTE_POOL:
        /*
         * 0x00E0F506: unlock first, then
         *   move.w (-0x96,A6),D0w ; ext.l D0 ; move.l D0,-(SP)
         *   jsr MMAP_$REMOTE_POOL
         *   move.w D0w,(0x344,A5)
         */
        ML_$SPIN_UNLOCK(&NETWORK_$LOCK, token);
        NETWORK_$SET_REMOTE_POOL(
            (int16_t)MMAP_$REMOTE_POOL((uint16_t)value));
        *status_p = status_$ok;
        return;

    default:
        /* 0x00E0F530 */
        ML_$SPIN_UNLOCK(&NETWORK_$LOCK, token);
        *status_p = status_$network_unknown_request_type;
        return;
    }

check_diskless:
    /*
     * 0x00E0F4C4 / 0x00E0F4D2: tst.b (0x350,A5) / bpl -> store directly.
     * Only when NETWORK_$DISKLESS is true (negative) does the check at
     * 0x00E0F4DC run: not.w D0w / andi.w #0x5,D0w / beq -> store.  Bits 0
     * and 2 are paging and "network active"; a diskless node may not turn
     * either off.
     */
    if ((NETWORK_$DISKLESS < 0) &&
        ((~new_service & (NETWORK_SERVICE_PAGING | NETWORK_SERVICE_ACTIVE)) != 0)) {
        ML_$SPIN_UNLOCK(&NETWORK_$LOCK, token);
        *status_p = status_$network_request_denied_by_local_node;
        return;
    }

    /* 0x00E0F4FE: move.w (-0x94,A6),(0x342,A5) */
    NETWORK_$SET_SERVICE_FLAGS(new_service);
    goto after_store;

update_service:
    NETWORK_$SET_SERVICE_FLAGS(new_service);

after_store:
    /*
     * 0x00E0F54A:
     *   cmpi.w #0x1,(0x00e26f1c).l   ; ROUTE_$N_ROUTING_PORTS
     *   sgt     D0b                  ; 0xFF when there is more than one
     *   or.b    (0x34c,A5),D0b       ; NETWORK_$USER_SOCK_OPEN
     *   bpl     skip                 ; neither -> nothing to do
     *   tst.w   (0x342,A5) ; beq skip
     *   bset.b  #0x3,(0x343,A5)
     */
    if (((ROUTE_$N_ROUTING_PORTS > 1 ? -1 : 0) | NETWORK_$USER_SOCK_OPEN) < 0 &&
        NETWORK_$SERVICE_FLAGS != 0) {
        NETWORK_$SET_SERVICE_FLAGS(NETWORK_$SERVICE_FLAGS | NETWORK_SERVICE_ROUTING);
    }

    ML_$SPIN_UNLOCK(&NETWORK_$LOCK, token);
    *status_p = status_$ok;

    /*
     * 0x00E0F57A: tst.w (0x00e2e0ce).l / bne -> done.
     * 0xE2E0CE is ROUTE_$PORT_ARRAY[0].port_type; the notification below runs
     * only while port 0 still has no type assigned.
     */
    if (ROUTE_$PORT_ARRAY[0].port_type == 0) {
        uint16_t external_service;      /* D0w */
        uint16_t service_rec[2];        /* A6-0x88 */
        int16_t   drv_out4;             /* A6-0x9a, never initialised or read */
        status_$t drv_status;           /* A6-0x90; NETWORK_$SET_SERVICE never
                                         * initialises or reads it, but the
                                         * driver writes its status there -
                                         * see route_$set_service_fn_t
                                         * (source-hi9m) */
        route_$driver_info_t *drv;
        route_$set_service_fn_t set_service_fn;

        /*
         * 0x00E0F582: translate the internal flag byte at A5+0x343 (the low
         * byte of the service word) into the external encoding.  Every test
         * is a btst.b on that byte, so the bit numbers are service-word bits.
         */
        external_service = 0;
        if ((NETWORK_$SERVICE_FLAGS & NETWORK_SERVICE_ACTIVE) != 0) {
            external_service = 0x0B;                    /* moveq #0xb,D0 */
            if ((NETWORK_$SERVICE_FLAGS & NETWORK_SERVICE_PAGING) != 0) {
                external_service = 0x2B;                /* moveq #0x2b,D0 */
            }
            if ((NETWORK_$SERVICE_FLAGS & NETWORK_SERVICE_FILE) != 0) {
                external_service |= 0x10;
            }
            if ((NETWORK_$SERVICE_FLAGS & NETWORK_SERVICE_ROUTING) != 0) {
                external_service |= 0x04;
            }
            if ((NETWORK_$SERVICE_FLAGS & NETWORK_SERVICE_RESERVED_4) != 0) {
                external_service |= 0x80;
            }
        }

        /* 0x00E0F5BC: clr.w (-0x88,A6) / move.w D0w,(-0x86,A6) */
        service_rec[0] = 0;
        service_rec[1] = external_service;

        /*
         * 0x00E0F5C4: move.l (0x00e2e0e8).l,D2 -- that address is
         * ROUTE_$PORT_ARRAY[0].driver_info (+0x48) -- then
         * 0x00E0F5E4: movea.l (0x24,A1),A0 / jsr (A0).
         * The word result is discarded and the arguments are never popped;
         * the unlk at 0x00E0F5F0 drops them.
         */
        drv = (route_$driver_info_t *)ARCH_VA_TO_PTR(ROUTE_$PORT_ARRAY[0].driver_info);
        set_service_fn = (route_$set_service_fn_t)ARCH_VA_TO_PTR(drv->set_service);

        (void)set_service_fn(&ROUTE_$PORT_ARRAY[0].socket, service_rec,
                             0x88, &drv_out4, &drv_status);
    }
}
