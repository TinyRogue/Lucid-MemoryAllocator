#include "heap.h"
#include "tested_declarations.h"



static Heap__ *heap = NULL;

static long long calc_ptrs_distance(void *ptr1, void *ptr2) {
    if (!ptr1 || !ptr2) return 0;
    return llabs((intptr_t)ptr1 - (intptr_t)ptr2);
}


int heap_setup(void) {
    if ((heap = (Heap__*)custom_sbrk(MY_PAGE_SIZE)) == SBRK_FAIL) return HEAP_INIT_FAIL;
    heap->pages = 1;
    heap->control_sum = 0;
    heap->headers_allocated = 0;
    heap->head = NULL;
    return 0;
}


static unsigned long count_headers() {
    Header__ *iterator = heap->head;
    if (!iterator) return 0;

    unsigned long counter = 1;
    while (iterator->next) {
        iterator = iterator->next;
        counter++;
    }
    return counter;
}


static unsigned long compute_control_sum() {
    Header__ *iterator = heap->head; //iterator is equal to left fence
    if (!iterator) return 0;

    unsigned long counter = 0;
    while (iterator) {
        for (int i = 0; i < FENCE_LENGTH; i++) {
            if (*((uint8_t*)iterator + i + CONTROL_STRUCT_SIZE) == 'f') counter++;
            if (*((uint8_t*)iterator->user_mem_ptr + i + iterator->mem_size) == 'F') counter++;
        }
        iterator = iterator->next;
    }
    return counter;
}


static bool contains(Header__* to_check) {
    if (!to_check) return false;

    Header__ *iterator = heap->head;
    while (iterator) {
        if (iterator == to_check) return true;
        iterator = iterator->next;
    }
    return false;
}


static Header__* last() {
    if (!heap || !heap->head) return NULL;
    Header__ *iterator = heap->head;
    while (iterator->next) iterator = iterator->next;
    return iterator;
}


static bool reversed_contains(Header__* to_check) {
    if (!to_check) return false;

    Header__ *reversed_iterator = last();
    while (reversed_iterator) {
        if (reversed_iterator == to_check) return true;
        reversed_iterator = reversed_iterator->prev;
    }
    return false;
}


static bool are_heap_ptrs_valid() {
    Header__ *iterator = heap->head;
    if (!iterator) return true;
    while (iterator) {
        if ((intptr_t)heap > (intptr_t)iterator
            || (intptr_t)((uint8_t*)heap + heap->pages * PAGE_SIZE) < (intptr_t)iterator
            || (intptr_t)((uint8_t*)heap + heap->pages * PAGE_SIZE) < (intptr_t)((uint8_t*)iterator + HEADER_SIZE(iterator->mem_size))) {
            return false;
        }
        iterator = iterator->next;
    }

    Header__ *reversed_iterator = last();
    while (reversed_iterator->prev) {
        if (!contains(reversed_iterator->prev)) return false;
        reversed_iterator = reversed_iterator->prev;
    }

    iterator = heap->head;
    while (iterator) {
        if (!reversed_contains(iterator)) return false;
        iterator = iterator->next;
    }
    return true;
}


static bool are_user_ptrs_valid() {
    Header__ *iterator = heap->head;
    if (!iterator) return true;
    while (iterator) {
        if (calc_ptrs_distance((uint8_t*)iterator + CONTROL_STRUCT_SIZE + FENCE_LENGTH, iterator->user_mem_ptr) != 0) return false;
        iterator = iterator->next;
    }
    return true;
}


static bool is_free_valid() {
    Header__ *iterator = heap->head;
    if (!iterator) return true;
    while (iterator) {
        if (iterator->is_free_ref != iterator->is_free) return false;
        iterator = iterator->next;
    }
    return true;
}


static bool is_mem_size_valid() {
    Header__ *iterator = heap->head;
    if (!iterator) return true;
    while (iterator) {
        if (iterator->mem_size_ref != iterator->mem_size) return false;
        iterator = iterator->next;
    }
    return true;
}


