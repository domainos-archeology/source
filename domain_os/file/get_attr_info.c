/*
 * FILE_$GET_ATTR_INFO - Get file attribute info (compact format)
 *
 * Returns file attributes in a compact 122-byte format (0x7A).
 * Checks lock status if requested via param_2 flags.
 *
 * Original address: 0x00E5D7F4
 */

#include "file/file_internal.h"

/*
 * Compact attribute output structure
 *
 * This is a repackaged version of the full 144-byte attributes
 * into a more compact 122-byte (0x7A) format.
 *
 * The exact layout is complex - this is a best-effort reconstruction
 * based on the decompiled code.
 */
typedef struct compact_attr_t {
    uint32_t flags;          /* 0x00: Repackaged flags */
    uint8_t  data1[24];      /* 0x04: Attribute data from offset 0x04 */
    uint32_t times[6];       /* 0x1C: Time values */
    uint8_t  data2[16];      /* 0x34: More attribute data */
    uint8_t  data3[28];      /* 0x44: Extended attribute data */
    uint8_t  reserved[18];   /* 0x60: Reserved/padding */
    uint8_t  byte_6a;        /* 0x6A: Single byte field */
    uint8_t  padding[3];     /* 0x6B: Padding */
    uint32_t data4[3];       /* 0x6E: Final data fields */
} compact_attr_t;

/*
 * FILE_$GET_ATTR_INFO
 *
 * Gets file attributes in compact format.
 *
 * Parameters:
 *   file_uid   - UID of file
 *   param_2    - Pointer to flags byte
 *   size_ptr   - Pointer to expected buffer size (must be 0x7A = 122)
 *   uid_out    - Output buffer for returned UID (32 bytes = 8 longs)
 *   attr_out   - Output buffer for compact attributes (122 bytes)
 *   status_ret - Receives operation status
 *
 * Flag bits in param_2[1]:
 *   - Bit 0: Check if file is locked
 *   - Bit 1: Check delete status
 *   - Bit 2: Skip delete check
 */
