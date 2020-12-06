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
    remaining_header->is_free = remaining_header->is_free_ref = true;
    header_to_reduce->next = remaining_header;
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
//        assert(heap_validate() == 0 && "Allocating head failed::malloc");
        return heap->head->user_mem_ptr;
    }

    //Search for perfect block existing already on heap
    Header__ *iterator = heap->head;
    while (iterator) {
        if (iterator->is_free && iterator->mem_size == size) {
            iterator->is_free = iterator->is_free_ref = false;
//            assert(heap_validate() == 0 && "Allocating in same size failed::malloc");
            return iterator->user_mem_ptr;
        } else if (iterator->is_free && iterator->mem_size > HEADER_SIZE(size) + 1) { //At least one byte for splittedheader's user mem
            split_headers(iterator, size);
//            assert(heap_validate() == 0 && "Splitting failed::malloc");
            return iterator->user_mem_ptr;
        } else if (iterator->is_free && iterator->mem_size > size) {
            //Set new size and put new right fences, lost memory will be reverted on heap_free()
            iterator->mem_size = iterator->mem_size_ref = size;
            fill_fences(iterator);
            iterator->is_free = iterator->is_free_ref = false;
//            assert(heap_validate() == 0 && "Erasing next header failed::malloc");
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
//    assert(heap_validate() == 0 && "Creating at the end failed::malloc");
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
        handler->mem_size = handler->mem_size_ref = count;
        fill_fences(handler);
//        assert(heap_validate() == 0 && "Decreasing header failed::realloc");
        return handler->user_mem_ptr;
    } else if (count == handler->mem_size) {
//        assert(heap_validate() == 0 && "Same size failed::realloc");
        return handler->user_mem_ptr;
    }

    if (!handler->next) {
        long long left_mem = calc_ptrs_distance((uint8_t*)handler->user_mem_ptr + handler->mem_size, (uint8_t*)heap + heap->pages * MY_PAGE_SIZE - FENCE_LENGTH);
//        assert(left_mem >= 0 && "left_mem lower than zero::no next block::realloc");

        if (left_mem < (long long)count) {
            int pages_to_allocate = (int)((long long)count - left_mem) / MY_PAGE_SIZE + ((((long long)count - left_mem) / MY_PAGE_SIZE) % MY_PAGE_SIZE != 0);
            pages_to_allocate = pages_to_allocate == 0 ? 1 : pages_to_allocate;
            if (REQUEST_SPACE_FAIL == request_more_space(pages_to_allocate)) return NULL;
        }
//        assert(handler == last() && "isn't really last");
        handler->mem_size = handler->mem_size_ref = count;
        fill_fences(handler);
//        assert(heap_validate() == 0 && "Extending at the end failed");
        return handler->user_mem_ptr;
    } else if (handler->next->is_free && handler->mem_size + handler->next->mem_size > count) {
        Header__ *reduced = (Header__*)((uint8_t*)handler->user_mem_ptr + count + FENCE_LENGTH);
        long long reduced_size = (long long)(handler->mem_size + handler->next->mem_size - count);
//        assert(reduced_size > 0 && "Reduced header size under zero! Math went absolutely wrong.");
        Header__ copy;
        memcpy(&copy, handler->next, sizeof(Header__));
        reduced->next = copy.next;
        if (copy.next) copy.next->prev = reduced;
        reduced->prev= handler;
        reduced->mem_size = reduced->mem_size_ref = reduced_size;
        reduced->is_free = reduced->is_free_ref = true;
        reduced->user_mem_ptr = (uint8_t*)reduced + FENCE_LENGTH + CONTROL_STRUCT_SIZE;
        fill_fences(reduced);
        handler->next = reduced;
//        assert(heap_validate() == 0 && "Reducing failed::reduced block failed::realloc");
        handler->mem_size = handler->mem_size_ref = count;
        fill_fences(handler);
//        assert(heap_validate() == 0 && "Reducing failed::handler block failed::realloc");
        return handler->user_mem_ptr;
    } else if (handler->next->is_free && calc_ptrs_distance(handler->user_mem_ptr, (uint8_t*)handler->next->user_mem_ptr + handler->next->mem_size) > (long long)count) {
//        long long available_size = calc_ptrs_distance(handler->user_mem_ptr, (uint8_t*)handler->next->user_mem_ptr + handler->next->mem_size);
//        if (handler->next->next) assert(available_size < calc_ptrs_distance(handler->user_mem_ptr, handler->next->next) && "Size of block AB cannot be longer than AC!\nRealloc::erasing header B failed.");
        if (handler->next->next) handler->next->next->prev = handler;
        handler->next = handler->next->next;
        handler->mem_size = handler->mem_size_ref = count;
        fill_fences(handler);
        heap->control_sum -= 6;
        heap->headers_allocated--;
//        assert(heap_validate() == 0 && "Erasing header failed!::realloc");
        return handler->user_mem_ptr;
    }

    void *ptr = heap_malloc(count);
    if (!ptr) {
        return NULL;
    }

    memcpy(ptr, handler->user_mem_ptr, handler->mem_size);
    heap_free(handler->user_mem_ptr);
//    assert(heap_validate() == 0 && "Reallocating to end wrong! heap!");
    return ptr;
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

    Header__ *nxt = handler->next;
    Header__ *prv = handler->prev;

    if (prv && prv->is_free) handler = join_backward(handler);
    if (nxt && nxt->is_free) join_forward(handler);
    if (handler->next) {
        handler->mem_size = handler->mem_size_ref = calc_ptrs_distance(handler, handler->next) - HEADER_SIZE(0);
    }
    fill_fences(handler);
//    assert(heap_validate() == 0 && "Free failed");
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
//        assert(check_address(heap->head->user_mem_ptr) && "Address of head is not multiple of PAGE_SIZE");
//        assert(heap_validate() == 0 && "Allocating head failed::malloc_aligned");
        return heap->head->user_mem_ptr;
    }

    //Search for perfect block existing already on heap
    Header__ *iterator = heap->head;
    while (iterator) {
        if (iterator->is_free && check_address((uint8_t*)iterator + CONTROL_STRUCT_SIZE + FENCE_LENGTH) && iterator->mem_size == count) {
            iterator->is_free = iterator->is_free_ref = false;
//            assert(heap_validate() == 0 && "Allocating in same size failed::malloc_aligned");
            return iterator->user_mem_ptr;
        } else if (iterator->is_free && check_address((uint8_t*)iterator + CONTROL_STRUCT_SIZE + FENCE_LENGTH) && iterator->mem_size > HEADER_SIZE(count) + 1) { //At least one byte for splittedheader's user mem
            split_headers(iterator, count);
//            assert(check_address(iterator->user_mem_ptr) && "Splitted header::malloc_aligned");
//            assert(heap_validate() == 0 && "Splitting next header failed::malloc_aligned");
            return iterator->user_mem_ptr;
        } else if (iterator->is_free && check_address((uint8_t*)iterator + CONTROL_STRUCT_SIZE + FENCE_LENGTH) && iterator->mem_size > count) {
            //Set new size and put new right fences, lost memory will be reverted on heap_free()
            iterator->mem_size = iterator->mem_size_ref = count;
            fill_fences(iterator);
            iterator->is_free = iterator->is_free_ref = false;
//            assert(heap_validate() == 0 && "Erasing next header failed::malloc_aligned");
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
//    assert(check_address(new_header->user_mem_ptr) && "End address is not multiple of PAGE SIZE::malloc_aligned");
//    assert(heap_validate() == 0 && "Creating at the end failed::malloc_aligned");
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
        handler->mem_size = handler->mem_size_ref = size;
        fill_fences(handler);
//        assert(heap_validate() == 0 && "Decreasing header failed::realloc");
        return handler->user_mem_ptr;
    } else if (size == handler->mem_size) {
//        assert(heap_validate() == 0 && "Same size failed::realloc");
        return handler->user_mem_ptr;
    }

    if (!handler->next) {
        long long left_mem = calc_ptrs_distance((uint8_t *) handler->user_mem_ptr + handler->mem_size,
                                                (uint8_t *) heap + heap->pages * MY_PAGE_SIZE - FENCE_LENGTH);
//        assert(left_mem >= 0 && "left_mem lower than zero::no next block::realloc");

        if (left_mem < (long long) size) {
            int pages_to_allocate = (int) ((long long) size - left_mem) / MY_PAGE_SIZE +
                                    ((((long long) size - left_mem) / MY_PAGE_SIZE) % MY_PAGE_SIZE != 0);
            pages_to_allocate = pages_to_allocate == 0 ? 1 : pages_to_allocate;
            if (REQUEST_SPACE_FAIL == request_more_space(pages_to_allocate)) return NULL;
        }
//        assert(handler == last() && "isn't really last");
        handler->mem_size = handler->mem_size_ref = size;
        fill_fences(handler);
//        assert(heap_validate() == 0 && "Extending at the end failed");
        return handler->user_mem_ptr;

    } else if (calc_ptrs_distance(handler->user_mem_ptr, handler->next) - FENCE_LENGTH > (long long)size) {
        handler->mem_size = handler->mem_size_ref = size;
        fill_fences(handler);
        return handler->user_mem_ptr;
    } else if (handler->next->is_free && handler->mem_size + handler->next->mem_size > size) {
        Header__ *reduced = (Header__*)((uint8_t*)handler->user_mem_ptr + size + FENCE_LENGTH);
        long long reduced_size = (long long)(handler->mem_size + handler->next->mem_size - size);
//        assert(reduced_size > 0 && "Reduced header size under zero! Math went absolutely wrong.");
        Header__ copy;
        memcpy(&copy, handler->next, sizeof(Header__));
        reduced->next = copy.next;
        if (copy.next) copy.next->prev = reduced;
        reduced->prev= handler;
        reduced->mem_size = reduced->mem_size_ref = reduced_size;
        reduced->is_free = reduced->is_free_ref = true;
        reduced->user_mem_ptr = (uint8_t*)reduced + FENCE_LENGTH + CONTROL_STRUCT_SIZE;
        fill_fences(reduced);
        handler->next = reduced;
//        assert(heap_validate() == 0 && "Reducing failed::reduced block failed::realloc");
        handler->mem_size = handler->mem_size_ref = size;
        fill_fences(handler);
//        assert(heap_validate() == 0 && "Reducing failed::handler block failed::realloc");
        return handler->user_mem_ptr;
    } else if (handler->next->is_free && calc_ptrs_distance(handler->user_mem_ptr, (uint8_t*)handler->next->user_mem_ptr + handler->next->mem_size) > (long long)size) {
//        long long available_size = calc_ptrs_distance(handler->user_mem_ptr, (uint8_t*)handler->next->user_mem_ptr + handler->next->mem_size);
//        if (handler->next->next) assert(available_size < calc_ptrs_distance(handler->user_mem_ptr, handler->next->next) && "Size of block AB cannot be longer than AC!\nRealloc::erasing header B failed.");
        if (handler->next->next) handler->next->next->prev = handler;
        handler->next = handler->next->next;
        handler->mem_size = handler->mem_size_ref = size;
        fill_fences(handler);
        heap->control_sum -= 6;
        heap->headers_allocated--;
//        assert(heap_validate() == 0 && "Erasing header failed!::realloc");
        return handler->user_mem_ptr;
    }

    void *ptr = heap_malloc_aligned(size);
    if (!ptr) {
        return NULL;
    }

    memcpy(ptr, handler->user_mem_ptr, handler->mem_size);
    heap_free(handler->user_mem_ptr);
//    assert(heap_validate() == 0 && "Reallocating to end wrong! heap!");
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
               calc_ptrs_distance(iterator, iterator->next), iterator->is_free);
    }
    reset();
}

void display_mem() {
    Header__ *iterator = heap->head;
    magenta();
    for (int i = 1; iterator; iterator = iterator->next, i++) {
        red();
        printf("\n%d:\n", i);
        reset();
        blue();
        for (size_t j = 0; j < HEADER_SIZE(iterator->mem_size); j++) {
            if ((j >= CONTROL_STRUCT_SIZE && j < CONTROL_STRUCT_SIZE + FENCE_LENGTH) || (j >= HEADER_SIZE(iterator->mem_size) - FENCE_LENGTH && j < HEADER_SIZE(iterator->mem_size))) {
                red();
            }
            printf("%lu: '%c'|", j, ((char*)iterator)[j]);
            if ((j >= CONTROL_STRUCT_SIZE && j < CONTROL_STRUCT_SIZE + FENCE_LENGTH) || (j >= HEADER_SIZE(iterator->mem_size) - FENCE_LENGTH && j < HEADER_SIZE(iterator->mem_size))) {
                blue();
            }
        }
        reset();
    }
    reset();
    printf("\n\n");
}
