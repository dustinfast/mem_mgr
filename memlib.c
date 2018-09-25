// Defs and helper functions of the memory management system.
//
// TODO:
//      Macros for better perf
//      Mem Block footer for better perf
//
// Author: Dustin Fast
 
#include <stdio.h>  // debug
#include <unistd.h> 
#include <stdlib.h>
#include <stddef.h>
#include <sys/mman.h>

/* BEGIN Definitions ------------------------------------------------------ */

/*  INTRO: Here we define our heap & memory-block elements, which coalesce to
    represent the Heap and Memory Block structures, like so:

     Memory Block        Memory Block            Heap
    (Unallocated)        (Allocated)        --------------
    --------------      --------------      |  HeapHead  |      
    | BlockHead  |      | BlockHead  |      --------------  
    --------------      --------------      |     ...    |  
    |    ...     |      |    ...     |      |    (mem    |
    |   (data    |      |   (data    |      |    blocks) |
    |    field)  |      |    field)  |      |     ...    |
    |    ...     |      |    ...     |      |     ...    |
    --------------      --------------      --------------

    Notes:
    1. Our malloc, etc., functions return ptrs to the start of the data field
        inside the memory block, not to the memory block itself.
    2. The heap may not exist when a memory allocation is requested. If
        not, it is initialized to START_HEAP_SZ MBs, with a single free
        memory block occupying all it's space. That block's data field
        is then (START_HEAP_SIZE - HEAP_HEAD_SZ - BLOCK_HEAD_SIZE) bytes
        wide, but it is immediattely chunked to serve the current, and
        any sunseqent memory allocation requests.
    3. If an allocation request cannot be served because a chunk of at least
        tat size is not available...
    4. The heap contains a double linked list...
    
*/

// Struct BlockHead - Information about a memory block. Also functions as a
// node when the block is in the heaps's "free" list (a doubly-linked list,
// sorted ASC, by address).
typedef struct BlockHead {
  size_t size;              // Size of the entire block, in words
  struct BlockHead *start_addr;  // Ptr to the block's start address
  char *data_addr;          // Ptr to the block's data field
  struct BlockHead *next;   // Next block (unused if block not in free list)
  struct BlockHead *prev;   // Prev block (unused if block not in free list)
  size_t prev_in_use;       // Nonzero if prev contiguous block is in free list
} BlockHead;                // Data field immediately follows above 6 bytes

// Struct HeapHead - Information about the memory heap.
typedef struct HeapHead {
    size_t size;                // Denotes total size of the heap
    char *start_addr;           // Ptr to first byte of heap
    char *end_addr;             // Ptr to last addressable bit of the heap
    BlockHead *first_free;      // Ptr to head of the "free" memory list 
} HeapHead;                     // Memory blocks field follows above 4 bytes

// Global heap ptr
HeapHead *g_heap = NULL;

#define START_HEAP_SZ 20                    // Initial heap allocation (mbytes)
#define BLOCK_HEAD_SZ sizeof(BlockHead)     // Size of BlockHead struct (bytes)
#define HEAP_HEAD_SZ sizeof(HeapHead)       // Size of HeapHead struct (bytes)
#define MIN_BLOCK_SZ (BLOCK_HEAD_SZ + 1)    // Min mem block sz = head + 1 byte


/* END Definitions -------------------------------------------------------- */
/* Begin Mem Helpers ------------------------------------------------------ */


// Allocates a new mem space of size "length" using the mmap syscall.
// Returns: A ptr to the mapped address space, or NULL on fail.
void *do_mmap(size_t length) {
    int prot = PROT_EXEC | PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void *result = mmap(NULL, length, prot, flags, -1, 0);

    if (result != MAP_FAILED)
        return result;
    return NULL;
}

// Unmaps the memory at "addr" of size "length" using the munmap syscall.
// Returns: Nonzero on success, or -1 on fail.
int do_munmap(void *addr, size_t length) {
    if (length > 0) {
        int result =  munmap(addr, length);
        if (result != -1)
            return result;
    }
    return -1;
}

// Debug function, prints the given BlockHead's properties.
void block_print(BlockHead *block) {
    printf("----------\nBlock\n----------\n");
    printf("Loc: %u\n", block);
    printf("Size: %u\n", block->size);
    printf("SAddr: %u\n", block->start_addr);
    printf("DAddr: %u\n", block->data_addr);
    printf("Next: %u\n", block->next);
    printf("Prev: %u\n", block->prev);
    printf("PrevUsed: %u\n", block->prev_in_use);
}

// Debug function, prints the given heap's properties.
void heap_print() {
    printf("----------\nHeap\n----------\n");
    printf("Size: %u\n", g_heap->size);
    printf("SAddr: %u\n", g_heap->start_addr);
    printf("End: %u\n", g_heap->end_addr);
    printf("FFree: %u\n", g_heap->first_free);

    BlockHead *next = g_heap->first_free;
    while(next) {
        block_print(next);
        next = next->next;
    }
}

