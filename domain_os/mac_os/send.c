/*
 * MAC_OS_$SEND - Send a packet at OS level
 *
 * Validates the caller's buffer chain, stages it into network header/data
 * buffers when the caller has not pre-built the frame, and hands the finished
 * descriptor to the port driver's send entry.
 *
 * Original address: 0x00E0B5A8, size 620 bytes (0x00E0B5A8-0x00E0B813).
 * A5 = 0x00E22990 (MAC_OS_$DATA).
 *
 * This routine touches no hardware: everything below is either the module's
 * own data, the caller's descriptor, or a call to FIM_$/NETBUF_$/the driver.
 * It therefore builds unchanged on the host (bead source-q6dm; the whole body
 * used to sit under "#if defined(ARCH_M68K)" with a not-implemented stub).
 */

#include "mac_os/mac_os_internal.h"

/*
 * mac_os_$driver_send_fn_t - the port driver entry at driver_info + 0x44.
 *
 * 0x00E0B78A-0x00E0B7AC pushes, right to left:
 *   subq.l #0x2,SP            word result slot; the result is discarded
 *   move.l (0x14,A6),-(SP)    arg 5, status_ret
 *   move.l (0x10,A6),-(SP)    arg 4, bytes_sent
 *   pea    (A4)               arg 3, the packet descriptor
 *   pea    (0x7ac,A3)         arg 2, &chan->callback_data
 *   move.w (0x7ae,A3),-(SP)   arg 1, chan->line_number
 *   ... jsr (A1) / lea (0x14,SP),SP
 */
typedef int16_t (*mac_os_$driver_send_fn_t)(uint16_t line_number,
                                            uint16_t *callback_data,
                                            mac_os_$send_pkt_t *pkt_desc,
                                            int16_t *bytes_sent,
                                            status_$t *status_ret);

/*
 * MAC_OS_$SEND
 *
 * Parameters:
 *   channel    - pointer to the channel number (0..9)
 *   pkt_desc   - the packet descriptor (mac_os_$send_pkt_t)
 *   bytes_sent - word the driver fills in
 *   status_ret - status return
 */