int heap_validate(void) {
    if (heap == NULL) return HEAP_UNINITIALIZED;
    if (!are_heap_ptrs_valid()) return HEAP_CONTROL_STRUCT_BLUR;
    if (heap->headers_allocated != count_headers()) return HEAP_CONTROL_STRUCT_BLUR;
    if (!are_user_ptrs_valid()) return HEAP_CONTROL_STRUCT_BLUR;
    if (!is_free_valid()) return HEAP_CONTROL_STRUCT_BLUR;
    if (!is_mem_size_valid()) return HEAP_CONTROL_STRUCT_BLUR;
    if (heap->control_sum != compute_control_sum()) return HEAP_CORRUPTED;
    return 0;
}


void heap_clean(void) {
    if (HEAP_UNINITIALIZED == heap_validate()) return;
    unsigned long mem_size = heap->pages * MY_PAGE_SIZE;
    memset(heap, 0x0, mem_size);
    heap->head = NULL;
    heap = NULL;
    custom_sbrk(-mem_size);
}


static int request_more_space(int pages_to_allocate) {
    uint8_t *handler = custom_sbrk(MY_PAGE_SIZE * pages_to_allocate);
    if (handler == (void*)-1) return REQUEST_SPACE_FAIL;
    heap->pages += pages_to_allocate;
    return 0;
}


static void update_heap_info() {
    heap->headers_allocated++;
    heap->control_sum += 2 * FENCE_LENGTH;
}


static void fill_fences(Header__ *header) {
    for (int i = 0; i < FENCE_LENGTH; i++) {
        *((uint8_t*)header + CONTROL_STRUCT_SIZE + i) = 'f';
        *((uint8_t*)header->user_mem_ptr + header->mem_size + i) = 'F';
    }
}


