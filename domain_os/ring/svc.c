/*
 * Ring SVC (Service) Functions
 *
 * This file contains the service call functions for the ring module.
 * These functions implement the user-facing API for ring operations.
 */

#include "ring/ring_internal.h"
#include "fim/fim.h"
#include "netbuf/netbuf.h"
#include "sock/sock.h"
#include "pkt/pkt.h"
#include "misc/crash_system.h"

/*
 * RING_$SVC_OPEN - Open a ring channel (user service call)
 *
 * This is the user-level service call for opening a ring channel.
 * It wraps ring_$open_internal with is_os=0.
 *
 * Original address: 0x00E76DF2
 *
 * @param name          Channel name or identifier
 * @param args          Open arguments
 * @param unused        Unused
 * @param status_ret    Output: status code
 */
void RING_$SVC_OPEN(void *name, void *args, void *unused, status_$t *status_ret)
{
    status_$t status;

    (void)unused;

    ring_$open_internal(0, name, args, &status);
    *status_ret = status;
}

/*
 * RING_$OPEN_OS - Open ring for OS use
 *
 * This is the OS-level service call for opening a ring channel.
 * Copies arguments and calls ring_$open_internal with is_os=-1.
 *
 * Original address: 0x00E77BA0
 *
 * @param param1        First parameter (unit/channel related)
 * @param args          Open arguments structure
 * @param status_ret    Output: status code
 */
void RING_$OPEN_OS(uint16_t param1, void *args, status_$t *status_ret)
{
    status_$t local_status;
    uint16_t local_args[3];
    int16_t arg_count;
    int16_t i;
    uint32_t *args_ptr = (uint32_t *)args;
    uint32_t *src;
    uint8_t *dest;

    /* Check argument count */
    arg_count = *((int16_t *)args + 0x2A);  /* Offset 0x15 words = 0x2A bytes */
    if (arg_count >= 0x11) {
        *status_ret = status_$ring_too_many_args;
        return;
    }

    /* Setup local args */
    local_args[0] = 1;
    local_args[1] = arg_count;

    /* Copy arguments */
    src = args_ptr;
    dest = (uint8_t *)&local_args[2];
    for (i = 0; i < arg_count; i++) {
        ((uint32_t *)dest)[0] = src[0];
        ((uint32_t *)dest)[1] = src[1];
        dest += 8;
        src += 2;
    }

    /* Call internal open with OS flag */
    ring_$open_internal(-1, &param1, local_args, &local_status);
    *status_ret = local_status;

    /* Copy result back */
    *((uint16_t *)args + 2) = local_args[2];
}

/*
 * RING_$SVC_CLOSE - Close a ring channel
 *
 * Closes the specified ring channel.
 *
 * Original address: 0x00E76E22
 *
 * @param unit_ptr      Pointer to unit number
 * @param args          Close arguments
 * @param status_ret    Output: status code
 */
void RING_$SVC_CLOSE(uint16_t *unit_ptr, void *args, status_$t *status_ret)
{
    uint16_t unit;
    ring_unit_t *unit_data;
    uint16_t channel;

    unit = *unit_ptr;

    /* Validate unit number */
    if (unit >= RING_MAX_UNITS) {
        *status_ret = status_$ring_invalid_unit_num;
        return;
    }

    unit_data = &RING_$DATA.units[unit];

    /* Get channel number from args */
    channel = *((uint16_t *)args + 2);

    /* Validate channel number */
    if (channel == 0 || channel > RING_MAX_CHANNELS) {
        *status_ret = status_$ring_channel_not_open;
        return;
    }

    /* Check if channel is open */
    if (unit_data->channels[channel - 1].flags >= 0) {
        *status_ret = status_$ring_channel_not_open;
        return;
    }

    /* Close the channel */
    unit_data->channels[channel - 1].flags = 0;
    unit_data->channels[channel - 1].socket_id = 0;

    *status_ret = status_$ok;
}

/*
 * RING_$SVC_READ - Read data from a ring channel
 *
 * Reads data from the specified ring channel into the provided buffer.
 * Uses FIM cleanup frames for fault isolation.
 *
 * Original address: 0x00E77402
 *
 * @param unit_ptr      Pointer to unit number
 * @param result        Result buffer (read information)
 * @param param3        I/O vectors
 * @param param4        I/O vector parameter
 * @param param5        I/O vector count
 * @param len_out       Output: bytes actually read
 * @param status_ret    Output: status code
 */
