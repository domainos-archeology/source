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

/*
 * TODO: audit_$alloc below is a stand-in, not a transcription.  The image
 * (0x00E7120C) is a bump allocator over AUDIT_$DATA.pool_next /
 * .pool_limit starting at the fixed VA AUDIT_POOL_BASE_VA, wiring one
 * 0x400-byte page at a time with WP_$CALLOC (0x00E070EC) + MMU_$INSTALL
 * (0x00E24048).  Tracked by bead source-3ad7.
 *
 * Simple memory pool for hash nodes.
 * In the original implementation, this used wired memory allocation.
 * For simplicity, we use a static pool here.
 */
#define AUDIT_MAX_HASH_NODES    AUDIT_MAX_LIST_ENTRIES

static audit_hash_node_t hash_node_pool[AUDIT_MAX_HASH_NODES];
static int16_t hash_node_next = 0;

/*
 * audit_$alloc - Allocate memory for hash nodes
 *
 * Allocates a block from the hash node pool.
 */
void *audit_$alloc(uint16_t size, status_$t *status_ret)
{
    audit_hash_node_t *node;

    if (size == 0) {
        /* Size 0 is a reset request (used before loading new list) */
        hash_node_next = 0;
        *status_ret = status_$ok;
        return NULL;
    }

    if (hash_node_next >= AUDIT_MAX_HASH_NODES) {
        *status_ret = status_$audit_excessive_event_types;
        return NULL;
    }

    node = &hash_node_pool[hash_node_next++];
    *status_ret = status_$ok;
    return node;
}

/*
 * audit_$free - Free memory to the pool
 *
 * In our simple implementation, freeing is a no-op.
 * Memory is reclaimed when the pool is reset.
 */
void audit_$free(void *ptr)
{
    /* No-op in this implementation */
    (void)ptr;
}

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
