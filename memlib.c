// Defs and helper functions of the memory management system.
//
// Author: Dustin Fast
 
#include <stdio.h>  // debug
#include <stdlib.h>
#include <stddef.h>
#include <sys/mman.h>

/* BEGIN Definitions ------------------------------------------------------ */

/*  INTRO: Here we define our heap & memory-block elements, which coalesce to
    represent the Heap and Memory Block structures, like so:

     Memory Block        Memory Block            Heap
    (Unallocated)        (Allocated)        --------------
    --------------      --------------      |  HeapInfo  |      
    | BlockInfo  |      | BlockInfo  |      --------------  
    --------------      --------------      |     ...    |  
    |   (free    |      |    ...     |      |     ...    |
    |    space)  |      |   (block   |      |   (blocks) |
    --------------      |    data)   |      |     ...    |
    | BlockInfo  |      |    ...     |      |     ...    |
    --------------      --------------      --------------

    Note: Allocated memory blocks don't get a BlockInfo footer - the footer
          is only used as a helper to combine unused blocks of memory.
    Note: Calls to our malloc, etc., functions return ptrs to the block
          data inside the memory block, not to the memory block itself.
*/

// Struct BlockInfo - Information about a memory block. Also functions as a
// node when the block is in the heaps's "free" list (a doubly-linked list,
// sorted ASC, by address).
typedef struct BlockInfo {
  char *data_addr;          // Ptr to the first byte of block's data
  size_t size;              // Size of the block, in words
  size_t in_use;            // Nonzero if block is currently in free list
  size_t prev_in_use;       // Nonzero if prev contiguous block is in free list
  struct BlockInfo *start_addr;  // Ptr to the block's start address
  struct BlockInfo *next;   // Next block (unused if not curr in free list)
  struct BlockInfo *prev;   // Prev block (unused if not curr in free list)
} BlockInfo;

// Struct HeapInfo - Information about the memory heap.
typedef struct HeapInfo {
    BlockInfo *first_free;      // Ptr to head of the "free" memory list 
    char *start_addr;           // Ptr to first byte of heap
    char *data_addr;            // Ptr to start address of heap's mem blocks.
    char *end_addr;             // Ptr to last byte of heap
    char *max_addr;             // Ptr to max legal heap address
    size_t size;                // Denotes total size of the heap
} HeapInfo;

// Global heap ptr
HeapInfo *g_heap = NULL;

// Denote size of the above structs.
#define BLOCK_INFO_SZ sizeof(BlockInfo)
#define HEAP_INFO_SZ sizeof(HeapInfo)

// Denote word size, and define our heap alignment to match
#define WORD_SZ sizeof(void*)   // 8 bytes, on a 64-bit machine
#define HEAP_ALIGN WORD_SZ

// Denote minumum memory block sizes
#define MIN_BLOCK_SZ (BLOCK_INFO_SZ + WORD_SZ + BLOCK_INFO_SZ)

// Denote min size of the heap as one free block plus the HeapInfo header
#define MIN_HEAP_SZ (HEAP_INFO_SZ + MIN_BLOCK_SZ)

// Define initial heap size, in MB
#define START_HEAP_SZ 20


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

// Debug function, prints the given BlockInfo's properties.
void print_block(BlockInfo *block) {
    printf("----------\nBlock\n----------\n");
    printf("Size: %u\n", block->size);
    printf("SAddr: %u\n", block->start_addr);
    printf("DAddr: %u\n", block->data_addr);
    printf("InUse: %u\n", block->in_use);
    printf("PUsed: %u\n", block->prev_in_use);
    printf("Next: %u\n", block->next);
    printf("Prev: %u\n", block->prev);
}

// Debug function, prints the given heap's properties.
void print_heap() {
    printf("----------\nHeap\n----------\n");
    printf("Size: %u\n", g_heap->size);
    printf("SAddr: %u\n", g_heap->start_addr);
    printf("DAddr: %u\n", g_heap->data_addr);
    printf("FFree: %u\n", g_heap->first_free);
    printf("End: %u\n", g_heap->end_addr);
    printf("Max: %u\n", g_heap->max_addr);

    BlockInfo *next = g_heap->first_free;
    while(next) {
        print_block(next);
        next = next->next;
    }
}

