/*
 * DIR_$UNMAP_PAGES - Unmap directory pages from memory
 *
 * Unmaps the cached directory page groups. For generic directories
 * (type 0), calls MST_$UNMAP_PRIVI to actually unmap the mapped
 * regions. For well-known directories (NODE, COM, WDIR, NDIR),
 * saves the mapping info back to the global mapping info arrays
 * so subsequent opens can reuse the mapping.
 *
 * dir_$handle_t.dir_kind selects which: 0 generic, 1 NODE, 2 COM, 3 WDIR,
 * 4 NDIR.  The 16 bytes at handle+0x20 that get saved are exactly a
 * name_$mapped_info_t (`lea (0x20,A2),A0` plus four `move.l (A0)+,(A1)+`).
 *
 * Parameters:
 *   handle - Pointer to handle structure
 *
 * Original address: 0x00E4B6BA
 * Original size: 252 bytes
 */

#include "dir/dir_internal.h"
#include "name/name.h"

/*
 * 0x00E4B7A0-0x00E4B7A6: `lea (0x20,A2),A0` then four `move.l (A0)+,(A1)+`.
 * The inverse of dir_$copy_mapped_info in dir/validate_handle.c - the 16
 * bytes at handle+0x20 and a name_$mapped_info_t share a layout.
 */
static void dir_$save_mapped_info(name_$mapped_info_t *info,
                                  const dir_$handle_t *h)
{
    info->active       = h->mapped;         /* 0x00 <- 0x20 */
    info->pad_01       = h->_0x21;          /* 0x01 <- 0x21 */
    info->reserved_02  = h->cache0_group;   /* 0x02 <- 0x22 */
    info->first_base   = h->cache0_base;    /* 0x04 <- 0x24 */
    info->reserved_08  = h->_0x28;          /* 0x08 <- 0x28 */
    info->entry_count  = h->cache1_group;   /* 0x0A <- 0x2A */
    info->second_base  = h->cache1_base;    /* 0x0C <- 0x2C */
}

/* `move.w #0x3,-(SP)` at 0x00E4B704 / 0x00E4B732 - MST_$UNMAP_PRIVI's mode. */
#define DIR_UNMAP_MODE      3
/* One cache group and the two of them together. */
#define DIR_UNMAP_GROUP     0x8000
#define DIR_UNMAP_BOTH      0x10000

void DIR_$UNMAP_PAGES(void *handle)
{
    dir_$handle_t *h = (dir_$handle_t *)handle;     /* A2 */
    status_$t      local_status;                    /* A6-0x0C */

    /* 0x00E4B6C6: `tst.b (0x20,A2)` / `bpl` - nothing mapped, nothing to do. */
    if (h->mapped >= 0) {
        return;
    }

    if (h->dir_kind == 0) {
        /* 0x00E4B6D4-0x00E4B6F2: one call covers both slots when they are
         * contiguous. */
        uint32_t unmap_size;                        /* D2 */

        if (h->cache0_base + DIR_UNMAP_GROUP == h->cache1_base) {
            unmap_size = DIR_UNMAP_BOTH;
        } else {
            unmap_size = DIR_UNMAP_GROUP;
        }

        /* 0x00E4B6F2-0x00E4B708: `pea (A2)` is the handle, whose first
         * eight bytes are the object's UID. */
        MST_$UNMAP_PRIVI(DIR_UNMAP_MODE, &h->uid, h->cache0_base,
                         unmap_size, PROC1_$AS_ID, &local_status);

        /* 0x00E4B712-0x00E4B736: a non-contiguous pair needs a second call. */
        if (unmap_size == DIR_UNMAP_GROUP) {
            MST_$UNMAP_PRIVI(DIR_UNMAP_MODE, &h->uid, h->cache1_base,
                             DIR_UNMAP_GROUP, PROC1_$AS_ID, &local_status);
        }
    } else {
        /* 0x00E4B73E-0x00E4B7A6: hand the mapping back to its owner so the
         * next open can reuse it. */
        name_$mapped_info_t *dest;

        if (h->dir_kind == 1) {
            dest = &NAME_$NODE_MAPPED_INFO;             /* 0x00E4B74A */
        } else if (h->dir_kind == 2) {
            dest = &NAME_$COM_MAPPED_INFO;              /* 0x00E4B75E */
        } else if (h->dir_kind == 3) {
            /* per-ASID slot: base + (PROC1_$AS_ID << 4) */
            dest = &NAME_$DATA.wdir_mapped_info[PROC1_$AS_ID];
        } else if (h->dir_kind == 4) {
            dest = &NAME_$DATA.ndir_mapped_info[PROC1_$AS_ID];
        } else {
            goto clear_flag;                            /* 0x00E4B788 */
        }

        dir_$save_mapped_info(dest, h);
    }

clear_flag:
    h->mapped = 0;      /* 0x00E4B7A8 `clr.b (0x20,A2)` */
}
