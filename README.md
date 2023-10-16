# About

This is a simple implementation of a memory allocator written in C. 

# Resources used

- ["Implementing malloc" slides by Michael Saelee](https://moss.cs.iit.edu/cs351/slides/slides-malloc.pdf)
- [Malloc Tutorial](https://danluu.com/malloc-tutorial/)

# Implementation details:

## helper macros

I have used the following helper macros:

- ALIGNMENT 8
- ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
- META_SIZE ALIGN(sizeof(block_meta_t))
- TOT_SIZE(size) ALIGN(size + META_SIZE)

## helper procedures

```C
// get the smaller size between a and b
size_t min_size(size_t a, size_t b);

// given the last block in a list and the size of a new block, if the
// list is NOT NULL and the last block is marked as free, then it
// extends the last block. Otherwise creates a new block and links
// it to the end of the list
block_meta_t *request_space(block_meta_t *last, size_t block_size);

// preallocates memory for the heap
void heap_init(void);

// check if the current block and the next one both 
// exist and are marked as free
int can_coalesce_one_block(block_meta_t *block);

// merges two adjacent free blocks
void coalesce_one_block(block_meta_t *block);

// merges all consecutive free blocks starting from the current one
void coalesce_blocks(block_meta_t *block);

// find a block in the heap list. Returns NULL if the block 
// is not found otherwise returns the block
block_meta_t *find_block_in_heap(block_meta_t *block);

// given a pointer to an array, returns a pointer to the enclosing block
block_meta_t *get_block_ptr(void *ptr);

// uses the BEST FIT strategy to find the smallest free space that is 
// larger than block_size. If no free space is found, the procedure
// returns NULL and last will point to the last element in the heap.
block_meta_t *find_best_fit(
  block_meta_t **last, 
  block_meta_t *heap_start,
  size_t block_size); 

// split the current block into two adjacent blocks where the first has size
// block_size and the second one is marked free
void split_block(block_meta_t *block, size_t block_size);
```

## os_malloc

For implementing os_malloc I use two lists: one for the heap (where I store small payloads) and one for the memory mapping segment (where I store the larger ones). If the block that is allocated is larger than the threshold, *os_malloc_mmap* strategy is used. Otherwise *os_malloc_sbrk* strategy is used

```C
// creates a new block using mmap and adds it to the beginning of 
// the mmap list
void *os_malloc_mmap(size_t size);

// creates a new block on heap. If *find_best_fit* returns a NOT NULL 
// location, the block is allocated within the existing heap. Otherwise
// the heap is extended using *request_space* and the new block is 
// added to the end of the list
void *os_malloc_sbrk(size_t size);
```

## os_free

For implementing os_free, I look up the status of the block. If it is equal to STATUS_MAPPED, then the block is removed using the *os_free_mmap* strategy. Otherwise, *os_free_sbrk* strategy is used

```C
// removes a block from memory mapping and updates the mmap list accordingly
void os_free_mmap(void *ptr);

// removes a block from heap. Because the list is implicit, no traversal
// of the entire list is required
void os_free_sbrk(void *ptr);
```

## os_calloc

For implementing os_calloc, I call os_malloc with an argument equal to the size of the entire array. After that, I use memset to fill the array with 0.

## os_realloc

For implementing os_realloc, I first check the special cases (pointer is NULL, size is 0 or the block is free). After that, I look up the status of the block. If it is equal to STATUS MAPPED, then the block is in the memory mapping region, thus it is reallocated using *os_realloc_mmap* strategy. Otherwise, *os_realloc_sbrk* strategy is used.

```C
// creates a new block using os_malloc, copies the content to the new location
// and frees the old location using os_free_mmap
void *os_realloc_mmap(void *ptr, size_t size);

// - If the block can't be found, NULL is returned
// - Then, I first check if the new block is larger than the threshold. If it
// is, then a new block is allocated using os_malloc_mmap. Then the content is
// simply copied to the new location and the old location is freed using 
// os_free_sbrk
// - Then, I try to extend the current block as long as it is followed by
// free blocks and the size does not exceed the new required size. If the 
// block can be extended enough to accomodate the new size, it is first split
// into the necessary and empty parts and the original pointer is returned
// - Then if the block can no longer be extended, I check whether the block
// is the last one in the heap. 
// --- If it is not, we have to split the block into the necessary and free
// parts, and the unse os_malloc_sbrk to allocate a new memory location for
// this block to be moved. The old block is copied and freed using os_free_sbrk
// --- If it is the last block, I first check if find_best_fit strategy finds
// a suitable location in the existing heap for the block to be moved to. If
// a location was not found, the block (which is also the last one) is extended
// to accomodate the new size. Otherwise, the block is simply copied to the
// new location found by find_best_fit.
void *os_realloc_sbrk(void *ptr, size_t size);
```

