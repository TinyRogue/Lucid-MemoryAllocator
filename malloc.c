#include "malloc.h"

static Heap__ heap;


static void update_heap_info(const size_t size) {
    heap.headers_allocated++;
    heap.control_sum += 2 * FENCE_LENGTH;
}

static void fill_fences(Header__ *header) {
    for (int i = 0; i < FENCE_LENGTH; i++) {
        header->l_fence[i] = 'f';
        header->r_fence[i] = 'F';
    }
}

static void set_header(Header__ *header, const size_t size, Header__ *prv_nxt[2]) {
    header->is_free = true;
    header->memory_size = size;
    header->prev = *prv_nxt;
    header->next = *(++prv_nxt);
    fill_fences(header);
    update_heap_info(size);
}

int heap_setup(void) {
    if ((heap.head = custom_sbrk(PAGE_SIZE)) == SBRK_FAIL) return HEAP_INIT_FAIL;
    set_header(heap.head, PAGE_SIZE - META_S, (Header__*[2]){NULL, NULL});
    heap.pages++;
    return 0;
}

void heap_clean(void) {
    memset(heap.head, 0x0, heap.pages * PAGE_SIZE);
    custom_sbrk(-heap.pages * PAGE_SIZE);
    heap.head = NULL;
    heap.control_sum = heap.headers_allocated = heap.pages = 0;
}
