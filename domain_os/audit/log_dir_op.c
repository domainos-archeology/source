/*
 * AUDIT_$LOG_DIR_OP - Log directory operation audit event (0x00E4BE16, 172 bytes)
 *
 * Builds the class-4 event header and a {dir_uid, file_uid, name} data block
 * and hands them to AUDIT_$LOG_EVENT.  Called once, from DIR_$DO_OP's add/drop
 * entry path (0x00E4C68C).  The SAU2 map has no symbol at this address (the
 * segment that holds it exports only DIR_$DO_OP at 0xE4C02C), so the name is
 * a tree name.
 *
 * Frame (A6+), verified against the prologue at 0x00E4BE1E-0x00E4BE22 and the
 * later reads:
 *   0x08 audit_type  word   (`move.w (0x8,A6),D0w`)
 *   0x0A status      long   by value; its ADDRESS is what goes to
 *                            AUDIT_$LOG_EVENT (`pea (0xa,A6)` 0x00E4BEA6)
 *   0x0E dir_uid     long   pointer
 *   0x12 file_uid    long   pointer
 *   0x16 name_len    word   (`move.w (0x16,A6),D1w`), compared SIGNED
 *   0x18 name        long   pointer
 *
 * Locals (A6-):
 *   -0x010  event header {word 4, word audit_type, long 0}
 *   -0x120  event data   {dir_uid, file_uid, name[256]}   (name at -0x110)
 *   -0x12A  data_len     word
 *   -0x12C  success_flag word
 */

#include "audit/audit_internal.h"
#include "audit/audit.h"
#include "os/os.h"

/* Event header at A6-0x10: 0x00E4BE26 `move.w #0x4,(-0x10,A6)`,
 * 0x00E4BE2C `move.w D0w,(-0xe,A6)`, 0x00E4BE30 `clr.l (-0xc,A6)`. */
typedef struct audit_dir_op_header_t {
    uint16_t event_class;   /* 0x00: always 4 */
    uint16_t audit_subtype; /* 0x02: audit_type parameter */
    uint32_t reserved;      /* 0x04: always 0 */
} audit_dir_op_header_t;

_Static_assert(sizeof(audit_dir_op_header_t) == 8, "audit_dir_op_header_t size");

/* Event data at A6-0x120: two UIDs then the name; the name area runs from
 * A6-0x110 up to the header at A6-0x10, i.e. 256 bytes. */
typedef struct audit_dir_op_data_t {
    uid_t    dir_uid;       /* 0x00: 0x00E4BE46/0x00E4BE4A */
    uid_t    file_uid;      /* 0x08: 0x00E4BE52/0x00E4BE56 */
    char     name[256];     /* 0x10: entry name, NUL-terminated at name[len] */
} audit_dir_op_data_t;

_Static_assert(__builtin_offsetof(audit_dir_op_data_t, file_uid) == 0x08, "audit_dir_op_data_t.file_uid");
_Static_assert(__builtin_offsetof(audit_dir_op_data_t, name)     == 0x10, "audit_dir_op_data_t.name");
_Static_assert(sizeof(audit_dir_op_data_t) == 0x110, "audit_dir_op_data_t size");

void AUDIT_$LOG_DIR_OP(uint16_t audit_type, status_$t status, uid_t *dir_uid,
                       uid_t *file_uid, uint16_t name_len, void *name)
{
    audit_dir_op_header_t event_header;     /* A6-0x10  */
    audit_dir_op_data_t   event_data;       /* A6-0x120 */
    uint16_t              data_len;         /* A6-0x12A */
    uint16_t              success_flag;     /* A6-0x12C */
    uint32_t              len;              /* D2/D3 */

    /* 0x00E4BE26-0x00E4BE30: the event header. */
    event_header.event_class   = 4;
    event_header.audit_subtype = audit_type;
    event_header.reserved      = 0;

    /*
     * 0x00E4BE34-0x00E4BE3A: `clr.w D2w / cmp.w D1w,D2w / bge` - a signed word
     * compare, so a negative length becomes 0.  0x00E4BE3C-0x00E4BE44 then
     * zero-extends the word into a longword (`clr.l D3 / move.w D2w,D3w`).
     */
    len = 0;
    if ((int16_t)name_len > 0) {
        len = name_len;
    }

    /* 0x00E4BE3E-0x00E4BE56: copy both UIDs as longword pairs. */
    event_data.dir_uid.high  = dir_uid->high;
    event_data.dir_uid.low   = dir_uid->low;
    event_data.file_uid.high = file_uid->high;
    event_data.file_uid.low  = file_uid->low;

    /* 0x00E4BE5A-0x00E4BE6E: `tst.l D2 / beq`, then
     * OS_$DATA_COPY(name, &event_data.name, len) with the LONGWORD count
     * (`move.l D2,-(SP)` at 0x00E4BE5E). */
    if (len != 0) {
        OS_$DATA_COPY(name, event_data.name, len);
    }

    /* 0x00E4BE72-0x00E4BE76: NUL terminator at name[len]. */
    event_data.name[len] = '\0';

    /* 0x00E4BE7A-0x00E4BE88: data_len = (&name[len] - &event_data) + 1, i.e.
     * 0x10 + len + 1, stored as a word. */
    data_len = (uint16_t)((uint32_t)(0x10 + len) + 1);

    /* 0x00E4BE8C-0x00E4BE98: `tst.l (0xa,A6)` - 0 for status_$ok, else 1. */
    if (status == status_$ok) {
        success_flag = 0;
    } else {
        success_flag = 1;
    }

    /* 0x00E4BE9E-0x00E4BEB2: AUDIT_$LOG_EVENT(&header, &flag, &status,
     * &event_data, &data_len). */
    AUDIT_$LOG_EVENT((uid_t *)&event_header, &success_flag,
                     &status, (char *)&event_data, &data_len);
}
