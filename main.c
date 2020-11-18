#include "heap.h"

#define EXIT_SUCCESS 0

int main(void) {
    printf("===TEST 1===\n");
    heap_setup();
    heap_malloc(1);
    display_heap();
    heap_clean();
    printf("===END===\n");
    
    printf("===TEST 2===\n");
    heap_setup();
    heap_malloc(PAGE_SIZE); 
    display_heap();
    heap_clean();
    printf("===END===\n");

    printf("===TEST 3===\n");
    heap_setup();
    uint8_t* ptr1 = heap_malloc(PAGE_SIZE); 
    uint8_t* ptr2 = heap_malloc(1);
    assert (ptr1 + PAGE_SIZE + CONTROL_STRUCT_SIZE + FENCE_LENGTH * 2 == ptr2);
    display_heap();
    heap_clean();
    printf("===END===\n");

    printf("===TEST 4===\n");
    heap_setup();
    heap_malloc(3 * PAGE_SIZE);
    display_heap();
    heap_clean();
    printf("===END===\n");

    printf("===TEST 5===\n");
    heap_setup();
    heap_malloc(3 * PAGE_SIZE);
    heap_malloc(2);
    display_heap();
    heap_clean();
    printf("===END===\n");

    printf("===TEST 6===\n");
    heap_setup();
    heap_malloc(3 * PAGE_SIZE);
    heap_malloc(2);
    heap_malloc(99);
    display_heap();
    heap_clean();
    printf("===END===\n");
        
    printf("===TEST 7===\n");
    heap_setup();
    heap_malloc(100);
    heap_malloc(3 * PAGE_SIZE);
    heap_malloc(2);
    heap_malloc(5 * PAGE_SIZE);
    heap_malloc(200000);
    heap_malloc(123);
    display_heap();
    heap_clean();
    printf("===END===\n");
    
    return EXIT_SUCCESS;
}
