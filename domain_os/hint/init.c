/*
 * HINT_$INIT - Initialize the hint subsystem
 *
 * Resolves (or creates) `node_data/hint_file, maps it with MST_$MAPS, locks
 * it and publishes it.  A first failure drops and deletes the file and
 * retries once; a second failure leaves HINT_$HINTFILE_PTR null.
 *
 * Address ranges (SR10.2 SAU2 image):
 *   0x00E3122C-0x00E3124E  prologue: retry flag false, HINT_$HINTFILE_PTR
 *                          cleared, globals.hintfile_uid := UID_$NIL
 *   0x00E31250-0x00E3126E  NAME_$RESOLVE
 *   0x00E31270-0x00E31290  NAME_$CR_FILE when the resolve failed
 *   0x00E31292-0x00E312CA  MST_$MAPS; the result is stored into
 *                          globals.hintfile_ptr (0x00E312C0) *before* the
 *                          status test
 *   0x00E312CC-0x00E312EC  FILE_$LOCK
 *   0x00E312EE-0x00E31300  version-1 test, read through globals.hintfile_ptr
 *   0x00E31302-0x00E31324  publish pointer and UID; reinitialise if not v7
 *   0x00E31326-0x00E3135E  network-match test, ROUTE_$PORT store
 *   0x00E31360-0x00E31398  failure arm: drop + delete, then retry once
 *   0x00E3139A-0x00E313A8  give up: HINT_$HINTFILE_PTR = NULL, epilogue
 *
 * Original address: 0x00E3122C (382 bytes)
 */

#include "hint/hint_internal.h"
#include "arch/arch.h"

/*
 * The three PC-relative constants the pathname calls share.  Bytes from the
 * image (gsk read 0xE313AA 0x1E):
 *
 *   0xE313AA  00 14                                  path length = 20
 *   0xE313AC  00 00                                  FILE_$LOCK lock index
 *   0xE313AE  00 04                                  FILE_$LOCK lock mode
 *   0xE313B0  00 00                                  FILE_$LOCK rights byte
 *   0xE313B4  60 6e 6f 64 65 5f 64 61 74 61 2f 68    "`node_data/hint_file"
 *             69 6e 74 5f 66 69 6c 65                (20 bytes, no NUL)
 *
 * NAME_$RESOLVE / NAME_$CR_FILE / NAME_$DROP each take the pathname address
 * (0xE313B4) and the length address (0xE313AA); see 0x00E31258/0x00E3125C,
 * 0x00E31278/0x00E3127C and 0x00E3136C/0x00E31370.
 */
static char hint_file_path[HINT_FILE_PATH_LEN] = HINT_FILE_PATH;
static int16_t hint_file_path_length = HINT_FILE_PATH_LEN;

/* 0xE313AC: FILE_$LOCK reads this as a word ("move.w (A1),-(SP)" at
 * 0x00E5EB68 through the pointer at A6+0x0C). */
static const uint16_t hint_lock_index = 0x0000;

/* 0xE313AE: FILE_$LOCK reads this as a word ("move.w (A4),-(SP)" at
 * 0x00E5EB62, A4 = the pointer at A6+0x10).  The image byte pair is 00 04. */
static const uint16_t hint_lock_mode = 0x0004;

/* 0xE313B0: FILE_$LOCK reads only the FIRST BYTE of this cell
 * ("move.b (A0),-(SP)" at 0x00E5EB60), so it is spelled as bytes to keep the
 * host build reading the same one the m68k does. */
static const uint8_t hint_lock_rights[2] = { 0x00, 0x00 };

