#include "heap.h"


static Heap__ *heap = NULL;

static long long calc_ptrs_distance(void *ptr1, void *ptr2) {
    if (!ptr1 || !ptr2) return 0;
    return llabs((long long)ptr1 - (long long)ptr2);
}

int heap_setup(void) {
    if ((heap = (Heap__*)custom_sbrk(PAGE_SIZE)) == SBRK_FAIL) return HEAP_INIT_FAIL;
    heap->pages++;
    heap->head = NULL;
    return 0;
}


int heap_validate(void) {
    return 0;
}


void heap_clean(void) {
    int mem_size = heap->pages * PAGE_SIZE;
    memset(heap, 0x0, mem_size);
    custom_sbrk(-mem_size);
}


static int request_more_space() {
    uint8_t *handler = custom_sbrk(PAGE_SIZE);
    if (!handler) return HEAP_INIT_FAIL;
    heap->pages++;
    return 0;
} 


static void update_heap_info() {
    heap->headers_allocated++;
    heap->control_sum += 2 * FENCE_LENGTH;
}


static void fill_fences(Header__ *header) {
    for (int i = 0; i < FENCE_LENGTH; i++) {
        *((uint8_t*)header + CONTROL_STRUCT_SIZE + i) = 'f';
        *((uint8_t*)header + CONTROL_STRUCT_SIZE + FENCE_LENGTH + header->mem_size + i) = 'F';
    }
}


static void set_header(Header__ *header, const size_t mem_size, Header__ *prv, Header__ *nxt) {
    header->is_free = false;
    header->mem_size = mem_size;
    header->prev = prv;
    header->next = nxt;
    header->user_memptr = (uint8_t*)header + CONTROL_STRUCT_SIZE + FENCE_LENGTH;
    if (prv) prv->next = header;
    if (nxt) nxt->prev = header;
    fill_fences(header);
    update_heap_info();
}


static void copy_fences(void *src, void *dst) {
    memcpy(dst, src, FENCE_LENGTH);
}


static Header__* last() {
    if (!heap || !heap->head) return NULL;
    Header__ *iterator = heap->head;
    while (iterator->next) iterator = iterator->next;
    return iterator;
}

/* 
 * From [.....cccfffUUUUUUUUUUUUUUFFF.......] to  [.....cccfffUUUFFF|cccfffUUFFF........]
 *      [       FREE HEADER BLOCK           ] to [Occupied header^  ^FREE HEADER BLOCK  ]
 * 
 * c - control struct
 * f - left fence
 * U - user's memory
 * F - right fence
*/ 
static void split_headers(Header__ *current, size_t new_mem_size) {
    Header__ *new_header = (Header__*)((uint8_t*)current->user_memptr + new_mem_size + FENCE_LENGTH);
    size_t prior_mem_size = current->mem_size; 

    current->is_free = false;
    current->mem_size = new_mem_size;
    copy_fences((uint8_t*)current->user_memptr + current->mem_size, (uint8_t*)current->user_memptr + prior_mem_size);
    set_header(new_header, prior_mem_size - new_mem_size, current, current->next);
    new_header->is_free = true;
    current->next = new_header;
}