void FILE_$GET_ATTR_INFO(uid_t *file_uid, void *param_2, int16_t *size_ptr,
                         uint32_t *uid_out, void *attr_out, status_$t *status_ret)
{
    status_$t status;
    uid_t local_uid;
    uid_t returned_uid;
    uint16_t flags;
    uint8_t result_buf[2];
    uint8_t *flag_bytes = (uint8_t *)param_2;
    int16_t i;

    /*
     * Full attribute buffer layout:
     * 0x00-0x03: First flags word
     * 0x04-0x17: Data copied to output+0x04 (24 bytes)
     * 0x18-0x4B: More attribute data
     * ...etc
     */
    struct {
        uint32_t flags_0;           /* 0x00 */
        uint8_t  data_04[24];       /* 0x04: Copy to output+0x04 */
        uint32_t time_1c;           /* 0x1C: uStack_78 */
        uint32_t time_20;           /* 0x20: uStack_74 */
        uint32_t time_24;           /* 0x24: local_98 */
        uint16_t time_28;           /* 0x28: local_94 */
        uint16_t pad_2a;            /* 0x2A: padding */
        uint32_t time_2c;           /* 0x2C: local_90 */
        uint16_t time_30;           /* 0x30: local_8c */
        uint16_t pad_32;            /* 0x32 */
        uint16_t val_34;            /* 0x34: local_3e -> output+0x32 */
        uint16_t pad_36;            /* 0x36 */
        uint32_t val_38;            /* 0x38: local_80 -> output+0x36 */
        uint16_t val_3c;            /* 0x3C: local_7c -> output+0x3A */
        uint16_t val_3e;            /* 0x3E: local_40 -> output+0x3C */
        uint8_t  data_40[16];       /* 0x40: -> output+0x3E */
        uint8_t  data_50[28];       /* 0x50: auStack_6c -> output+0x4E */
        uint8_t  byte_6c;           /* 0x6C: local_50 -> output+0x6A */
        uint8_t  flags_6d;          /* 0x6D: local_4f - contains flag bits */
        uint16_t pad_6e;            /* 0x6E */
        uint32_t data_70[3];        /* 0x70: uStack_4c,48,44 -> output+0x6E */
    } full_attrs;

    /* Determine flags based on param_2 */
    if ((flag_bytes[1] & 0x01) != 0) {
        flags = 0x01;
    } else if ((flag_bytes[1] & 0x04) != 0) {
        flags = 0x21;
    } else if ((flag_bytes[1] & 0x02) != 0) {
        int8_t result = FILE_$DELETE_INT((uid_t *)(uid_out + 2), 0, result_buf, &status);
        if (result < 0) {
            flags = 0x01;
        } else {
            flags = 0x21;
        }
    } else {
        *status_ret = file_$invalid_arg;
        return;
    }

    /* Copy the file UID to local storage */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /* Get attributes */
    AST_$GET_ATTRIBUTES(&returned_uid, flags, &full_attrs, &status);

    *status_ret = status;
    if (status != status_$ok) {
        return;
    }

    /* Copy 8 longs of UID to output */
    for (i = 0; i < 8; i++) {
        uid_out[i] = ((uint32_t *)&returned_uid)[i % 2];
    }

    /* Check size - must be 0x7A (122 bytes) */
    if (*size_ptr != FILE_ATTR_INFO_SIZE) {
        *status_ret = file_$invalid_arg;
        return;
    }

    /* Repackage attributes into compact format */
    {
        uint32_t *out = (uint32_t *)attr_out;
        uint8_t *out_bytes = (uint8_t *)attr_out;

        /* Repackage first flags word */
        out[0] = full_attrs.flags_0 & 0x00FF1F06;

        /* Set bit 1 of byte 2 based on bit 7 of flags_6d */
        out_bytes[2] &= 0xFD;
        if (full_attrs.flags_6d & 0x80) {
            out_bytes[2] |= 0x02;
        }

        /* Copy 24 bytes from offset 0x04 */
        for (i = 0; i < 24; i++) {
            out_bytes[4 + i] = full_attrs.data_04[i];
        }

        /* Copy time values */
        out[7] = full_attrs.time_1c;
        out[8] = full_attrs.time_20;
        out[9] = full_attrs.time_24;
        *((uint16_t *)&out[10]) = full_attrs.time_28;
        out[11] = full_attrs.time_2c;
        *((uint16_t *)&out[12]) = full_attrs.time_30;

        /* Copy more values */
        *((uint16_t *)&out_bytes[0x32]) = full_attrs.val_34;
        *((uint32_t *)&out_bytes[0x36]) = full_attrs.val_38;
        *((uint16_t *)&out_bytes[0x3A]) = full_attrs.val_3c;
        *((uint16_t *)&out_bytes[0x3C]) = full_attrs.val_3e;

        /* Copy 16 bytes to offset 0x3E */
        for (i = 0; i < 16; i++) {
            out_bytes[0x3E + i] = full_attrs.data_40[i];
        }

        /* Copy 28 bytes to offset 0x4E */
        for (i = 0; i < 28; i++) {
            out_bytes[0x4E + i] = full_attrs.data_50[i];
        }

        /* Copy single byte to offset 0x6A */
        out_bytes[0x6A] = full_attrs.byte_6c;

        /* Copy 3 longs to offset 0x6E */
        *((uint32_t *)&out_bytes[0x6E]) = full_attrs.data_70[0];
        *((uint32_t *)&out_bytes[0x72]) = full_attrs.data_70[1];
        *((uint32_t *)&out_bytes[0x76]) = full_attrs.data_70[2];

        /* Set additional flag bits based on flags_6d */
        out_bytes[2] &= 0xFE;
        if (full_attrs.flags_6d & 0x20) {
            out_bytes[2] |= 0x01;
        }
        out_bytes[3] &= 0x7F;
        if (full_attrs.flags_6d & 0x10) {
            out_bytes[3] |= 0x80;
        }
    }
}
