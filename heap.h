#ifndef HEAP_H
#define HEAP_H

#include <stdlib.h>                 /* For size_t, could be replaced with <stddef.h>, <stdio.h>, <string.h>, <time.h>, <wchar.h> */
#include <stdbool.h>                /* For bool type */
#include <string.h>                 /* For memcpy() */
#include <stdio.h>                  /* For logging with printf funcs */
#include <inttypes.h>               /* For uintptr_t */
#include "custom_unistd.h"          /* For (custom_)sbrk function, required for project */
#include "display_dependencies.h"   /* For colourful terminal messages */


#define FENCE_LENGTH 0x3
#define MY_PAGE_SIZE 0x1000
#define CONTROL_STRUCT_SIZE sizeof(Header__)
#define HEADER_SIZE(size) (CONTROL_STRUCT_SIZE + (size) + 2 * FENCE_LENGTH)

#define SBRK_FAIL (void*)(-1)
#define HEAP_INIT_FAIL (-1)
#define REQUEST_SPACE_FAIL HEAP_INIT_FAIL

#define HEAP_CORRUPTED 1
#define HEAP_UNINITIALIZED 2
#define HEAP_CONTROL_STRUCT_BLUR 3

struct header_t {
    struct header_t *prev;
    struct header_t *next;
    size_t mem_size;
    short is_free;
    void *user_mem_ptr;
    long long control_sum;
} __attribute__((packed));

typedef struct header_t Header__;

struct heap_t {
    size_t control_sum;
    size_t pages;
    size_t headers_allocated;
    Header__ *head;
} __attribute__((packed));

typedef struct heap_t Heap__;

typedef enum pointer_type_t {
    pointer_null,
    pointer_heap_corrupted,
    pointer_control_block,
    pointer_inside_fences,
    pointer_inside_data_block,
    pointer_unallocated,
    pointer_valid
} pointer_type_t;


int heap_setup(void);
int heap_validate(void);
void heap_clean(void);

void* heap_malloc(size_t size);
void* heap_calloc(size_t number, size_t size);
void* heap_realloc(void* memblock, size_t count);
void heap_free(void* memblock);

void* heap_malloc_aligned(size_t count);
void* heap_calloc_aligned(size_t number, size_t size);
void* heap_realloc_aligned(void* memblock, size_t size);

size_t heap_get_largest_used_block_size(void);
enum pointer_type_t get_pointer_type(const void* pointer);

#endif
