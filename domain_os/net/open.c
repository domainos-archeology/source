/*
 * NET_$OPEN - Open a network connection
 *
 * Looks up the driver's svc_open entry and calls it.  On success, registers
 * a cleanup handler (bit 10) for the process.
 *
 * Original address: 0x00E5A1A4
 * Original size: 112 bytes
 */

#include "net/net_internal.h"

void NET_$OPEN(int16_t *net_id, int16_t *port, uint32_t param3,
               int16_t *param4, uint32_t param5, status_$t *status_ret)
{
    net_io_$driver_fn_t handler;

    /*
     * 0x00E5A1AC-0x00E5A1D4: find the svc_open slot for this network/port.
     * One word is read out of each of net_id and port.
     */
    handler = NET_$FIND_HANDLER(*net_id, (uint16_t)*port,
                                NET_HANDLER_OFF_OPEN, status_ret);

    /* 0x00E5A1DC tst.l (A3) */
    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * 0x00E5A1E0-0x00E5A1F6: call the driver.  Five arguments plus a
     * discarded word result slot; param4 is dereferenced to one word, the
     * other two are copied through as longwords.
     */
    (void)((net_$svc_ctl_fn_t)handler)(port, param3, *param4, param5,
                                       status_ret);

    /* 0x00E5A1FA tst.l (A3) */
    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * 0x00E5A1FE-0x00E5A204: register the process cleanup handler
     * (bit 10 = NET cleanup).
     */
    PROC2_$SET_CLEANUP(10);
}