/* header - memory layout - control fences user_space fences  */
void* heap_malloc(size_t size) {
    if (size < 1 || heap_validate() || HEADER_SIZE(size) < size) return NULL;

    //Heap has no blocks at all
    if (!heap->head) {
        if (heap->pages * PAGE_SIZE - sizeof(Heap__) < HEADER_SIZE(size)) {
            return REQUEST_SPACE_FAIL == request_more_space() ? NULL : heap_malloc(size);
        }
        heap->head = (Header__*)((uint8_t*)heap + sizeof(Heap__));
        set_header(heap->head, size, NULL, NULL);
        return heap->head->user_memptr;
    }

    //Search for perfect block existing already on heap
    Header__ *iterator = heap->head;
    while (iterator) {
        if (iterator->is_free && iterator->mem_size == size) {
            iterator->is_free = false;
            return iterator->user_memptr;
        } else if (iterator->is_free && iterator->mem_size > HEADER_SIZE(size) + 1) { //At least one byte for splittedheader's user mem
            split_headers(iterator, size);
            return iterator->user_memptr;
        }
        iterator = iterator->next;
    }

    //Create header between last node and end of heap memory
    Header__ *last_header = last();

    if (calc_ptrs_distance((uint8_t*)heap + heap->pages * PAGE_SIZE, (uint8_t*)last_header->user_memptr + last_header->mem_size + FENCE_LENGTH)
        <= (long long)(HEADER_SIZE(size))) {
            return REQUEST_SPACE_FAIL == request_more_space() ? NULL : heap_malloc(size);
    }

    set_header((Header__*)((uint8_t*)last_header->user_memptr + last_header->mem_size + FENCE_LENGTH), size, last_header, NULL);
    return last()->user_memptr;
}

//TODO: func to implement
void* heap_calloc(size_t number, size_t size) {    
    return NULL;
}

//TODO: func to implement
void* heap_realloc(void* memblock, size_t count) {
    return NULL;
}


static void join_forward(Header__ *current) {
    printf("JOINING");
    Header__ *nxt = current->next;
    current->mem_size += current->next->mem_size + CONTROL_STRUCT_SIZE + FENCE_LENGTH;
    current->next = nxt->next;
}


static Header__* join_backward(Header__ *current) {
    printf("BACKWARD");
    Header__ *handler = current->prev;
    handler->mem_size += current->mem_size + CONTROL_STRUCT_SIZE + FENCE_LENGTH;
    handler->next = current->next;
    return handler;
}


/* From [...cccfffUUUFFFcccfffUUUUFFF...] to [...cccfffUUUUUUUUUUUUUUUUFFF...] */
void heap_free(void* memblock) {
    if (!memblock) return;
    Header__ *handler = (Header__*)((uint8_t*)memblock - FENCE_LENGTH - CONTROL_STRUCT_SIZE);
    handler->is_free = true;
    
    Header__ *nxt = handler->next;
    Header__ *prv = handler->prev;

    if (prv && prv->is_free) handler = join_backward(handler);
    if (nxt && nxt->is_free) join_forward(handler);
}

//TODO: func to implement
void* heap_malloc_aligned(size_t count) {
    return NULL;
}

//TODO: func to implement
void* heap_calloc_aligned(size_t number, size_t size) {
    return NULL;
}   

//TODO: func to implement
void* heap_realloc_aligned(void* memblock, size_t size) {
    return NULL;
} 


size_t heap_get_largest_used_block_size(void) {
    
    if (!heap || !heap->head || !heap_validate()) return 0;

    size_t max = 0;
    Header__ *iterator = heap->head;

    while (iterator) {
        if (!iterator->is_free) {
            max = iterator->mem_size > max ? iterator->mem_size : max;
        }
        iterator = iterator->next;
    }
    return max;
}

//TODO: func to implement
enum pointer_type_t get_pointer_type(const void* const pointer) {
    return 0;
}


void display_heap() {
    if (!heap || !heap->head) return;

    Header__ *iterator = heap->head;
    printf("Heap address %p Heap size %lu Header size: %lu - diff:%lld :\n", (void*)heap, sizeof(Heap__), sizeof(Header__), calc_ptrs_distance(heap, heap->head));
    printf("Heap pages: %lu. Heap headers: %lu. Heap control sum: %lu.\n", heap->pages, heap->headers_allocated, heap->control_sum);
    for (int i = 1; iterator; iterator = iterator->next, i++) {
        printf("Header %d address: %p, user address: %p user size: %lu - diff: %lld is free: %d\n",
            i, (void*)iterator, (void*)iterator->user_memptr, iterator->mem_size,
            calc_ptrs_distance(iterator->next, iterator), iterator->is_free);
    }
}
