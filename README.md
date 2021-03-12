# Lucid-MemoryAllocator
This project is simple implementation of memory allocator.

## Table of contents
* [About](#about)
* [Setup](#setup)
* [API Depiction](#setup)

## About
This project consist of:
* __malloc()__
* __calloc()__
* __realloc()__
* __free()__

standard functions implementation. The goal of this project was to dive into dynamic memory management.
	
## Setup
I make use of **custom sbrk()** function instead of the built-in one. To avoid corruption please, do not change it.

Compile project using `make c` inside the project catalog.
To use implemented functions just add `#include "heap.h"`

## API depiction

* ```int heap_setup(void);```

Initializes the heap.
Invoke this function once before using any of mentioned below.

* ```int heap_validate(void);```

This functions check if the heap is set up, validate the checksum for every block, checks if the fences are not breached and all memory blocks are accesible.

* ```void heap_clean(void);```

Cleans and realese the heap memory to operating system.

* ```void* heap_malloc(size_t size);```

Implementation of [malloc()](https://man7.org/linux/man-pages/man3/malloc.3.html) function.

* ```void* heap_calloc(size_t number, size_t size);```

Implementation of [calloc()](https://man7.org/linux/man-pages/man3/malloc.3.html) function.

* ```void* heap_realloc(void* memblock, size_t count);```

Implementation of [realloc()](https://man7.org/linux/man-pages/man3/malloc.3.html) function.

* ```void heap_free(void* memblock);```

Implementation of [free()](https://man7.org/linux/man-pages/man3/malloc.3.html) function.

* ```void* heap_malloc_aligned(size_t count);```

Implementation of [malloc()](https://man7.org/linux/man-pages/man3/malloc.3.html) function, yet new block is always aligned to the beginning of the [page](https://en.wikipedia.org/wiki/Page_(computer_memory)).

* ```void* heap_calloc_aligned(size_t number, size_t size);```

Implementation of [calloc()](https://man7.org/linux/man-pages/man3/malloc.3.html) function, yet new block is always aligned to the beginning of the [page](https://en.wikipedia.org/wiki/Page_(computer_memory)).

* ```void* heap_realloc_aligned(void* memblock, size_t size);```

Implementation of [realloc()](https://man7.org/linux/man-pages/man3/malloc.3.html) function, yet new block is always aligned to the beginning of the [page](https://en.wikipedia.org/wiki/Page_(computer_memory)).

* ```size_t heap_get_largest_used_block_size(void);```

Returns the size of largest allocated block.

* ```enum pointer_type_t get_pointer_type(const void* pointer);```

Returns type of given pointer:

1. __pointer_null__ - pointer to NULL
2. __pointer_heap_corrupted__ - heap has internal error
3. __pointer_control_block__ - points to inner struct of block
4. __pointer_inside_fences__ - points to block fences
5. __pointer_inside_data_block__ - points to user memory
6. __pointer_unallocated__ - points to unallocated memory, yet in the heap space
7. __pointer_valid__ - first byte of allocated block
