/**
 * xmalloc and xfree version 4.0
 *
 * Supported features:
 *	- align allocation on word boundaries
 *	- split on malloc
 *  - merge on free
 *  - double linked free list
 */
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

struct header {
	size_t size;
	bool free; // 1 if the block is free, 0 otherwise
	struct header *next;
	struct header *prev;
};

struct header *head = NULL;
struct header *tail = NULL;

#define HEADER_SIZE sizeof(struct header)
#define get_header(ptr) (struct header*)(ptr) - 1
#define append_to_free_list(blk) \
	(blk)->prev = tail; \
	tail->next = (blk); \
	tail = (blk);

// sizeof(size_t) is probably the word size, so align on multiples of that
#define ALIGNMENT sizeof(size_t)
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

void
init_header(struct header *header, size_t size, bool free)
{
	header->size = size;
	header->free = free;
	header->next = NULL;
	header->prev = NULL;
}

void *
get_raw_heap_mem(size_t size)
{
	void *request;

	if ((request = sbrk(size)) == (void*)-1)
		return NULL;

	return request;
}

// Gets enough heap for the header and the requested allocation size
// Returns a pointer to the start of the header or NULL if there is no memory available
struct header *
get_heap_mem(size_t size)
{
	struct header *block;

	if ((block = get_raw_heap_mem(HEADER_SIZE + size)) == NULL)
		return NULL;

	init_header(block, size, false);	
	return block;
}

bool
can_split_block(struct header *block, size_t req_size)
{
	// need space for the header and some data
	return (block->size + HEADER_SIZE) > req_size;
}

struct header *
split_block(struct header *lhs_block, size_t size)
{
	struct header *rhs_block;

	rhs_block = (struct header*)(char*)(lhs_block+1) + size;
	init_header(rhs_block, lhs_block->size - size - HEADER_SIZE, true);
	lhs_block->size = size;
	lhs_block->free = false;

	rhs_block->prev = lhs_block;
	rhs_block->next = lhs_block->next;
	if (lhs_block->next)
		lhs_block->next->prev = rhs_block;
	lhs_block->next = rhs_block;
	if (lhs_block == tail) // splitting the tail?
		tail = rhs_block;
	
	return lhs_block;
}

// Searches the free list for a block that can fit size bytes in it
// If there is no block found NULL is returned
struct header *
search_free_list(size_t size)
{
	struct header *curr_blk;

	// while the current block is to small or the block is taken step through the list
	curr_blk = head;
	while (curr_blk && !(curr_blk->size >= size && curr_blk->free))
		curr_blk = curr_blk->next;

	if (curr_blk == NULL)
		return NULL;

	if (can_split_block(curr_blk, size))
		return split_block(curr_blk, size);

	return curr_blk;
}

struct header *
expand_free_list(size_t size)
{
	struct header *block;
	
	if ((block = get_heap_mem(size)) == NULL)
		return NULL;

	append_to_free_list(block);
	return block;
}

struct header *
init_heap(size_t size)
{
	struct header *blk_one;	

	if ((blk_one = get_heap_mem(size)) == NULL)
		return NULL;

	head = tail = blk_one;
	return head;
}

void *
xmalloc(size_t size)
{
	struct header *block;

	size = ALIGN(size);
	if (head == NULL) {
		block = init_heap(size);
	} else {
		if ((block = search_free_list(size)) == NULL) {
			if ((block = expand_free_list(size)) == NULL)
				return NULL;
		}
	}

	return block + 1;
}

// Merge block b into block a
void
merge_adjacent_free_blocks(struct header *a, struct header *b)
{
	a->size += b->size + HEADER_SIZE;

	a->next = b->next;
	if (b->next)
		b->next->prev = a;

	if (tail == b)
		tail = a;
}

void
xfree(void *ptr)
{
	struct header *block;

	if (!ptr)
		return;

	block = get_header(ptr);
	block->free = true;

	if (block->next && block->next->free) {
		merge_adjacent_free_blocks(block, block->next);
		return;
	}

	if (block->prev && block->prev->free)
		merge_adjacent_free_blocks(block->prev, block);
}

int
free_list_len()
{
	int len;
	struct header *tmp;

	for (tmp = head, len = 0; tmp; tmp = tmp->next, ++len)
		;

	return len;
}

int
main()
{
	struct header *a1h, *a2h, *a3h;
	char *a1, *a2, *a3;

	a1 = xmalloc(10);
	a1h = get_header(a1);	
	assert(a1h->size == ALIGN(10));
	assert(a1h->free == false);

	a2 = xmalloc(20);
	a2h = get_header(a2);
	assert(a2h->size == ALIGN(20));
	assert(a2h->free == false);

	a3 = xmalloc(30);
	a3h = get_header(a3);
	assert(a3h->size == ALIGN(30));
	assert(a3h->free == false);

	assert(free_list_len() == 3);

	xfree(a1);
	assert(a1h->free == true);

	xfree(a2);
	assert(a2h->free == true);

	assert(a1h->size == (ALIGN(10) + ALIGN(20) + ALIGN(HEADER_SIZE)));

	assert(free_list_len() == 2);

	xfree(a3);
	assert(a3h->free == true);

	assert(a1h->size == (ALIGN(10) + ALIGN(20) + ALIGN(30) + ALIGN(HEADER_SIZE) + ALIGN(HEADER_SIZE)));
	assert(free_list_len() == 1);
	assert(a1h == head);
	assert(a1h == tail);

	a1 = xmalloc(10);
	a1h = get_header(a1);
	assert(a1h->size == ALIGN(10));
	assert(a1h->free == false);
	assert(a1h == head);
	
	a2h = a1h->next;
	assert(a2h == tail);
	assert(a2h->free == true);

	assert(free_list_len() == 2);

	puts("All asserts passed");

	return 0;
}
