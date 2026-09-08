/*
 * hash_table.c - Audit list hash table functions
 *
 * Manages the hash table used for selective auditing.
 * When the audit list is loaded, UIDs are hashed into buckets
 * for efficient lookup during event logging.
 *
 * Original addresses:
 *   audit_$clear_hash_table: 0x00E7128A
 *   audit_$add_to_hash:      0x00E712BA
 *   audit_$alloc:            0x00E7120C
 */

#include "audit/audit_internal.h"
#include "mmu/mmu.h"    /* MMU_$INSTALL - audit_$alloc wires its own pages */
#include "wp/wp.h"      /* WP_$CALLOC */

/*
 * audit_$alloc (0x00E7120C) - the AUDIT_ module's bump allocator
 *
 * Two modes, selected by the word at (0x8,A6):
 *
 *   size == 0   0x00E7121E `move.l #0xec4800,(0x1a4,A5)` resets pool_next to
 *               AUDIT_POOL_BASE_VA.  0x00E71226-0x00E7122C sets pool_limit to
 *               the same VA ONLY when it is still zero, so the pages wired by
 *               earlier calls stay wired across a reset.  Returns NIL
 *               (0x00E71232 `clr.l D2`) and does not touch status_ret.
 *
 *   size != 0   0x00E71238 saves the old pool_next as the result, 0x00E7123C
 *               `ext.l D1` sign-extends the size word and 0x00E7123E adds it
 *               to pool_next.  Then, while pool_next is at or past pool_limit
 *               (0x00E71274-0x00E7127C `cmp.l` + `bcc`, an UNSIGNED compare),
 *               one 0x400-byte page is wired in at pool_limit:
 *                 0x00E7124A  WP_$CALLOC(&ppn, status_ret)
 *                 0x00E71252  `tst.l (A2)` - bail out on a bad status, still
 *                             returning the block that was handed out
 *                 0x00E71262  MMU_$INSTALL(ppn, pool_limit, 0x16)
 *                 0x00E7126C  pool_limit += 0x400
 *
 * NOTE, PRESERVED AS FOUND: neither path ever stores status_$ok.  status_ret
 * is written only by WP_$CALLOC, so a call that needs no new page leaves the
 * caller's status cell exactly as it found it - which is why
 * audit_$add_to_hash's `tst.l (A4)` at 0x00E712DA is meaningful only because
 * its own caller cleared the cell first.
 *
 * The declared parameter is `uint16_t size`, but the image sign-extends the
 * word, so the addition is spelled with an int16_t cast to reproduce
 * 0x00E7123C exactly.  (source-3ad7)
 */

/* 0x00E71256 `pea (0x16).w` - the same MMU_$INSTALL flag word AREA_$INIT and
 * the PEB control page use. */
#define AUDIT_POOL_MMU_FLAGS    0x16

/* 0x00E7126C `addi.l #0x400,(0x1a8,A5)` - one page per WP_$CALLOC. */
#define AUDIT_POOL_PAGE_SIZE    0x400

void *audit_$alloc(uint16_t size, status_$t *status_ret)
{
    uint8_t *result;
    uint32_t ppn;

    /* 0x00E7121C `bne.b` - the reset arm. */
    if (size == 0) {
        AUDIT_$DATA.pool_next = (uint8_t *)ARCH_VA_TO_PTR(AUDIT_POOL_BASE_VA);

        /* 0x00E71226 `tst.l (0x1a8,A5)` / `bne.b`: first call only. */
        if (AUDIT_$DATA.pool_limit == NULL) {
            AUDIT_$DATA.pool_limit = AUDIT_$DATA.pool_next;
        }

        return NULL;                            /* 0x00E71232 `clr.l D2` */
    }

    /* 0x00E71238-0x00E7123E: hand out the old cursor, then advance it by the
     * SIGN-EXTENDED size word. */
    result = AUDIT_$DATA.pool_next;
    AUDIT_$DATA.pool_next += (int16_t)size;

    /* 0x00E71274-0x00E7127C: `bcc` is an unsigned >=, and the test runs
     * before the first body (0x00E71242 `bra.b`). */
    while (ARCH_PTR_TO_VA(AUDIT_$DATA.pool_next) >=
           ARCH_PTR_TO_VA(AUDIT_$DATA.pool_limit)) {

        WP_$CALLOC(&ppn, status_ret);           /* 0x00E7124A */

        /* 0x00E71252-0x00E71254: leave with the block already handed out. */
        if (*status_ret != status_$ok) {
            return result;
        }

        MMU_$INSTALL(ppn, ARCH_PTR_TO_VA(AUDIT_$DATA.pool_limit),
                     AUDIT_POOL_MMU_FLAGS);     /* 0x00E71262 */

        AUDIT_$DATA.pool_limit += AUDIT_POOL_PAGE_SIZE;  /* 0x00E7126C */
    }

    return result;                              /* 0x00E7127E `move.l D2,D0` */
}