void HINT_$INIT(void)
{
    int8_t retry_flag;        /* D2b: 0 initially, 0xFF after the first retry */
    uid_t hintfile_uid;       /* A6-0x08 */
    status_$t status;         /* A6-0x18 */
    uint8_t map_info[12];     /* A6-0x14: MST_$MAPS' map_info argument */
    uint32_t lock_info;       /* A6-0x1C: FILE_$LOCK's 5th argument */
    hint_file_t *mapped_ptr;
    const route_$port_t *port0;
    uint16_t net_info_hi;
    uint16_t net_info_lo;

    /* 0x00E31234-0x00E3124E */
    retry_flag = 0;
    HINT_$HINTFILE_PTR = NULL;
    HINT_$GLOBALS->hintfile_uid = UID_$NIL;

    for (;;) {
        /* 0x00E31250-0x00E3126E */
        NAME_$RESOLVE(hint_file_path, &hint_file_path_length,
                      &hintfile_uid, &status);

        if (status != status_$ok) {
            /* 0x00E31270-0x00E31290 */
            NAME_$CR_FILE(hint_file_path, &hint_file_path_length,
                          &hintfile_uid, &status);
            if (status != status_$ok) {
                goto handle_failure;
            }
        }

        /*
         * 0x00E31292-0x00E312B8.  Arguments in push order (right to left):
         *   0x00E312AE  clr.w   -(SP)          asid          = 0
         *   0x00E312AC  st      -(SP)          direction     = true (0xFF)
         *   0x00E312A8  pea (-0x8,A6)          uid           = &hintfile_uid
         *   0x00E312A6  clr.l   -(SP)          start_va      = 0
         *   0x00E312A2  pea (0x7fff).w         length        = 0x7FFF
         *   0x00E3129E  move.w #0x16,-(SP)     area_id       = 0x16
         *   0x00E3129C  clr.l   -(SP)          area_size     = 0
         *   0x00E3129A  st      -(SP)          access_rights = true (0xFF)
         *   0x00E31296  pea (-0x14,A6)         map_info
         *   0x00E31292  pea (-0x18,A6)         status
         */
        mapped_ptr = (hint_file_t *)MST_$MAPS(0, true, &hintfile_uid, 0,
                                              0x7FFF, 0x16, 0, true,
                                              map_info, &status);

        /*
         * 0x00E312BA-0x00E312C2: the mapped address goes into the module
         * block at globals+0x20 unconditionally, before the status is tested.
         */
        HINT_$GLOBALS->hintfile_ptr = ARCH_PTR_TO_VA(mapped_ptr);

        /* 0x00E312C4-0x00E312CA */
        if (status != status_$ok) {
            goto handle_failure;
        }

        /*
         * 0x00E312CC-0x00E312EC.  Six arguments; the fifth is the frame slot
         * at A6-0x1C ("pea (-0x1c,A6)" at 0x00E312D0), not a zero.
         */
        FILE_$LOCK(&hintfile_uid, &hint_lock_index, &hint_lock_mode,
                   hint_lock_rights, &lock_info, &status);

        /*
         * 0x00E312EE-0x00E31300: the version is read through the module
         * block's pointer ("movea.l (0x20,A1),A0 / cmpi.l #0x1,(A0)"), which
         * MST_$MAPS has just filled in.  A file still carrying the
         * uninitialised marker leaves HINT_$HINTFILE_PTR null.
         */
        mapped_ptr = ARCH_VA_TO_PTR(HINT_$GLOBALS->hintfile_ptr);
        if (mapped_ptr->header.version == HINT_FILE_UNINIT) {
            return;
        }

        /* 0x00E31302-0x00E31314 */
        HINT_$HINTFILE_PTR = ARCH_VA_TO_PTR(HINT_$GLOBALS->hintfile_ptr);
        HINT_$GLOBALS->hintfile_uid = hintfile_uid;

        /* 0x00E31316-0x00E31324 */
        mapped_ptr = ARCH_VA_TO_PTR(HINT_$GLOBALS->hintfile_ptr);
        if (mapped_ptr->header.version != HINT_FILE_VERSION) {
            HINT_$clear_hintfile();
        }

        /*
         * 0x00E31326-0x00E3135E: compare the two words of the header's
         * net_info against port 0's port_type/socket.  ROUTE_$PORTP is read
         * as a single longword ("movea.l (0x00e26ee8).l,A2"), i.e. entry 0.
         */
        port0 = ROUTE_$WIRED_DATA.portp[0];
        mapped_ptr = ARCH_VA_TO_PTR(HINT_$GLOBALS->hintfile_ptr);
        net_info_hi = (uint16_t)(mapped_ptr->header.net_info >> 16);
        net_info_lo = (uint16_t)(mapped_ptr->header.net_info);

        if (net_info_hi == port0->port_type && net_info_lo == port0->socket) {
            /* 0x00E3134A-0x00E31356: the image re-loads the pointer for the
             * store ("movea.l (0x20,A0),A1" at 0x00E3134A). */
            mapped_ptr = ARCH_VA_TO_PTR(HINT_$GLOBALS->hintfile_ptr);
            ROUTE_$PORT = mapped_ptr->header.net_port;
        } else {
            /* 0x00E31358 */
            ROUTE_$PORT = 0;
        }
        return;

handle_failure:
        /* 0x00E31360-0x00E31362 */
        if (retry_flag < 0) {
            /* 0x00E3139A: second failure - publish nothing */
            HINT_$HINTFILE_PTR = NULL;
            return;
        }

        /* 0x00E31364-0x00E31382 */
        NAME_$DROP(hint_file_path, &hint_file_path_length,
                   &hintfile_uid, &status);

        if (status == status_$ok) {
            /* 0x00E31384-0x00E31392 */
            FILE_$DELETE(&hintfile_uid, &status);
        }

        /* 0x00E31394-0x00E31396: st D2b, then bra back to 0x00E31250 */
        retry_flag = -1;
    }
}
