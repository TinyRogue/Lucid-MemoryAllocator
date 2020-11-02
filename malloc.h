#ifndef MALLOC_H
#define MALLOC_H

#include <stdlib.h> /* For size_t, could be replaced with <stddef.h>, <stdio.h>, <string.h>, <time.h>, <wchar.h> */
#include <stdbool.h> /* For bool type */
#include <string.h> /* For memcpy() */
#include <stdio.h> /* For logging with printf funcs  */
#include "custom_unistd.h" /* For (custom_)sbrk function, required for project */


#define FENCE_LENGTH 0x3
#define KB 0x400
#define PAGE_SIZE 0x1000
#define META_S sizeof(Header__)

#define SBRK_FAIL (void*)-1
#define HEAP_INIT_FAIL -1

struct header_t {
    char l_fence[FENCE_LENGTH];
    struct header_t *prev;
    struct header_t *next;
    size_t memory_size;
    bool is_free;
    unsigned char : 1;
    char r_fence[FENCE_LENGTH];
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
enum pointer_type_t get_pointer_type(const void* const pointer);

#endif
