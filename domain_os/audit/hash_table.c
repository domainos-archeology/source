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
 * Resets all bucket pointers to NULL and resets the memory pool.
 */
void audit_$clear_hash_table(void)
{
    int i;
    status_$t status;

    /* Reset memory pool */
    audit_$alloc(0, &status);

    /* Clear all bucket pointers */
    for (i = 0; i < AUDIT_HASH_TABLE_SIZE; i++) {
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
    bucket = UID_$HASH(uid, &AUDIT_HASH_MODULO);

    /* Find the end of the bucket's linked list */
    bucket_ptr = &AUDIT_$DATA.hash_buckets[bucket];
    while (*bucket_ptr != NULL) {
        bucket_ptr = &(*bucket_ptr)->next;
    }

    /* Link the new node at the end */
    *bucket_ptr = node;
}