void RING_$SVC_READ(uint16_t *unit_ptr, void *result, void *param3,
                    void *param4, uint16_t param5, int16_t *len_out,
                    status_$t *status_ret)
{
    uint16_t unit;
    ring_unit_t *unit_data;
    uint16_t channel;
    status_$t fim_status;
    uint8_t fim_cleanup[24];
    int32_t hdr_buf = 0;
    int32_t data_buf = 0;
    int32_t data_va[2] = {0, 0};
    int16_t hdr_len;
    int16_t data_len;
    int32_t sock_info[10];
    uint16_t offset;
    uint16_t count;
    uint32_t *result_ptr = (uint32_t *)result;

    (void)param4;  /* Unused in simplified implementation */
    hdr_buf = 0;
    data_buf = 0;
    data_va[0] = 0;
    *len_out = 0;
    unit = *unit_ptr;

    /* Get channel number from result buffer */
    channel = *((uint16_t *)result + 2);

    /* Validate unit number */
    if (unit >= RING_MAX_UNITS) {
        *status_ret = status_$ring_invalid_unit_num;
        return;
    }

    /* Setup FIM cleanup frame */
    fim_status = FIM_$CLEANUP(fim_cleanup);
    if (fim_status != status_$cleanup_handler_set) {
        *status_ret = fim_status;
        return;
    }

    unit_data = &RING_$DATA.units[unit];

    /* Check if unit is started and running */
    if (((unit_data->state_flags & RING_UNIT_STARTED) == 0) ||
        ((unit_data->state_flags & RING_UNIT_RUNNING) == 0)) {
        FIM_$RLS_CLEANUP(fim_cleanup);
        *status_ret = status_$ring_device_offline;
        return;
    }

    /* Validate channel number */
    if (channel == 0 || channel > RING_MAX_CHANNELS) {
        FIM_$RLS_CLEANUP(fim_cleanup);
        *status_ret = status_$ring_channel_not_open;
        return;
    }

    /* Check if channel is open */
    if (unit_data->channels[channel - 1].flags >= 0) {
        FIM_$RLS_CLEANUP(fim_cleanup);
        *status_ret = status_$ring_channel_not_open;
        return;
    }

    /*
     * Main read loop - get packets from socket until we have enough data.
     */
    while (1) {
        /* Try to get data from socket */
        SOCK_$GET(unit_data->channels[channel - 1].socket_id, sock_info);

        /* Check if we got valid data (sock_info[0] would be header pointer) */
        if (sock_info[0] == 0) {
            /* No data available */
            FIM_$RLS_CLEANUP(fim_cleanup);
            *status_ret = status_$ring_socket_already_open;
            return;
        }

        /* Got packet - extract header info */
        hdr_buf = sock_info[0];
        hdr_len = *((int16_t *)&sock_info[4] + 1);
        data_buf = sock_info[5];

        /* Check for oversized data */
        if (hdr_len > 0x400) {
            /* Data too large - return buffer and continue */
            NETBUF_$RTN_HDR((uint32_t *)&hdr_buf);
            hdr_buf = 0;
            PKT_$DUMP_DATA((uint32_t *)&data_buf, hdr_len);
            data_buf = 0;
            continue;
        }

        /* Check minimum header length */
        data_len = *((int16_t *)&sock_info[4]);
        if (data_len <= 0x1B) {
            /* Header too short - return buffer and continue */
            NETBUF_$RTN_HDR((uint32_t *)&hdr_buf);
            hdr_buf = 0;
            PKT_$DUMP_DATA((uint32_t *)&data_buf, hdr_len);
            data_buf = 0;
            continue;
        }

        /* Copy header fields to result */
        result_ptr[3] = *((uint32_t *)(hdr_buf + 8));
        *((int8_t *)result + 8) = -(*((int8_t *)(hdr_buf + 4)) < 0);
        result_ptr[0] = *((uint32_t *)(hdr_buf + 0x18));

        /* Adjust data length for header overhead */
        data_len -= 0x1C;
        *((int16_t *)result + 3) = data_len;
        *len_out = hdr_len + data_len;

        /* Copy header data to user buffer */
        offset = 1;
        count = 0;
        int32_t src_ptr = hdr_buf + 0x1C;
        ring_$copy_to_user((void **)&src_ptr, data_len, param3, param5,
                          &offset, &count);

        /* Return header buffer */
        NETBUF_$RTN_HDR((uint32_t *)&hdr_buf);
        hdr_buf = 0;

        /* Copy data if present */
        if (data_buf != 0) {
            NETBUF_$GETVA(data_buf, data_va, status_ret);
            if (*status_ret != status_$ok) {
                CRASH_SYSTEM(status_ret);
            }

            ring_$copy_to_user((void **)&data_va[0], hdr_len, param3, param5,
                              &offset, &count);

            NETBUF_$RTNVA((uint32_t *)data_va);
            NETBUF_$RTN_DAT(data_va[0]);
            data_va[0] = 0;
            data_buf = 0;
        }

        /* Success */
        FIM_$RLS_CLEANUP(fim_cleanup);
        *status_ret = status_$ok;
        return;
    }
}

