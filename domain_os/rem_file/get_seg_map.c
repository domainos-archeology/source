/*
 * REM_FILE_$GET_SEG_MAP - Get segment map from remote file
 *
 * Retrieves the segment allocation map for a file on a remote server.
 * The segment map indicates which segments of the file have data.
 * This is an iterative operation that may require multiple requests.
 *
 * Original address: 0x00E61F3E
 * Size: 348 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Get segment map request structure
 */
typedef struct {
    uint8_t magic;              /* 0x80 */
    uint8_t opcode;             /* 0x1E = Get segment map */
    uid_t file_uid;             /* File UID (8 bytes) */
    uint16_t segment_index;     /* Current segment index */
    uint16_t flags;             /* Flags (value 3) */
    uint8_t force_flag;         /* Force flag (0xFF) */
    uint8_t type_flag;          /* Type flag */
    uint32_t offset;            /* File offset (bytes >> 10) */
    uint32_t magic2;            /* Magic value 0x10020 */
} rem_file_get_seg_map_req_t;

/*
 * Get segment map response structure - small response (0x0C bytes)
 */
typedef struct {
    uint8_t padding[REM_FILE_RESPONSE_BUF_SIZE - 0xBC];
    uint32_t seg_bitmap;        /* Segment bitmap for current segment */
} rem_file_get_seg_map_resp_small_t;

/*
 * Get segment map response structure - full response (0x28 bytes)
 */
typedef struct {
    uint8_t padding[REM_FILE_RESPONSE_BUF_SIZE - 0xB8];
    uint32_t seg_data[1];       /* Start of segment data array */
} rem_file_get_seg_map_resp_full_t;

void REM_FILE_$GET_SEG_MAP(void *addr_info, uid_t *file_uid,
                            uint32_t start_offset, uint32_t end_offset,
                            uint8_t type_flag, uint32_t *seg_map_out,
                            status_$t *status)
{
    rem_file_get_seg_map_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    uint16_t start_seg;
    uint16_t current_seg;
    int i, j;

    /* Calculate starting segment index (offset >> 15) */
    start_seg = (uint16_t)(start_offset >> 15);
    current_seg = start_seg;

    /* Build base request */
    request.magic = 0x80;
    request.opcode = 0x1E;  /* GET_SEG_MAP opcode */
    request.file_uid = *file_uid;
    request.flags = 3;
    request.force_flag = 0xFF;
    request.type_flag = type_flag;
    request.offset = start_offset >> 10;
    request.magic2 = 0x10020;

    while (1) {
        request.segment_index = current_seg;

        /* Send request */
        REM_FILE_$SEND_REQUEST(addr_info, &request, 0x1C,
                               &zero, 0,
                               response, REM_FILE_RESPONSE_BUF_SIZE,
                               &received_len, &zero, 0,
                               (int16_t *)&zero, &packet_id,
                               status);

        if (*status != status_$ok) {
            return;
        }

        if (received_len == 0x28) {
            /* Full response - copy segment data directly */
            rem_file_get_seg_map_resp_full_t *resp = (rem_file_get_seg_map_resp_full_t *)response;
            for (i = 0; i < 1; i++) {
                seg_map_out[i] = resp->seg_data[i];
            }
            return;
        }

        if (received_len == 0x0C) {
            /* Small response - process segment bitmap */
            rem_file_get_seg_map_resp_small_t *resp = (rem_file_get_seg_map_resp_small_t *)response;

            /* Initialize output on first segment */
            if (start_seg == current_seg) {
                for (i = 0; i < 1; i++) {
                    seg_map_out[i] = 0;
                }
            }

            int16_t seg_offset = current_seg - start_seg;

            /* Process bitmap bits */
            if (resp->seg_bitmap != 0) {
                for (j = 31; j >= 0; j--) {
                    if (j < 32 && (resp->seg_bitmap & (1 << j)) != 0) {
                        uint32_t bit_mask = 0;
                        uint16_t bit_pos = 31 - j;
                        if (bit_pos < 32) {
                            /* Set bit in temporary mask */
                            ((uint8_t *)&bit_mask)[bit_pos >> 3] |= (1 << (j & 7));
                        }
                        seg_map_out[seg_offset] |= bit_mask;
                    }
                }
            }

            current_seg++;

            /* Check if we've covered the requested range */
            if ((uint32_t)current_seg * 0x8000 > end_offset) {
                return;
            }
        }

        /* Check if we've gone past start segment */
        if (current_seg > start_seg) {
            return;
        }
    }
}
