// SPDX-License-Identifier: BSD-3-Clause
#include "osmem.h"
#include "helpers.h"

#include <unistd.h>

typedef struct block_meta block_meta_t;

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define META_SIZE ALIGN(sizeof(block_meta_t))
#define TOT_SIZE(size) ALIGN(size + META_SIZE)
#define MMAP_THRESHOLD (128 * 1024)
#define INITIAL_HEAP_SIZE (128 * 1024)

block_meta_t *heap_start;
block_meta_t *mmap_start;

size_t threshold = MMAP_THRESHOLD;

size_t min_size(size_t a, size_t b)
{
	return a < b ? a : b;
}

block_meta_t *request_space(block_meta_t *last, size_t block_size)
{
	if (last && last->status == STATUS_FREE) {
		block_meta_t *ret = sbrk(block_size - TOT_SIZE(last->size));

		DIE(ret == (void *) -1, "sbrk");
		last->size += block_size - TOT_SIZE(last->size);
	} else {
		block_meta_t *block = sbrk(block_size);

		DIE(block == (void *) -1, "sbrk");

		block->size = block_size - META_SIZE;
		block->status = STATUS_FREE;
		block->next = NULL;

		if (last)
			last->next = block;
		last = block;
	}

	return last;
}

void heap_init(void)
{
	heap_start = request_space(NULL, INITIAL_HEAP_SIZE);
}