/*
 * RING_$SVC_WRITE - Write data to a ring channel
 *
 * Writes data to the specified ring channel from the provided buffer.
 * Uses FIM cleanup frames for fault isolation.
 *
 * Original address: 0x00E76F9E
 *
 * @param unit_ptr      Pointer to unit number
 * @param hdr           Packet header info
 * @param param3        I/O vectors
 * @param data          Data buffer / I/O vector parameter
 * @param data_len      Data length / I/O vector count
 * @param result        Output: result flags
 * @param status_ret    Output: status code
 */
void RING_$SVC_WRITE(uint16_t *unit_ptr, void *hdr, void *param3,
                     void *data, int16_t data_len, uint16_t *result,
                     status_$t *status_ret)
{
    uint16_t unit;
    ring_unit_t *unit_data;
    uint16_t channel;
    status_$t fim_status;
    uint8_t fim_cleanup[24];
    void *hdr_buf = NULL;
    int32_t data_buf = 0;
    int32_t data_pa = 0;
    uint16_t total_len = 0;
    uint16_t hdr_len;
    int16_t pkt_type_idx;
    int16_t i;
    int32_t *hdr_ptr = (int32_t *)hdr;

    (void)param3;  /* Used in full implementation */
    *result = 0;
    unit = *unit_ptr;

    /* Validate unit number */
    if (unit >= RING_MAX_UNITS) {
        *status_ret = status_$ring_invalid_unit_num;
        return;
    }

    /* Setup FIM cleanup frame */
    fim_status = FIM_$CLEANUP(fim_cleanup);
    if (fim_status != status_$cleanup_handler_set) {
        /* Cleanup any allocated buffers */
        if (hdr_buf != NULL) {
            NETBUF_$RTN_HDR((uint32_t *)&hdr_buf);
        }
        if (data_pa != 0) {
            NETBUF_$RTNVA((uint32_t *)&data_pa);
        }
        if (data_buf != 0) {
            NETBUF_$RTN_DAT(data_buf);
        }
        *status_ret = fim_status;
        return;
    }

    unit_data = &RING_$DATA.units[unit];

    /* Get channel number from hdr */
    channel = *((uint16_t *)hdr + 2);

    /* Check if unit is started and running */
    if (((unit_data->state_flags & RING_UNIT_STARTED) == 0) ||
        ((unit_data->state_flags & RING_UNIT_RUNNING) == 0)) {
        FIM_$RLS_CLEANUP(fim_cleanup);
        *status_ret = status_$ring_device_offline;
        return;
    }

    /* Validate channel number and check if open */
    if (channel == 0 || channel > RING_MAX_CHANNELS ||
        unit_data->channels[channel - 1].flags >= 0) {
        FIM_$RLS_CLEANUP(fim_cleanup);
        *status_ret = status_$ring_channel_not_open;
        return;
    }

    /* Calculate total data length from I/O vectors */
    if (data_len > 0) {
        uint8_t *iov_ptr = (uint8_t *)data;
        for (i = 0; i < data_len; i++) {
            total_len += *((int16_t *)(iov_ptr + 4));
            iov_ptr += 8;
        }
    }

    /* Validate header length */
    hdr_len = *((uint16_t *)hdr + 3);
    if (hdr_len > 0x3C8) {
        FIM_$RLS_CLEANUP(fim_cleanup);
        *status_ret = status_$ring_illegal_header_length;
        return;
    }

    /* Find packet type in table */
    pkt_type_idx = ring_$find_pkt_type(hdr_ptr[0],
                                       unit_data->pkt_type_table,
                                       unit_data->something);

    if (pkt_type_idx == 0) {
        FIM_$RLS_CLEANUP(fim_cleanup);
        *status_ret = status_$ring_invalid_svc_packet_type;
        return;
    }

    /*
     * TODO: Complete implementation
     *
     * The full implementation would:
     * 1. Allocate header buffer via NETBUF_$GET_HDR
     * 2. Copy header data from hdr
     * 3. If data present, allocate data buffer via NETBUF_$GET_DAT
     * 4. Copy data from iovecs
     * 5. Call RING_$SENDP to transmit
     * 6. Return buffers
     */

    FIM_$RLS_CLEANUP(fim_cleanup);
    *status_ret = status_$ok;
    *result = total_len;
}