static void set_header(Header__ *header, const size_t mem_size, Header__ *prv, Header__ *nxt) {
    header->is_free = header->is_free_ref = false;
    header->mem_size = header->mem_size_ref = mem_size;
    header->prev = prv;
    header->next = nxt;
    header->user_mem_ptr = (uint8_t*)header + CONTROL_STRUCT_SIZE + FENCE_LENGTH;
    if (prv) prv->next = header;
    if (nxt) nxt->prev = header;
    fill_fences(header);
    update_heap_info();
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
static void split_headers(Header__ *header_to_reduce, size_t new_mem_size) {
    size_t prior_mem_size = header_to_reduce->mem_size;
    Header__ *remaining_header = (Header__*)((uint8_t*)header_to_reduce->user_mem_ptr + new_mem_size + FENCE_LENGTH);

    header_to_reduce->is_free = header_to_reduce->is_free_ref = false;
    header_to_reduce->mem_size = header_to_reduce->mem_size_ref = new_mem_size;
    fill_fences(header_to_reduce);

    set_header(remaining_header, prior_mem_size - HEADER_SIZE(new_mem_size), header_to_reduce, header_to_reduce->next);
    blue();
    printf("Splitting header: size %lu, address %p into: size> %lu %lu, addresses> %p %p. Distance: %lld\n", prior_mem_size, (void*)header_to_reduce, header_to_reduce->mem_size, remaining_header->mem_size, (void*)header_to_reduce, (void*)remaining_header, calc_ptrs_distance(header_to_reduce, remaining_header));
    reset();
    remaining_header->is_free = remaining_header->is_free_ref = true;
    header_to_reduce->next = remaining_header;
}

/* header - memory layout - control fences user_space fences  */
void* heap_malloc(size_t size) {
    if (size < 1 || heap_validate() || HEADER_SIZE(size) < size) return NULL;

    red();
    printf("Requested malloc for size: %lu\n", size);
    reset();

    //Heap has no blocks at all
    if (!heap->head) {
        if (heap->pages * MY_PAGE_SIZE - sizeof(Heap__) < HEADER_SIZE(size)) {
            int pages_to_allocate = (int)((HEADER_SIZE(size) - (MY_PAGE_SIZE * heap->pages - sizeof(Heap__)))) / PAGE_SIZE
                    + ((HEADER_SIZE(size) - (MY_PAGE_SIZE * heap->pages - sizeof(Heap__))) % PAGE_SIZE != 0);
            return REQUEST_SPACE_FAIL == request_more_space(pages_to_allocate) ? NULL : heap_malloc(size);
        }
        heap->head = (Header__*)((uint8_t*)heap + sizeof(Heap__));
        set_header(heap->head, size, NULL, NULL);
        green();
        printf("First block!\n");
        printf("Allocating at: %p - user memory at: %p\n", (void*)heap->head, (void*)heap->head->user_mem_ptr);
        reset();
        display_heap();
        return heap->head->user_mem_ptr;
    }

    //Search for perfect block existing already on heap
    Header__ *iterator = heap->head;
    while (iterator) {
        if (iterator->is_free && iterator->mem_size == size) {
            iterator->is_free = iterator->is_free_ref = false;
            green();
            printf("Found perfect, free block with equal size!\n");
            printf("Allocating at: %p - user memory at: %p\n", (void*)iterator, (void*)iterator->user_mem_ptr);
            reset();
            display_heap();
            return iterator->user_mem_ptr;
        } else if (iterator->is_free && iterator->mem_size > HEADER_SIZE(size) + 1) { //At least one byte for splittedheader's user mem
            green();
            printf("Found smaller, 'spiltable', free block!\n");
            printf("Allocating at: %p - user memory at: %p\n", (void*)iterator, (void*)iterator->user_mem_ptr);
            reset();
            split_headers(iterator, size);
            display_heap();
            return iterator->user_mem_ptr;
        } else if (iterator->is_free && iterator->mem_size > size) {
            green();
            printf("Found smaller, 'not-spiltable', free block!\n");
            printf("Allocating at: %p - user memory at: %p\n", (void*)iterator, (void*)iterator->user_mem_ptr);
            reset();
            //Set new size and put new right fences, lost memory will be reverted on heap_free()
            iterator->mem_size = iterator->mem_size_ref = size;
            for (int i = 0; i < FENCE_LENGTH; i++) {
                *((uint8_t*)iterator->user_mem_ptr + iterator->mem_size + i) = 'F';
            }
            iterator->is_free = iterator->is_free_ref = false;
            display_heap();
            return iterator->user_mem_ptr;
        }
        iterator = iterator->next;
    }

    //Create header between last node and end of heap memory
    Header__ *last_header = last();

    long long free_mem_size = calc_ptrs_distance((uint8_t*)heap + heap->pages * MY_PAGE_SIZE, (uint8_t*)last_header->user_mem_ptr + last_header->mem_size + FENCE_LENGTH);
    if (free_mem_size <= (long long)(HEADER_SIZE(size))) {
        bold_cyan();
        printf("Too little space. Upsizing.\nHaving: %lld Required: %lld - diff: %lld\n",
               free_mem_size, (long long)(HEADER_SIZE(size)), free_mem_size - (long long)HEADER_SIZE(size));
        reset();

        int pages_to_allocate = (int)((HEADER_SIZE(size) - free_mem_size) / MY_PAGE_SIZE + (int)(((HEADER_SIZE(size) - free_mem_size)) % PAGE_SIZE != 0));
        return REQUEST_SPACE_FAIL == request_more_space(pages_to_allocate) ? NULL : heap_malloc(size);
    }

    green();
    printf("Allocating at the end!\n");
    printf("Allocating at: %p - user memory at: %p\n", (void*)((uint8_t*)last_header->user_mem_ptr + last_header->mem_size + FENCE_LENGTH), (void*)((uint8_t*)last_header->user_mem_ptr + last_header->mem_size + FENCE_LENGTH + CONTROL_STRUCT_SIZE + FENCE_LENGTH));
    reset();
    set_header((Header__*)((uint8_t*)last_header->user_mem_ptr + last_header->mem_size + FENCE_LENGTH), size, last_header, NULL);
    display_heap();
    return last()->user_mem_ptr;
}


void* heap_calloc(size_t number, size_t size) {
    red();
    printf("Requested calloc for %lu memory.\n", number * size);
    reset();

    void *handler = heap_malloc(number * size);
    if (!handler) return NULL;
    memset(handler, 0x0, number * size);
    return handler;
}

//TODO: func to implement
void* heap_realloc(void* memblock, size_t count) {
    return NULL;
}


static void join_forward(Header__ *current) {
    Header__ *nxt = current->next;
    current->mem_size += HEADER_SIZE(nxt->mem_size);
    current->mem_size_ref = current->mem_size;
    current->next = nxt->next;
    if (nxt->next) {
        nxt->next->prev = current;
    }
    heap->control_sum -= FENCE_LENGTH * 2;
    heap->headers_allocated--;
}


static Header__* join_backward(Header__ *current) {
    Header__ *prv = current->prev;
    prv->mem_size += HEADER_SIZE(current->mem_size);
    prv->mem_size_ref = prv->mem_size;
    prv->next = current->next;
    if (current->next) {
        current->next->prev = prv;
    }
    heap->control_sum -= FENCE_LENGTH * 2;
    heap->headers_allocated--;
    return prv;
}

/* From [...cccfffUUUFFFcccfffUUUUFFF...] to [...cccfffUUUUUUUUUUUUUUUUFFF...] */
void heap_free(void* memblock) {
    if (HEAP_UNINITIALIZED == heap_validate() || !memblock || get_pointer_type(memblock) != pointer_valid) return;
    Header__ *handler = (Header__*)((uint8_t*)memblock - FENCE_LENGTH - CONTROL_STRUCT_SIZE);
    handler->is_free = handler->is_free_ref = true;

    bold_cyan();
    printf("Freeing %p\n", (void*)handler);
    reset();

    Header__ *nxt = handler->next;
    Header__ *prv = handler->prev;

    if (prv && prv->is_free) handler = join_backward(handler);
    if (nxt && nxt->is_free) join_forward(handler);
    if (handler->next) {
        handler->mem_size = handler->mem_size_ref = calc_ptrs_distance(handler->next, handler) - HEADER_SIZE(0);
    }
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

    if (!heap || !heap->head || heap_validate()) return 0;

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


enum pointer_type_t get_pointer_type(const void* const pointer) {
    if (!pointer) return pointer_null;
    if (heap_validate() == HEAP_CORRUPTED) return pointer_heap_corrupted;

    intptr_t ptr_handler = (intptr_t)pointer;

    if (ptr_handler < (intptr_t)heap) return pointer_unallocated;
    if (ptr_handler < (intptr_t)((uint8_t*)heap + sizeof(Heap__))) return pointer_control_block;

    Header__ *iterator = heap->head;
    while (iterator->next && (intptr_t)iterator->next <= ptr_handler) {
        iterator = iterator->next;
    }

    intptr_t control_block = (intptr_t)((uint8_t*)iterator + CONTROL_STRUCT_SIZE);
    intptr_t left_fences = (intptr_t)((uint8_t*) iterator + FENCE_LENGTH + CONTROL_STRUCT_SIZE);
    intptr_t user_mem = (intptr_t)((uint8_t*)iterator->user_mem_ptr + iterator->mem_size);
    intptr_t right_fences = (intptr_t)((uint8_t*)iterator->user_mem_ptr + iterator->mem_size + FENCE_LENGTH);

    if (ptr_handler < control_block) return pointer_control_block;
    else if (ptr_handler < left_fences && !iterator->is_free) return pointer_inside_fences; // NOLINT(bugprone-branch-clone)
    else if (ptr_handler == (intptr_t)iterator->user_mem_ptr && !iterator->is_free) return pointer_valid;
    else if (ptr_handler == (intptr_t)iterator->user_mem_ptr) return pointer_unallocated;  // NOLINT(bugprone-branch-clone)
    else if (ptr_handler < user_mem && !iterator->is_free) return pointer_inside_data_block;
    else if (ptr_handler < user_mem) return pointer_unallocated;
    else if (ptr_handler < right_fences && !iterator->is_free) return pointer_inside_fences;
    return pointer_unallocated;
}


void display_heap() {
    if (!heap || !heap->head) return;

    Header__ *iterator = heap->head;
    bred();
    printf("> Heap address %p\n", (void*)heap);
    bold_yellow();
    printf("> Heap pages: %lu. Heap headers: %lu. Heap control sum: %lu.\n", heap->pages, heap->headers_allocated, heap->control_sum);

    blue();
    for (int i = 1; iterator; iterator = iterator->next, i++) {
        printf("\033[1;34m> Header %d address: \033[1;33m%p\033[1;34m, user address: \033[1;33m%p\033[1;34m user size: \033[0;32m%lu\033[1;34m - diff: \033[1;33m%lld\033[1;34m is free: \033[0;32m%d\n",
               i, (void*)iterator,
               (void*)iterator->user_mem_ptr,
               iterator->mem_size,
               calc_ptrs_distance(iterator->next, iterator), iterator->is_free);
    }
    reset();
}

