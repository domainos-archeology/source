/*
 * Test cases for MEM_$PARITY_LOG
 *
 * These tests verify the parity error logging functionality.
 * The function tracks parity errors by memory board and by page.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

/* Memory board boundary (3MB mark) */
#define MEM_BOARD_BOUNDARY      0x300000

/* Number of per-page error tracking records */
#define MEM_PARITY_PAGE_RECORDS 4

/* Memory parity record for tracking errors per page */
typedef struct mem_parity_record_t {
    uint32_t    phys_addr;          /* 0x00: Physical address */
    uint16_t    count;              /* 0x04: Error count for this page */
    uint8_t     reserved[12];       /* 0x06: Padding to 0x12 bytes */
} mem_parity_record_t;

/* Mock global state */
static uint16_t mock_board1_count;
static uint16_t mock_board2_count;
static mem_parity_record_t mock_records[MEM_PARITY_PAGE_RECORDS];

/* Macros to use mock state */
#define MEM_BOARD1_COUNT        mock_board1_count
#define MEM_BOARD2_COUNT        mock_board2_count
#define MEM_PARITY_RECORDS      mock_records

/*
 * Test implementation of MEM_$PARITY_LOG
 */
void test_MEM_$PARITY_LOG(uint32_t phys_addr)
{
    int16_t board_index;
    int16_t search_count;
    int16_t record_index;
    int16_t min_count_index;
    int16_t min_count;
    int16_t scan_index;
    uint8_t page_id;
    mem_parity_record_t *rec;

    /* Determine which memory board */
    if (phys_addr < MEM_BOARD_BOUNDARY) {
        board_index = 1;
    } else {
        board_index = 2;
    }

    /* Increment per-board error count */
    if (board_index == 1) {
        MEM_BOARD1_COUNT++;
    } else {
        MEM_BOARD2_COUNT++;
    }

    /* Extract page identifier (bits 8-13) */
    page_id = (phys_addr >> 8) & 0x3F;

    /* Search existing records for a matching page */
    search_count = MEM_PARITY_PAGE_RECORDS - 1;
    record_index = 1;
    rec = &MEM_PARITY_RECORDS[0];

    while (search_count >= 0) {
        if (rec->count == 0) {
            break;
        }

        if (((rec->phys_addr >> 8) & 0x3F) == page_id) {
            rec->count++;
            return;
        }

        record_index++;
        rec++;
        search_count--;
    }

    /* Find record with minimum count to replace */
    min_count_index = 1;
    min_count = MEM_PARITY_RECORDS[0].count;
    scan_index = 2;
    rec = &MEM_PARITY_RECORDS[1];

    for (search_count = 2; search_count >= 0; search_count--) {
        if ((int16_t)rec->count < min_count) {
            min_count = rec->count;
            min_count_index = scan_index;
        }
        scan_index++;
        rec++;
    }

    /* Replace the record with minimum count */
    rec = &MEM_PARITY_RECORDS[min_count_index - 1];
    rec->count = 1;
    rec->phys_addr = phys_addr;
}

/* Reset test state */
void reset_state(void)
{
    mock_board1_count = 0;
    mock_board2_count = 0;
    memset(mock_records, 0, sizeof(mock_records));
}

/* Test: Board counting works correctly */
void test_board_counting(void)
{
    reset_state();

    /* Address below 3MB should increment board1 */
    test_MEM_$PARITY_LOG(0x100000);
    assert(mock_board1_count == 1);
    assert(mock_board2_count == 0);

    /* Another address below 3MB */
    test_MEM_$PARITY_LOG(0x200000);
    assert(mock_board1_count == 2);
    assert(mock_board2_count == 0);

    /* Address at 3MB boundary - should be board2 */
    test_MEM_$PARITY_LOG(0x300000);
    assert(mock_board1_count == 2);
    assert(mock_board2_count == 1);

    /* Address above 3MB */
    test_MEM_$PARITY_LOG(0x400000);
    assert(mock_board1_count == 2);
    assert(mock_board2_count == 2);

    printf("test_board_counting: PASSED\n");
}

/* Test: First error creates a record */
void test_first_error(void)
{
    reset_state();

    test_MEM_$PARITY_LOG(0x100400);

    /* Should have created a record */
    assert(mock_records[0].count == 1);
    assert(mock_records[0].phys_addr == 0x100400);

    printf("test_first_error: PASSED\n");
}

/* Test: Same page error increments count */
void test_same_page_increment(void)
{
    reset_state();

    /* First error at page */
    test_MEM_$PARITY_LOG(0x100400);
    assert(mock_records[0].count == 1);

    /* Same page ID (bits 8-13 match) */
    test_MEM_$PARITY_LOG(0x100480);  /* Different low bits but same page ID */
    assert(mock_records[0].count == 2);

    /* Another hit on same page */
    test_MEM_$PARITY_LOG(0x100400);
    assert(mock_records[0].count == 3);

    printf("test_same_page_increment: PASSED\n");
}

/* Test: Different pages create different records */
void test_multiple_pages(void)
{
    reset_state();

    /* Different page IDs (bits 8-13 differ) */
    test_MEM_$PARITY_LOG(0x100100);  /* Page ID 0x01 */
    test_MEM_$PARITY_LOG(0x100200);  /* Page ID 0x02 */
    test_MEM_$PARITY_LOG(0x100300);  /* Page ID 0x03 */

    /* Each should be in its own record */
    int records_with_count = 0;
    for (int i = 0; i < MEM_PARITY_PAGE_RECORDS; i++) {
        if (mock_records[i].count > 0) {
            records_with_count++;
        }
    }
    assert(records_with_count >= 3);

    printf("test_multiple_pages: PASSED\n");
}

/* Test: Record replacement when full */
void test_record_replacement(void)
{
    reset_state();

    /* Fill all records with different pages */
    test_MEM_$PARITY_LOG(0x100100);  /* Record 0: count 1 */
    test_MEM_$PARITY_LOG(0x100200);  /* Record 1: count 1 */
    test_MEM_$PARITY_LOG(0x100300);  /* Record 2: count 1 */
    test_MEM_$PARITY_LOG(0x100400);  /* Record 3: count 1 */

    /* Increment one of them to make it "more important" */
    test_MEM_$PARITY_LOG(0x100100);  /* Record 0: count 2 */
    test_MEM_$PARITY_LOG(0x100100);  /* Record 0: count 3 */

    /* Now add a new page - it should replace the lowest count (1) */
    test_MEM_$PARITY_LOG(0x100500);  /* Should replace one of the count=1 records */

    /* Record 0 should still exist with count 3 */
    int found_high_count = 0;
    int found_new_page = 0;
    for (int i = 0; i < MEM_PARITY_PAGE_RECORDS; i++) {
        if (mock_records[i].count == 3) {
            found_high_count = 1;
        }
        if (mock_records[i].phys_addr == 0x100500) {
            found_new_page = 1;
        }
    }
    assert(found_high_count);
    assert(found_new_page);

    printf("test_record_replacement: PASSED\n");
}

int main(void)
{
    printf("Running MEM_$PARITY_LOG tests...\n\n");

    test_board_counting();
    test_first_error();
    test_same_page_increment();
    test_multiple_pages();
    test_record_replacement();

    printf("\nAll tests PASSED!\n");
    return 0;
}