void MAC_OS_$SEND(int16_t *channel, mac_os_$send_pkt_t *pkt_desc,
                  int16_t *bytes_sent, status_$t *status_ret)
{
    mac_os_$channel_t          *chan;
    void                       *driver_info;
    mac_os_$driver_send_fn_t    driver_send;
    status_$t                   cleanup_status;
    uint8_t                     cleanup_info[24];   /* A6-0x18 */

    /*
     * A6-0x44 and A6-0x3e.  The image reads BOTH of these again on the
     * FIM_$CLEANUP unwind path (0x00E0B7E2), where they still hold whatever
     * the aborted first pass left in the frame.  Plain C has no way to model a
     * longjmp-preserved frame, so they start at zero here; the difference only
     * shows when a fault unwinds before 0x00E0B61C ran.
     * TODO: model the FIM_$CLEANUP frame carry-over (bead source-gs4e).
     */
    int8_t      needs_buffers = 0;      /* A6-0x44: ~pkt_desc->hdr_prebuilt */
    int16_t     total_length  = 0;      /* A6-0x3e */

    int8_t      use_header_buf;         /* D3 */
    int8_t      use_data_buf;           /* D4 */
    int16_t     header_length;          /* D2: bytes copied into the header buf */

    uint32_t    header_phys;            /* A6-0x34 */
    uint32_t    header_ptr;             /* A6-0x2c */
    uint32_t    data_ptr;               /* A6-0x28 */
    uint32_t    data_page;              /* A6-0x30 */
    status_$t   buffer_status;          /* A6-0x24 */

    /* The two uplevel slots MAC_OS_$COPY_BUFFER_DATA reads through its
     * static link: A6-0x1c and A6-0x36. */
    mac_os_$buf_desc_t *copy_chain;
    int16_t             copy_offset;

    /* 0x00E0B5B6-0x00E0B5C8 */
    *bytes_sent = 0;
    *status_ret = status_$ok;
    header_ptr  = 0;
    data_ptr    = 0;

    /* 0x00E0B5CA-0x00E0B5E0: chan = A5 + 0x7A0 + 20 * channel */
    chan = &MAC_OS_$CHANNEL_TABLE[*channel];

    /*
     * 0x00E0B5E4-0x00E0B5F6.  The driver record pointer is dereferenced
     * unconditionally; only the send entry itself is tested.
     */
    driver_info = chan->driver_info;
    driver_send = (mac_os_$driver_send_fn_t)
        *(void **)((uint8_t *)driver_info + MAC_OS_DRIVER_SEND_OFFSET);
    if (driver_send == NULL) {
        *status_ret = status_$mac_port_op_not_implemented;
        return;
    }

    /* 0x00E0B5F8-0x00E0B610 */
    cleanup_status = FIM_$CLEANUP(cleanup_info);
    if (cleanup_status != status_$cleanup_handler_set) {
        /*
         * 0x00E0B7E2-0x00E0B80A: the fault unwind.  Note the image never
         * releases the cleanup handler on this path and never pops the
         * NETBUF_$RTN_PKT arguments (unlk restores SP).
         */
        if (needs_buffers < 0) {
            NETBUF_$RTN_PKT(&header_ptr, &data_ptr,
                            &pkt_desc->data_pages[0], total_length);
        }
        *status_ret = cleanup_status;
        return;
    }

    /* 0x00E0B612-0x00E0B624: needs_buffers = ~pkt_desc->hdr_prebuilt */
    needs_buffers = (int8_t)~pkt_desc->hdr_prebuilt;
    if (needs_buffers >= 0) {
        goto do_send;
    }

    /* 0x00E0B628: st D3b / move.b D3b,D4b */
    use_header_buf = -1;
    use_data_buf   = -1;

    /*
     * 0x00E0B62C-0x00E0B672: walk the caller's buffer chain, summing the
     * lengths in a WORD accumulator.  A negative length, or a positive length
     * with a null address, is rejected; so is a total above 0x7B8.
     */
    total_length = 0;
    copy_chain = &pkt_desc->hdr_desc;
    while (copy_chain != NULL) {
        int32_t buf_size = copy_chain->length;

        if (buf_size < 0) {
            goto buffer_error;
        }
        if (buf_size > 0 && copy_chain->address == 0) {
            goto buffer_error;
        }

        total_length = (int16_t)(total_length + (int16_t)buf_size);
        copy_chain = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(copy_chain->next);
    }

    if (total_length > MAC_OS_MAX_PACKET_SIZE) {
buffer_error:
        /* 0x00E0B674-0x00E0B688 */
        FIM_$RLS_CLEANUP(cleanup_info);
        *status_ret = status_$mac_illegal_buffer_spec;
        return;
    }

    /*
     * 0x00E0B68C-0x00E0B6AC: pick the buffers this length needs.
     *   == 0        neither
     *   <= 0x3B8    header buffer only
     *   <= 0x400    data buffer only
     *   >  0x400    both, split at 0x400
     */
    if (total_length == 0) {
        use_header_buf = 0;
        use_data_buf   = 0;
    } else if (total_length <= MAC_OS_SMALL_PACKET_SIZE) {
        use_data_buf = 0;
    } else if (total_length <= MAC_OS_LARGE_PACKET_SIZE) {
        use_header_buf = 0;
    }

    /*
     * 0x00E0B6AE-0x00E0B6B6: reset the two uplevel slots and the header byte
     * count.  copy_chain goes back to &pkt_desc->hdr_desc (A0 still holds it
     * from 0x00E0B62C).
     */
    copy_chain    = &pkt_desc->hdr_desc;
    copy_offset   = 0;
    header_length = 0;

    /* 0x00E0B6B8-0x00E0B6D4: phys first, VA second, then add the MAC header */
    NETBUF_$GET_HDR(&header_phys, &header_ptr);
    header_ptr += (int32_t)(int16_t)chan->header_size;

    if (use_header_buf < 0) {
        if (use_data_buf < 0) {
            /* 0x00E0B6DE: the part above 0x400 goes into the header buffer */
            header_length = (int16_t)(total_length - MAC_OS_LARGE_PACKET_SIZE);
            total_length  = MAC_OS_LARGE_PACKET_SIZE;
        } else {
            /*
             * 0x00E0B6EE: all of it goes into the header buffer.  The length
             * is read into D2 BEFORE the slot is cleared.
             */
            header_length = total_length;
            total_length  = 0;
        }

        /* 0x00E0B6F6-0x00E0B702 */
        MAC_OS_$COPY_BUFFER_DATA(&header_ptr, header_length,
                                 &copy_chain, &copy_offset);
    }

    if (use_data_buf < 0) {
        /* 0x00E0B708-0x00E0B728 */
        NETBUF_$GET_DAT(&data_page);
        NETBUF_$GETVA(data_page, &data_ptr, &buffer_status);

        /* 0x00E0B72C-0x00E0B73C */
        if (buffer_status != status_$ok) {
            *status_ret = buffer_status;
            data_ptr = 0;
            goto send_done;
        }

        /* 0x00E0B742 */
        pkt_desc->data_pages[0] = data_page;

        /* 0x00E0B746-0x00E0B754 */
        MAC_OS_$COPY_BUFFER_DATA(&data_ptr, total_length,
                                 &copy_chain, &copy_offset);

        /* 0x00E0B756-0x00E0B762 */
        NETBUF_$RTNVA(&data_ptr);
        data_ptr = 0;
    } else {
        /* 0x00E0B76C */
        pkt_desc->data_pages[0] = 0;
    }

    /* 0x00E0B770-0x00E0B788 */
    pkt_desc->hdr_desc.length  = (int32_t)header_length;
    pkt_desc->hdr_desc.address = header_ptr;
    pkt_desc->hdr_desc.next    = 0;
    pkt_desc->data_length      = (uint32_t)(int32_t)total_length;

do_send:
    /* 0x00E0B78A-0x00E0B7AC */
    (void)(*driver_send)(chan->line_number, &chan->callback_data,
                         pkt_desc, bytes_sent, status_ret);

send_done:
    /* 0x00E0B7B0-0x00E0B7D2 */
    if (needs_buffers < 0) {
        NETBUF_$RTN_PKT(&header_ptr, &data_ptr,
                        &pkt_desc->data_pages[0], total_length);
    }

    /* 0x00E0B7D6 */
    FIM_$RLS_CLEANUP(cleanup_info);
}