// Inits the global heap with one free memory block of the minimum size.
// Returns: 0 on success, or -1 on error.
int init_heap() {
    // Allocate the heap and its first free mem block
    size_t heap_bytes_sz = (START_HEAP_SZ * (1 << START_HEAP_SZ));
    g_heap = do_mmap(heap_bytes_sz);
    BlockInfo *first_free = do_mmap(MIN_BLOCK_SZ);

    if (!g_heap || !first_free)
        return -1;
    
    // Init the first free block
    first_free->start_addr = first_free;
    first_free->data_addr = (char*)first_free + BLOCK_INFO_SZ;
    first_free->size = MIN_BLOCK_SZ;
    first_free->in_use = 0;
    first_free->prev_in_use = 1;  // First block always sets prev as in use
    first_free->next = NULL;
    first_free->prev = NULL;

    // Init the g_heap
    g_heap->first_free = first_free;  // First free block is one we just created
    g_heap->size = heap_bytes_sz;
    g_heap->start_addr = (char*)g_heap;
    g_heap->data_addr = (char*)g_heap + HEAP_INFO_SZ;
    g_heap->end_addr = (char*)g_heap + MIN_BLOCK_SZ;
    g_heap->max_addr = (char*)g_heap + heap_bytes_sz;
    
    return 0;  // Success
}

// Frees the memory associated with the heap
void free_heap() {
    printf("*** FREED HEEP:\n");  // debug
    print_heap();           // debug
    do_munmap(g_heap->start_addr, g_heap->size);
}

/* End Mem Helpers -------------------------------------------------------- */
/* Begin Linked List Helpers ---------------------------------------------- */

/*  INTRO: Our linked list is a doubly linked list, with the head denoted by
    g_heap->first_free. 
*/

// Searches for a mem block >= "size" bytes in the given heap's "free" list.
// Returns: A ptr to the block found, or NULL if none found.
void *get_free_block(size_t size) {
    BlockInfo* curr = g_heap->first_free;

    while (curr)
        if (curr->size >= size)
            return curr;
        else
            curr = curr->next;
    return NULL;
}

// Adds the given block into the heap's "free" list.
// Assumes: Block does not already exist in the "free" list.
void add_free(BlockInfo *block) {
    // If free list empty
    if (!g_heap->first_free) {
        g_heap->first_free = block;
        return;
    }

    // Find insertion point, recalling that "free" list is ASC, by mem address
    BlockInfo* curr = g_heap->first_free;
    while (curr)
        if (curr->start_addr > block->start_addr)
            break;
        else
            curr = curr->next;
        
    // Insert the block immediately before the curr block
    curr->prev->next = (BlockInfo*)block->start_addr;
    curr->prev = (BlockInfo*)block->start_addr;
    block->prev = curr->prev;
    block->next = curr;
}

// Removes the given block from the heap's "free" list.
void rm_free(BlockInfo *block) {
    BlockInfo *next = block->next;
    BlockInfo *prev = block->prev;

    // If not at EOL, set next node's "prev" ptr to the node before us (if any)
    if (next)
        next->prev = prev;

    // If we're the head of the list, the next node becomes the new head
    if (block == g_heap->first_free)
        g_heap->first_free = next;

    // Else, the prev node gets linked to the node ahead of us
    else
        prev->next = next;
}

/* End Linked List Helpers ------------------------------------------------ */
/* Begin malloc, calloc, realloc, free ------------------------------------ */

// Allocates "size" bytes of memory in user space.
// RETURNS: A ptr to the allocated memory location on success, else NULL.
void *do_malloc(size_t size) {
    if (size <= 0)
        return NULL;
        
    if (!g_heap)        // If heap not yet initialized, do it now
        init_heap();

    // Make room for block header and set to min if needed
    size += BLOCK_INFO_SZ;      
    if (size <= MIN_BLOCK_SZ)
        size = MIN_BLOCK_SZ;
    
    // If not needed, ensure word-boundry alignment by rounding size up
    else
        size = HEAP_ALIGN * ((size + HEAP_ALIGN - 1) / HEAP_ALIGN);

    // Look for a block of at least this size in the heap's "free" list
    BlockInfo* free_block = get_free_block(size);

    if (!free_block) {
        printf("ERROR: No block of requested size found.\n");
        // TODO: If none found, expand heap
        exit(0);
    }

    // TODO: Break up this block, if much bigger than requested

    // Remove the block from the free list, set its "used" flag
    rm_free(free_block);
    free_block->in_use = 1;
    printf("*** ALLOCATED BLOCK:\n");  // debug
    // printf("%d\n", free_block->data_addr);  // debug
    print_block(free_block);    // debug

    // Return a ptr to it's data area
    return free_block->data_addr;
}

// Frees the memory space pointed to by ptr iff ptr != NULL
void do_free(void *ptr) {
    if (!ptr)
        return;
    
    BlockInfo *used_block = (BlockInfo*)((void*)ptr - BLOCK_INFO_SZ);
    add_free(used_block);
    used_block->in_use = 0;
    printf("*** FREED ALLOCATED BLOCK:\n");  // debug
    print_block(used_block);            // debug
    // TODO: If the heap is empty, free it. It will re-init if needed.
}

/* End malloc, calloc, realloc, free -------------------------------------- */


/* Begin Debug ------------------------------------------------------------ */

int main(int argc, char **argv) {
    init_heap();
    // print_heap();
    char *test = do_malloc(1);
    // test = "t";
    do_free(test);
    free_heap();

}
