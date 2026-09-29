/*
 * HINT_$clear_hintfile - Clear and reinitialize hint file
 *
 * Called when the hint file needs to be reinitialized (version != 7).
 * Truncates the file to zero and fills it with the initialized structure.
 *
 * Original address: 0x00E31194
 */

#include "hint/hint_internal.h"

void HINT_$clear_hintfile(void)
{
    hint_file_t *hintfile;
    hint_bucket_t *bucket;
    hint_slot_t *slot;
    hint_addr_t *addr;
    int16_t bucket_idx;
    int16_t slot_idx;
    int16_t addr_idx;
    status_$t status;
    /*
     * 0x00E311A2 `pea (-0x10,A6)` is AST_$TRUNCATE's fourth argument, a
     * one-BYTE Domain boolean out-cell: the callee writes it with
     * `move.b D3b,(A0)` at 0x00E05C70 and `st (A1)` at 0x00E05DB6.  A byte
     * is the right width, and nothing here reads it back.
     */
    boolean truncate_result;

    /*
     * 0x00E3119C-0x00E311BA: AST_$TRUNCATE(&uid, 0L, 0, &byte_out, &status).
     * The `lea (0x14,SP),SP` that pops the call accounts for 2 (result slot)
     * + 4 + 4 + 2 + 4 + 4 = 0x14, which is what fixes the third argument as
     * a word and the second as a longword.
     */
    AST_$TRUNCATE(&HINT_$HINTFILE_UID, 0, 0, &truncate_result, &status);

    hintfile = HINT_$HINTFILE_PTR;

    /* Initialize the header */
    hintfile->header.version = HINT_FILE_VERSION;  /* 7 = initialized */
    hintfile->header.net_port = 0;

    /*
     * 0x00E311D2-0x00E311DE: ONE longword out of ROUTE_$PORTP[0]+0x2E, which
     * is route_$port_t.port_type followed by route_$port_t.socket.  Built
     * from the two words rather than read through a longword cast, so the
     * value is the m68k one on a little-endian host too.
     */
    hintfile->header.net_info =
        ((uint32_t)ROUTE_$WIRED_DATA.portp[0]->port_type << 16) | ROUTE_$WIRED_DATA.portp[0]->socket;

    /*
     * Clear all hash buckets.  0x00E311E0 `moveq #0x40,D0` / 0x00E3121E
     * `dbf D0w` is 65 iterations over a 0x54-byte stride, so slots 0..64 are
     * cleared even though the hash only ever selects 0..63 - hence
     * HINT_HASH_SLOTS rather than HINT_HASH_SIZE (bead source-nrfl).
     */
    for (bucket_idx = HINT_HASH_SLOTS - 1; bucket_idx >= 0; bucket_idx--) {
        bucket = &hintfile->buckets[bucket_idx];

        /* 0x00E311E6 `moveq #0x2,D1` + `dbf`: three slots, stride 0x1C. */
        for (slot_idx = HINT_SLOTS_PER_BUCKET - 1; slot_idx >= 0; slot_idx--) {
            slot = &bucket->slots[slot_idx];

            /* 0x00E311F0 `clr.l (-0x10,A1)`: the slot's UID key. */
            slot->uid_low_masked = 0;

            /*
             * 0x00E311F4 `moveq #0x2,D3` + `dbf`: three 8-byte address
             * pairs, cleared as 0x00E311FC / 0x00E31200 `clr.l`.
             */
            for (addr_idx = HINT_ADDRS_PER_SLOT - 1; addr_idx >= 0; addr_idx--) {
                slot->addrs[addr_idx].flags = 0;
                slot->addrs[addr_idx].node_id = 0;
            }
        }
    }
}
