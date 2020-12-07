#include "heap.h"
#include "tested_declarations.h"



static Heap__ *heap = NULL;

static long long calc_ptrs_distance(void *previous, void *further) {
    if (!previous || !further) return 0;
    return (intptr_t)further - (intptr_t)previous;
}


int heap_setup(void) {
    if ((heap = (Heap__*)custom_sbrk(MY_PAGE_SIZE)) == SBRK_FAIL) return HEAP_INIT_FAIL;
    heap->pages = 1;
    heap->control_sum = 0;
    heap->headers_allocated = 0;
    heap->head = NULL;
    return 0;
}


static unsigned long compute_fences() {
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


static Header__* last() {
    if (!heap || !heap->head) return NULL;
    Header__ *iterator = heap->head;
    while (iterator->next) iterator = iterator->next;
    return iterator;
}


static long long compute_control_sum(void *pointer, size_t size) {
    long long control_sum = 0;
    uint8_t *ptr = (uint8_t *)pointer;
    for (size_t i = 0; i < size; i++) {
        control_sum += ptr[i];
    }
    return control_sum;
}


static void update_header_control_sum(Header__ *header) {
    header->control_sum = 0;
    header->control_sum = compute_control_sum(header, sizeof(Header__) - sizeof(header->control_sum));
}


static bool is_control_sum_valid() {
    Header__ *iterator = heap->head;
    if (!iterator) return true;
    while (iterator) {
        Header__ copy = *iterator;
        copy.control_sum = 0;
        long long control_sum = compute_control_sum(&copy, sizeof(copy) - sizeof(copy.control_sum));
        if (control_sum != iterator->control_sum) return false;
        iterator = iterator->next;
    }
    return true;
}


int heap_validate(void) {
    if (heap == NULL) return HEAP_UNINITIALIZED;
    if (!is_control_sum_valid()) return HEAP_CONTROL_STRUCT_BLUR;
    if (heap->control_sum != compute_fences()) return HEAP_CORRUPTED;
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
    update_header_control_sum(header);
}


static void set_header(Header__ *header, const size_t mem_size, Header__ *prv, Header__ *nxt) {
    header->is_free = false;
    header->mem_size = mem_size;
    header->prev = prv;
    header->next = nxt;
    header->user_mem_ptr = (uint8_t*)header + CONTROL_STRUCT_SIZE + FENCE_LENGTH;
    if (prv){
        prv->next = header;
        update_header_control_sum(prv);
    }
    if (nxt){
        nxt->prev = header, update_header_control_sum(nxt);
    }
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

    header_to_reduce->is_free = false;
    header_to_reduce->mem_size = new_mem_size;
    fill_fences(header_to_reduce);

    set_header(remaining_header, prior_mem_size - HEADER_SIZE(new_mem_size), header_to_reduce, header_to_reduce->next);
    remaining_header->is_free = true;
    header_to_reduce->next = remaining_header;
    update_header_control_sum(remaining_header);
    update_header_control_sum(header_to_reduce);
}

/* header - memory layout - control fences user_space fences  */
void* heap_malloc(size_t size) {
    if (size < 1 || heap_validate() || HEADER_SIZE(size) < size) return NULL;

    //Heap has no blocks at all
    if (!heap->head) {
        if (heap->pages * MY_PAGE_SIZE - sizeof(Heap__) < HEADER_SIZE(size)) {
            int pages_to_allocate = (int)((HEADER_SIZE(size) - (MY_PAGE_SIZE * heap->pages - sizeof(Heap__)))) / MY_PAGE_SIZE
                                    + ((HEADER_SIZE(size) - (MY_PAGE_SIZE * heap->pages - sizeof(Heap__))) % MY_PAGE_SIZE != 0);
            return REQUEST_SPACE_FAIL == request_more_space(pages_to_allocate) ? NULL : heap_malloc(size);
        }
        heap->head = (Header__*)((uint8_t*)heap + sizeof(Heap__));
        set_header(heap->head, size, NULL, NULL);
        return heap->head->user_mem_ptr;
    }

    //Search for perfect block existing already on heap
    Header__ *iterator = heap->head;
    while (iterator) {
        if (iterator->is_free && iterator->mem_size == size) {
            iterator->is_free = false;
            update_header_control_sum(iterator);
            return iterator->user_mem_ptr;
        } else if (iterator->is_free && iterator->mem_size > HEADER_SIZE(size) + 1) { //At least one byte for splittedheader's user mem
            split_headers(iterator, size);
            return iterator->user_mem_ptr;
        } else if (iterator->is_free && iterator->mem_size > size) {
            //Set new size and put new right fences, lost memory will be reverted on heap_free()
            iterator->mem_size = size;
            iterator->is_free = false;
            fill_fences(iterator);
            return iterator->user_mem_ptr;
        }
        iterator = iterator->next;
    }

    //Create header between last node and end of heap memory
    Header__ *last_header = last();

    long long free_mem_size = calc_ptrs_distance((uint8_t*)last_header->user_mem_ptr + last_header->mem_size + FENCE_LENGTH, (uint8_t*)heap + heap->pages * MY_PAGE_SIZE);

    if (free_mem_size <= (long long)(HEADER_SIZE(size))) {
        int pages_to_allocate = (int)((HEADER_SIZE(size) - free_mem_size) / MY_PAGE_SIZE + (int)(((HEADER_SIZE(size) - free_mem_size)) % PAGE_SIZE != 0));
        pages_to_allocate = pages_to_allocate == 0 ? 1 : pages_to_allocate;
        return REQUEST_SPACE_FAIL == request_more_space(pages_to_allocate) ? NULL : heap_malloc(size);
    }

    set_header((Header__*)((uint8_t*)last_header->user_mem_ptr + last_header->mem_size + FENCE_LENGTH), size, last_header, NULL);
    return last()->user_mem_ptr;
}


void* heap_calloc(size_t number, size_t size) {
    void *handler = heap_malloc(number * size);
    if (!handler) return NULL;
    memset(handler, 0x0, number * size);
    return handler;
}


void* heap_realloc(void* memblock, size_t count) {
    if ((long long)count < 0 || (!memblock && !count) || heap_validate()) return NULL;
    if (!memblock) return heap_malloc(count);
    if (get_pointer_type(memblock) != pointer_valid) return NULL;
    if (count == 0) return heap_free(memblock), NULL;
    Header__ *handler = (Header__*)((uint8_t*)memblock - FENCE_LENGTH - CONTROL_STRUCT_SIZE);

    if (count < handler->mem_size) {
        handler->mem_size = count;
        fill_fences(handler);
        return handler->user_mem_ptr;
    } else if (count == handler->mem_size) {
        update_header_control_sum(handler);
        return handler->user_mem_ptr;
    }

    if (!handler->next) {
        long long left_mem = calc_ptrs_distance((uint8_t*)handler->user_mem_ptr + handler->mem_size, (uint8_t*)heap + heap->pages * MY_PAGE_SIZE - FENCE_LENGTH);

        if (left_mem < (long long)count) {
            int pages_to_allocate = (int)((long long)count - left_mem) / MY_PAGE_SIZE + ((((long long)count - left_mem) / MY_PAGE_SIZE) % MY_PAGE_SIZE != 0);
            pages_to_allocate = pages_to_allocate == 0 ? 1 : pages_to_allocate;
            if (REQUEST_SPACE_FAIL == request_more_space(pages_to_allocate)) return NULL;
        }

        handler->mem_size = count;
        fill_fences(handler);
        return handler->user_mem_ptr;
    } else if (handler->next->is_free && handler->mem_size + handler->next->mem_size > count) {
        Header__ *reduced = (Header__*)((uint8_t*)handler->user_mem_ptr + count + FENCE_LENGTH);
        long long reduced_size = (long long)(handler->mem_size + handler->next->mem_size - count);
        Header__ copy;

        memcpy(&copy, handler->next, sizeof(Header__));
        reduced->next = copy.next;

        if (copy.next) {
            copy.next->prev = reduced;
            update_header_control_sum(copy.next);
        }
        reduced->prev= handler;
        reduced->mem_size = reduced_size;
        reduced->is_free = true;
        reduced->user_mem_ptr = (uint8_t*)reduced + FENCE_LENGTH + CONTROL_STRUCT_SIZE;
        fill_fences(reduced);

        handler->next = reduced;
        handler->mem_size = count;
        fill_fences(handler);

        return handler->user_mem_ptr;
    } else if (handler->next->is_free && calc_ptrs_distance(handler->user_mem_ptr, (uint8_t*)handler->next->user_mem_ptr + handler->next->mem_size) > (long long)count) {

        if (handler->next->next){
            handler->next->next->prev = handler;
            update_header_control_sum(handler->next->next);
        }

        handler->next = handler->next->next;
        handler->mem_size = count;
        fill_fences(handler);
        heap->control_sum -= 6;
        heap->headers_allocated--;
        return handler->user_mem_ptr;
    }

    void *ptr = heap_malloc(count);
    if (!ptr) {
        return NULL;
    }

    memcpy(ptr, handler->user_mem_ptr, handler->mem_size);
    heap_free(handler->user_mem_ptr);
    update_header_control_sum((Header__ *) ((uint8_t *) ptr - CONTROL_STRUCT_SIZE - FENCE_LENGTH));
    return ptr;
}


static void join_forward(Header__ *current) {
    Header__ *nxt = current->next;
    current->mem_size += HEADER_SIZE(nxt->mem_size);
    current->next = nxt->next;
    if (nxt->next) {
        nxt->next->prev = current;
        update_header_control_sum(nxt->next);
    }
    update_header_control_sum(current);
    heap->control_sum -= FENCE_LENGTH * 2;
    heap->headers_allocated--;
}


static Header__* join_backward(Header__ *current) {
    Header__ *prv = current->prev;
    prv->mem_size += HEADER_SIZE(current->mem_size);
    prv->next = current->next;
    if (current->next) {
        current->next->prev = prv;
        update_header_control_sum(current->next);
    }
    update_header_control_sum(current);
    heap->control_sum -= FENCE_LENGTH * 2;
    heap->headers_allocated--;
    return prv;
}

/* From [...cccfffUUUFFFcccfffUUUUFFF...] to [...cccfffUUUUUUUUUUUUUUUUFFF...] */
void heap_free(void* memblock) {
    if (HEAP_UNINITIALIZED == heap_validate() || !memblock || get_pointer_type(memblock) != pointer_valid) return;

    Header__ *handler = (Header__*)((uint8_t*)memblock - FENCE_LENGTH - CONTROL_STRUCT_SIZE);
    handler->is_free = true;

    Header__ *nxt = handler->next;
    Header__ *prv = handler->prev;

    if (prv && prv->is_free) handler = join_backward(handler);
    if (nxt && nxt->is_free) join_forward(handler);
    if (handler->next) {
        handler->mem_size = calc_ptrs_distance(handler, handler->next) - HEADER_SIZE(0);
    }
    fill_fences(handler);
}


static bool check_address(const void * const ptr) {
    return ((intptr_t)ptr & (intptr_t)(PAGE_SIZE - 1)) == 0;
}


void* heap_malloc_aligned(size_t count) {
    if (count < 1 || heap_validate() || HEADER_SIZE(count) < count) return NULL;
    //Heap has no blocks at all
    if (!heap->head) {
        if (heap->pages * MY_PAGE_SIZE - sizeof(Heap__) < HEADER_SIZE(count) + MY_PAGE_SIZE) {
            int pages_to_allocate = (int)HEADER_SIZE(count) / MY_PAGE_SIZE + ((int)HEADER_SIZE(count) % PAGE_SIZE != 0);
            if (REQUEST_SPACE_FAIL == request_more_space(pages_to_allocate)) return NULL;
        }

        heap->head = (Header__*)((uint8_t*)heap + MY_PAGE_SIZE - FENCE_LENGTH - CONTROL_STRUCT_SIZE);
        set_header(heap->head, count, NULL, NULL);
        return heap->head->user_mem_ptr;
    }

    //Search for perfect block existing already on heap
    Header__ *iterator = heap->head;
    while (iterator) {
        if (iterator->is_free && check_address((uint8_t*)iterator + CONTROL_STRUCT_SIZE + FENCE_LENGTH) && iterator->mem_size == count) {
            iterator->is_free = false;
            fill_fences(iterator);
            return iterator->user_mem_ptr;
        } else if (iterator->is_free && check_address((uint8_t*)iterator + CONTROL_STRUCT_SIZE + FENCE_LENGTH) && iterator->mem_size > HEADER_SIZE(count) + 1) { //At least one byte for splittedheader's user mem
            split_headers(iterator, count);
            return iterator->user_mem_ptr;
        } else if (iterator->is_free && check_address((uint8_t*)iterator + CONTROL_STRUCT_SIZE + FENCE_LENGTH) && iterator->mem_size > count) {
            //Set new size and put new right fences, lost memory will be reverted on heap_free()
            iterator->mem_size = count;
            iterator->is_free = false;
            fill_fences(iterator);
            return iterator->user_mem_ptr;
        }
        iterator = iterator->next;
    }

    //Create header between last node and end of heap memory
    Header__ *last_header = last();
    Header__ *end_of_last = (Header__*)((uint8_t*)last_header->user_mem_ptr + last_header->mem_size + FENCE_LENGTH);
    long long free_mem_size = calc_ptrs_distance(end_of_last, (uint8_t*)heap + heap->pages * MY_PAGE_SIZE);

    int pages_to_allocate = HEADER_SIZE(count) / MY_PAGE_SIZE;
    pages_to_allocate += HEADER_SIZE(count) % MY_PAGE_SIZE != 0;
    bool is_smaller = false;
    if (free_mem_size < (long long)CONTROL_STRUCT_SIZE + FENCE_LENGTH) {
        pages_to_allocate += 1;
        is_smaller = true;
    }

    if (REQUEST_SPACE_FAIL == request_more_space(pages_to_allocate)) return NULL;
    Header__ *new_header = (Header__*)((uint8_t*)end_of_last + free_mem_size - FENCE_LENGTH - CONTROL_STRUCT_SIZE + is_smaller * PAGE_SIZE);

    set_header(new_header, count, last_header, NULL);
    return new_header->user_mem_ptr;
}


void* heap_calloc_aligned(size_t number, size_t size) {
    void *handler = heap_malloc_aligned(number * size);
    if (!handler) return NULL;
    memset(handler, 0x0, number * size);
    return handler;
}


void* heap_realloc_aligned(void* memblock, size_t size) {
    if ((long long)size < 0 || (!memblock && !size) || heap_validate()) return NULL;
    if (!memblock) return heap_malloc_aligned(size);
    if (get_pointer_type(memblock) != pointer_valid) return NULL;
    if (size == 0) return heap_free(memblock), NULL;
    Header__ *handler = (Header__*)((uint8_t*)memblock - FENCE_LENGTH - CONTROL_STRUCT_SIZE);

    if (size < handler->mem_size) {
        handler->mem_size = size;
        fill_fences(handler);
        return handler->user_mem_ptr;
    } else if (size == handler->mem_size) {
        return handler->user_mem_ptr;
    }

    if (!handler->next) {
        long long left_mem = calc_ptrs_distance((uint8_t *) handler->user_mem_ptr + handler->mem_size,
                                                (uint8_t *) heap + heap->pages * MY_PAGE_SIZE - FENCE_LENGTH);

        if (left_mem < (long long) size) {
            int pages_to_allocate = (int) ((long long) size - left_mem) / MY_PAGE_SIZE +
                                    ((((long long) size - left_mem) / MY_PAGE_SIZE) % MY_PAGE_SIZE != 0);
            pages_to_allocate = pages_to_allocate == 0 ? 1 : pages_to_allocate;
            if (REQUEST_SPACE_FAIL == request_more_space(pages_to_allocate)) return NULL;
        }
        handler->mem_size = size;
        fill_fences(handler);
        return handler->user_mem_ptr;

    } else if (calc_ptrs_distance(handler->user_mem_ptr, handler->next) - FENCE_LENGTH > (long long)size) {
        handler->mem_size = size;
        fill_fences(handler);
        return handler->user_mem_ptr;
    } else if (handler->next->is_free && handler->mem_size + handler->next->mem_size > size) {
        Header__ *reduced = (Header__*)((uint8_t*)handler->user_mem_ptr + size + FENCE_LENGTH);
        long long reduced_size = (long long)(handler->mem_size + handler->next->mem_size - size);
        Header__ copy;

        memcpy(&copy, handler->next, sizeof(Header__));
        reduced->next = copy.next;

        if (copy.next) {
            copy.next->prev = reduced;
            update_header_control_sum(copy.next);
        }

        reduced->prev= handler;
        reduced->mem_size = reduced_size;
        reduced->is_free = true;
        reduced->user_mem_ptr = (uint8_t*)reduced + FENCE_LENGTH + CONTROL_STRUCT_SIZE;
        fill_fences(reduced);

        handler->next = reduced;
        handler->mem_size = size;
        fill_fences(handler);
        return handler->user_mem_ptr;
    } else if (handler->next->is_free && calc_ptrs_distance(handler->user_mem_ptr, (uint8_t*)handler->next->user_mem_ptr + handler->next->mem_size) > (long long)size) {

        if (handler->next->next) {
            handler->next->next->prev = handler;
            update_header_control_sum(handler->next->next);
        }

        handler->next = handler->next->next;
        handler->mem_size = size;
        fill_fences(handler);

        heap->control_sum -= 6;
        heap->headers_allocated--;
        return handler->user_mem_ptr;
    }

    void *ptr = heap_malloc_aligned(size);
    if (!ptr) {
        return NULL;
    }

    memcpy(ptr, handler->user_mem_ptr, handler->mem_size);
    heap_free(handler->user_mem_ptr);
    update_header_control_sum((Header__ *) ((uint8_t *) ptr - CONTROL_STRUCT_SIZE - FENCE_LENGTH));
    return ptr;
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
    if (!heap->head) return pointer_unallocated;
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