/*
 * There is no audit_$free in the image: 0x00E7120C is the only allocator
 * entry point and neither of its two callers (0x00E7129A, 0x00E712D2) has a
 * free-shaped argument.  Blocks are reclaimed only by the `alloc(0)` reset
 * audit_$clear_hash_table performs.  The prototype still sitting in
 * audit/audit_internal.h has no definition on purpose.  (source-3ad7)
 */

/*
 * audit_$clear_hash_table - Clear the audit list hash table
 *
 * Resets the memory pool and clears the bucket pointers.
 *
 * In the image this is a nested procedure of audit_$load_list: it takes no
 * pushed arguments, reaches its parent's frame with `movea.l (A6),A2`
 * (0x00E71290) and forwards the parent's status_ret -- the longword at
 * A2+0x08, audit_$load_list's own first argument -- to audit_$alloc
 * (0x00E71294).  Flattened here with that uplevel reference passed
 * explicitly.
 *
 * OFF-BY-ONE, PRESERVED AS FOUND.  The clear loop is
 *
 *     00e712a0  movea.l A5,A0
 *     00e712a2  moveq   #0x24,D0
 *     00e712a4  addq.l  #0x4,A0        ; A0 = A5 + 4
 *     00e712a6  movea.l A0,A0
 *     00e712a8  clr.l   (0xb0,A0)      ; first store is A5 + 0xB4
 *     00e712ac  addq.l  #0x4,A0
 *     00e712ae  dbf     D0w,0x00e712a8 ; 0x25 = 37 iterations
 *
 * so it clears the longwords at A5+0xB4 .. A5+0x144, i.e. bucket slots
 * 1..37.  audit_$add_to_hash and AUDIT_$LOG_EVENT_S index the same array
 * with UID_$HASH's remainder (0x00E17376 `divu.w` + `swap`), which is
 * 0..36 for the modulus 37.  Slot 0 is therefore never cleared and slot 37
 * is cleared but never used.  Reproduced exactly.
 */
void audit_$clear_hash_table(status_$t *status_ret)
{
    int16_t i;

    /* 0x00E71292-0x00E7129E: audit_$alloc(0, parent's status_ret). */
    audit_$alloc(0, status_ret);

    /* 0x00E712A0-0x00E712AE: slots 1..37, see the note above. */
    for (i = 1; i <= AUDIT_HASH_TABLE_SIZE; i++) {
        AUDIT_$DATA.hash_buckets[i] = NULL;
    }
}

/*
 * audit_$add_to_hash - Add a UID to the hash table
 *
 * Allocates a new hash node, stores the UID, and links it
 * into the appropriate bucket.
 */
void audit_$add_to_hash(uid_t *uid, status_$t *status_ret)
{
    audit_hash_node_t *node;
    audit_hash_node_t **bucket_ptr;
    int16_t bucket;

    /* Allocate a new node */
    node = (audit_hash_node_t *)audit_$alloc(sizeof(audit_hash_node_t), status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /* Initialize the node */
    node->uid_high = uid->high;
    node->uid_low = uid->low;
    node->next = NULL;

    /* Hash the UID to find the bucket */
    /* 0x00E712EA: `pea (-0x228,PC)` -> the shared 0x00E710C4 cell. */
    bucket = UID_$HASH(uid, (uint16_t *)&audit_$hash_modulus);

    /* Find the end of the bucket's linked list */
    bucket_ptr = &AUDIT_$DATA.hash_buckets[bucket];
    while (*bucket_ptr != NULL) {
        bucket_ptr = &(*bucket_ptr)->next;
    }

    /* Link the new node at the end */
    *bucket_ptr = node;
}