void *os_malloc_mmap(size_t size)
{
	size_t blk_size = TOT_SIZE(size);

	block_meta_t *block = mmap(NULL, blk_size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	DIE(block == MAP_FAILED, "mmap");

	block->size = blk_size - META_SIZE;
	block->status = STATUS_MAPPED;
	block->next = mmap_start;
	mmap_start = block;

	return (char *)block + META_SIZE;
}

int can_coalesce_one_block(block_meta_t *block)
{
	if (block->status == STATUS_FREE) {
		block_meta_t *next = block->next;

		if (next && next->status == STATUS_FREE)
			return 1;
	}

	return 0;
}

block_meta_t *find_block_in_heap(block_meta_t *block)
{
	block_meta_t *current = heap_start;

	while (current) {
		if (current == block)
			return current;
		current = current->next;
	}

	return NULL;
}

void coalesce_one_block(block_meta_t *block)
{
	if (can_coalesce_one_block(block)) {
		block_meta_t *next = block->next;

		block->size += TOT_SIZE(next->size);
		block->next = next->next;
	}
}

void coalesce_blocks(block_meta_t *block)
{
	while (can_coalesce_one_block(block))
		coalesce_one_block(block);
}

block_meta_t *find_best_fit(block_meta_t **last, block_meta_t *heap_start,
	size_t block_size)
{
	block_meta_t *best_fit = NULL;
	block_meta_t *current = heap_start;

	while (current) {
		coalesce_blocks(current);
		if (current->status == STATUS_FREE &&
			TOT_SIZE(current->size) >= block_size) {
			if (!best_fit || current->size < best_fit->size)
				best_fit = current;
		}
		*last = current;
		current = current->next;
	}

	return best_fit;
}

block_meta_t *get_block_ptr(void *ptr)
{
	return (void *)((char *) ptr - META_SIZE);
}

void split_block(block_meta_t *block, size_t block_size)
{
	block_meta_t *new_block = (void *) block + block_size;

	new_block->size = block->size - block_size;
	new_block->status = STATUS_FREE;
	new_block->next = block->next;

	block->size = block_size - META_SIZE;
	block->next = new_block;
}

void *os_malloc_sbrk(size_t size)
{
	if (!heap_start)
		heap_init();

	size_t blk_size = TOT_SIZE(size);

	block_meta_t *last = heap_start;
	block_meta_t *block = find_best_fit(&last, heap_start, blk_size);

	if (!block) {
		block = request_space(last, blk_size);
		block->status = STATUS_ALLOC;
		if (!block)
			return NULL;
	} else {
		block->status = STATUS_ALLOC;
		if (block->size > blk_size)
			split_block(block, blk_size);
	}

	return (char *) block + META_SIZE;
}

void *os_malloc(size_t size)
{
	if (size <= 0)
		return NULL;

	size = ALIGN(size);
	size_t blk_size = TOT_SIZE(size);

	if (blk_size < threshold)
		return os_malloc_sbrk(size);
	else
		return os_malloc_mmap(size);
}

void os_free_sbrk(void *ptr)
{
	block_meta_t *block = get_block_ptr(ptr);

	if (!find_block_in_heap(block))
		return;

	block->status = STATUS_FREE;
	coalesce_blocks(block);
}

void os_free_mmap(void *ptr)
{
	block_meta_t *block = get_block_ptr(ptr);
	block_meta_t *prev = NULL;
	block_meta_t *current = mmap_start;

	while (current) {
		if (current == block) {
			if (prev)
				prev->next = current->next;
			else
				mmap_start = current->next;
			munmap(current, TOT_SIZE(current->size));
			break;
		}
		prev = current;
		current = current->next;
	}
}

void os_free(void *ptr)
{
	if (!ptr)
		return;

	block_meta_t *block = get_block_ptr(ptr);

	if (block->status == STATUS_MAPPED)
		os_free_mmap(ptr);
	else
		os_free_sbrk(ptr);
}

void *os_calloc(size_t nmemb, size_t size)
{
	threshold = getpagesize();

	size_t total_size = nmemb * size;
	void *ptr = os_malloc(total_size);

	memset(ptr, 0, total_size);
	threshold = MMAP_THRESHOLD;

	return ptr;
}

void *os_realloc_mmap(void *ptr, size_t size)
{
	block_meta_t *block = get_block_ptr(ptr);
	void *new_ptr = os_malloc(size);

	memcpy(new_ptr, ptr, min_size(block->size, size));
	os_free_mmap(ptr);

	return new_ptr;
}

void *os_realloc_sbrk(void *ptr, size_t size)
{
	block_meta_t *block = get_block_ptr(ptr);
	size_t blk_size = TOT_SIZE(size);

	if (!find_block_in_heap(block))
		return NULL;

	if (blk_size >= threshold) {
		void *new_ptr = os_malloc_mmap(size);

		memcpy(new_ptr, ptr, min_size(block->size, size));
		os_free_sbrk(ptr);
		return new_ptr;
	}

	size_t old_size = block->size;

	block->status = STATUS_FREE;
	while (TOT_SIZE(block->size) < blk_size && can_coalesce_one_block(block))
		coalesce_one_block(block);
	block->status = STATUS_ALLOC;

	if (TOT_SIZE(block->size) >= blk_size) {
		if (block->size > blk_size)
			split_block(block, blk_size);
		return ptr;
	}

	if (!block->next) {
		block->status = STATUS_FREE;
		block_meta_t *last = heap_start;
		block_meta_t *new_block = find_best_fit(&last, heap_start, blk_size);

		if (!new_block) {
			block->status = STATUS_FREE;
			block = request_space(last, blk_size);
			block->status = STATUS_ALLOC;
			if (!block)
				return NULL;

			return ptr;
		}

		new_block->status = STATUS_ALLOC;

		memcpy((char *) new_block + META_SIZE, ptr, old_size);
		new_block->status = STATUS_ALLOC;
		os_free_sbrk(ptr);

		return (char *) new_block + META_SIZE;
	}
	if (block->size != old_size)
		split_block(block, TOT_SIZE(old_size));

	void *new_ptr = os_malloc_sbrk(size);

	memcpy(new_ptr, ptr, min_size(block->size, size));
	os_free_sbrk(ptr);

	return new_ptr;
}

void *os_realloc(void *ptr, size_t size)
{
	if (!ptr)
		return os_malloc(size);
	if (!size) {
		os_free(ptr);
		return NULL;
	}

	size = ALIGN(size);
	block_meta_t *block = get_block_ptr(ptr);

	if (block->status == STATUS_FREE)
		return NULL;

	if (block->status == STATUS_MAPPED)
		return os_realloc_mmap(ptr, size);
	else
		return os_realloc_sbrk(ptr, size);
}