// Inits the global heap with one free memory block of the minimum size.
// Returns: 0 on success, or -1 on error.
int heap_init() {
    // Determine size reqs
    size_t heap_bytes_sz = START_HEAP_SZ * (1 << START_HEAP_SZ);
    size_t first_block_sz = heap_bytes_sz - HEAP_HEAD_SZ;

    // Allocate the heap and its first free mem block
    g_heap = do_mmap(heap_bytes_sz);
    BlockHead *first_block = do_mmap(first_block_sz);  // TODO: refactor

    if (!g_heap || !first_block)
        return -1;
    
    // Init the first free block
    first_block->size = first_block_sz;
    first_block->start_addr = first_block;
    first_block->data_addr = (char*)first_block + BLOCK_HEAD_SZ;
    first_block->next = NULL;
    first_block->prev = NULL;
    first_block->prev_in_use = 1;    // Signals 'stop here' to compactor

    // Init the heap's members and add the free block to it's "free" list
    g_heap->size = heap_bytes_sz;
    g_heap->start_addr = (char*)g_heap;
    g_heap->end_addr = (char*)g_heap + HEAP_HEAD_SZ + first_block_sz;
    g_heap->first_free = first_block;
    
    printf("\n*** INITIALIZED HEAP:\n");    // debug
    heap_print();                           // debug
    return 0;
}

// Expands the heap by START_HEAP_SZ mbs
void heap_expand() {

}

// Compacts the heap's free memory blocks
void heap_compact() {
    // TODO: If the heap is empty, free it. It will re-init if needed.
}

// Breaks the given memory block in two w/the 2nd block = requested size.
// Returns: A ptr to the 2nd blocks header
BlockHead *block_chunk(size_t size) {

}


// Frees the memory associated with the heap
void heap_free() {
    printf("\n*** FREED HEAP:\n");    // debug
    heap_print();              // debug
    do_munmap(g_heap->start_addr, g_heap->size);
}


/* End Mem Helpers -------------------------------------------------------- */
/* Begin Linked List Helpers ---------------------------------------------- */


// Searches for a mem block >= "size" bytes in the given heap's "free" list.
// Returns: A ptr to the block found, or NULL if none found.
void *block_findfree(size_t size) {
    BlockHead* curr = g_heap->first_free;

    while (curr)
        if (curr->size >= size)
            return curr;
        else
            curr = curr->next;
    return NULL;
}

// Adds the given block into the heap's "free" list.
// Assumes: Block does not already exist in the "free" list.
void block_tofreelst(BlockHead *block) {
    // If free list empty
    if (!g_heap->first_free) {
        g_heap->first_free = block;
        return;
    }

    // Find insertion point, recalling that "free" list is ASC, by mem address
    BlockHead* curr = g_heap->first_free;
    while (curr)
        if (curr->start_addr > block->start_addr)
            break;
        else
            curr = curr->next;
        
    // Insert the block immediately before the curr block
    curr->prev->next = (BlockHead*)block->start_addr;
    curr->prev = (BlockHead*)block->start_addr;
    block->prev = curr->prev;
    block->next = curr;
}

// Removes the given block from the heap's "free" list.
void block_rmfreelst(BlockHead *block) {
    BlockHead *next = block->next;
    BlockHead *prev = block->prev;

    // If not at EOL, set next node's "prev" ptr to the node before us (if any)
    if (next)
        next->prev = prev;

    // If we're the head of the list, the next node becomes the new head
    if (block == g_heap->first_free)
        g_heap->first_free = next;

    // Else, the prev node gets linked to the node ahead of us
    else
        prev->next = next;

    // Clear BlockHead info that's no longer relevent
    block->prev_in_use = 0;
    block->prev = NULL;
    block->next = NULL;
}

/* End Linked List Helpers ------------------------------------------------ */
/* Begin malloc, calloc, realloc, free ------------------------------------ */


// Allocates "size" bytes of memory to the requester.
// RETURNS: A ptr to the allocated memory location on success, else NULL.
void *do_malloc(size_t size) {
    if (size <= 0)
        return NULL;
        
    if (!g_heap)        // If heap not yet initialized, do it now
        heap_init();

    // Make room for block header
    size += BLOCK_HEAD_SZ;

    // Look for a block of at least this size in the heap's "free" list
    BlockHead* free_block = block_findfree(size);

    if (!free_block) {
        printf("ERROR: No block of size %u found.\n", size);
        // TODO: If none found, expand heap
        exit(0);
    }

    // TODO: Break up this block, if > 12% bigger than requested

    // Remove the block from the free list
    block_rmfreelst(free_block);
    printf("\n*** ALLOCATED BLOCK (%u bytes):\n", size);  // debug
    block_print(free_block);    // debug

    // Return a ptr to it's data area
    return free_block->data_addr;
}

// Frees the memory space pointed to by ptr iff ptr != NULL
void do_free(void *ptr) {
    if (!ptr)
        return;
    
    printf("\nPTR:%u\n", ptr);
    BlockHead *used_block = (BlockHead*)((void*)ptr - BLOCK_HEAD_SZ);
    // block_tofreelst(used_block);

    printf("\n*** FREED ALLOCATED BLOCK:\n");   // debug
    block_print(used_block);                    // debug

    // TODO: compact heap
}


/* End malloc, calloc, realloc, free -------------------------------------- */
/* Begin Debug ------------------------------------------------------------ */


int main(int argc, char **argv) {
    // heap_init();
    char *t = do_malloc(2);
    printf("t1: %u\n", t);
    t = "q";
    printf("t2: %u\n", t);
    printf("S: %u\n", sizeof(BlockHead));

    do_free(t);
    heap_free();

}
