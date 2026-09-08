/*
 * PKT_$COPY_TO_PA - Copy caller data into network data buffers
 *
 * Copies "len" bytes from a virtual address into a chain of NETBUF data
 * buffers, filling buffers_out[] with one buffer address (ppn << 10) per
 * PKT_CHUNK_SIZE (0x400) byte chunk.
 *
 * The whole body runs under a FIM cleanup handler, so the function has two
 * entries: the normal fall-through and the re-entry FIM_$CLEANUP makes when a
 * fault unwinds into it.  On that second entry the routine returns the VA it
 * still holds mapped *and* every data buffer it had already allocated, then
 * re-signals the fault to the next handler.
 *
 * Address ranges (SR10.2 SAU2 image):
 *   0x00E1251C-0x00E12556  prologue, locals cleared, FIM_$CLEANUP
 *   0x00E12558-0x00E125E6  copy loop
 *   0x00E125E8-0x00E125F8  normal exit (FIM_$RLS_CLEANUP, *status_ret = 0)
 *   0x00E125FA-0x00E1260A  cleanup: return the mapped VA if one is held
 *   0x00E1260C-0x00E1262E  cleanup: NETBUF_$RTN_DAT over the buffers taken
 *   0x00E12632-0x00E1264C  cleanup: FIM_$SIGNAL, *status_ret = status, exit
 *
 * Original address: 0x00E1251C (306 bytes)
 */

#include "pkt/pkt_internal.h"
#include "misc/crash_system.h"

void PKT_$COPY_TO_PA(char *src_va, uint16_t len, uint32_t *buffers_out,
                     status_$t *status_ret)
{
    uint16_t chunk_size;
    int16_t remaining;   /* D2w */
    uint32_t buf_va;     /* A6-0x20 */
    char *src_ptr;       /* A6-0x1c */
    uint32_t *buf_ptr;   /* A2 */
    int16_t buf_count;   /* A6-0x28 */
    int16_t i;
    status_$t status;                /* A6-0x24 */
    uint8_t cleanup_context[24];     /* A6-0x18 */

    /* 0x00E1252A-0x00E1253E */
    *buffers_out = 0;
    buf_va = 0;
    buf_count = 0;
    src_ptr = src_va;

    /* 0x00E1253E-0x00E12554 */
    status = FIM_$CLEANUP(cleanup_context);

    if (status == status_$cleanup_handler_set) {
        /* --- normal path, 0x00E12558-0x00E125F8 --- */
        remaining = (int16_t)len;          /* 0x00E12558 move.w (0xc,A6),D2w */
        buf_ptr = buffers_out;             /* 0x00E12560 lea (A0),A2 */

        while (remaining > 0) {            /* 0x00E12562 / 0x00E125E4 */
            buf_count++;                   /* 0x00E12568 addq.w #0x1,(-0x28,A6) */
            buf_ptr++;                     /* 0x00E1256C addq.l #0x4,A2 */

            /* 0x00E1256E-0x00E12578: by reference, fills the slot */
            NETBUF_$GET_DAT(buf_ptr - 1);

            /* 0x00E1257A-0x00E1258C: buffer address by value */
            NETBUF_$GETVA(*(buf_ptr - 1), &buf_va, &status);
            if (status != status_$ok) {    /* 0x00E12590 tst.l (-0x24,A6) */
                CRASH_SYSTEM(&status);     /* 0x00E12596 */
                break;                     /* 0x00E125A2 bra done */
            }

            /* 0x00E125A4-0x00E125AC: chunk = min(0x400, remaining) */
            chunk_size = PKT_CHUNK_SIZE;
            if (remaining < PKT_CHUNK_SIZE) {
                chunk_size = (uint16_t)remaining;
            }

            /* 0x00E125AE-0x00E125C2 */
            OS_$DATA_COPY(src_ptr, ARCH_VA_TO_PTR(buf_va), (uint32_t)chunk_size);

            /* 0x00E125C6-0x00E125D2 */
            NETBUF_$RTNVA(&buf_va);
            buf_va = 0;

            /* 0x00E125D6-0x00E125E0 */
            src_ptr += PKT_CHUNK_SIZE;
            remaining -= PKT_CHUNK_SIZE;
        }

        /* 0x00E125E8-0x00E125F6 */
        FIM_$RLS_CLEANUP(cleanup_context);
        *status_ret = status_$ok;
    } else {
        /* --- cleanup path, 0x00E125FA-0x00E12642 --- */

        /* 0x00E125FA-0x00E1260A: unmap the VA still held, if any */
        if (buf_va != 0) {
            NETBUF_$RTNVA(&buf_va);
        }

        /*
         * 0x00E1260C-0x00E1262E: give back every data buffer already taken.
         *   move.w (-0x28,A6),D0w / subq.w #0x1,D0w / bmi
         *   lea (0x4,A0),A2
         *   loop: move.l (-0x4,A2),-(SP) / jsr NETBUF_$RTN_DAT / addq.l #4,A2
         *         dbf D2w,loop
         * dbf with D2 = buf_count-1 runs buf_count times, one per slot.
         */
        buf_ptr = buffers_out + 1;
        for (i = (int16_t)(buf_count - 1); i >= 0; i--) {
            NETBUF_$RTN_DAT(*(buf_ptr - 1));
            buf_ptr++;
        }

        /* 0x00E12632-0x00E12642 */
        FIM_$SIGNAL(status);
        *status_ret = status;
    }
}
