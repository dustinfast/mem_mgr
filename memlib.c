// Defs and helper functions of the memory management system.
//
// TODO:
//      Macros for better perf
//      Mem Block footer for better perf
//      Remove HeadInfo->start_addr (it's redundant)
//
// Author: Dustin Fast
 
#include <stdio.h>  // debug
#include <unistd.h> 
#include <stdlib.h>
#include <stddef.h>
#include <sys/mman.h>


/*  INTRODUCTION: 
    Our heap & memory-block are structured like so:

     Memory Block       Memory Block           Heap
    (Unallocated)       (Allocated)       --------------
    -------------      -------------      |  HeapHead  |      
    | BlockHead |      | BlockHead |      --------------  
    --------------     -------------      |     ...    |  
    |    ...    |      |    ...    |      |   (blocks  |
    |   (data   |      |   (data   |      |    field)  |
    |   field)  |      |   field)  |      |     ...    |
    |    ...    |      |    ...    |      |     ...    |
    -------------      -------------      --------------

    Walkthrough:
    1. The heap may not exist when a memory allocation is requested. If
        not, it is initialized to START_HEAP_SZ mbs with a single free memory
        block occupying it's entire "blocks" field. The block's data field
        is then START_HEAP_SIZE - HEAP_HEAD_SZ - BLOCK_HEAD_SIZE bytes wide,
        however it is immediately chunked to serve the current allocation
        request (and any sunseqent allocation requests).
    2. If an allocation request cannot be served because a chunk of at least
        the requested size (plus it's header) is not available, the heap is
        expanded with another block of size START_HEAP_SZ.
    3. The heap header contains a ptr to the head of a doubly linked list of
        currently unallocated memory blocks. The "nodes" of this list are
        the headers of each memory block in the list - i.e., each mem block
        header has a "next" and "prev" ptr.
    4. We pass around free memory blocks by their header. When a block is not
        free, we don't do anything with it - it belongs to the caller, who
        knows nothing about the header because the malloc/calloc/realloc 
        functions return ptrs to the data fields inside the blocks they serve,
        rather than to the headers themselves.
    5. When a free() request is made, we step back BLOCK_HEAD_SZ bytes to the
        start of it's header and again reference it by its header.
    6. If at any time the heap is back to a single free block occupying it's 
        entire blocks field, the heap is freed. In this way, we:
            a. Avoid expanding the heap (requiring a syscall) for every 
                allocation request.
            b. Ensure that no heaps go un-freed (unless their requestor fails
                to properly free their memory).
            c. 
            
    Design Decisions:
    The kind of list to use (singly-linked/doubly-linked) as well as the 
    structure of the heap and blocks had to be determined beforehand, as they
    dictated the structure of the rest of the application - each possible
    choice had large performance implications.
    It also had to be decided how to initialize and free the heap.
    Some perf improvments can still be made - if we add a footer to each mem
    block, we can step backwards through the free list, rather than starting
    from the front.
    
*/


/* BEGIN Definitions ------------------------------------------------------ */

// Struct BlockHead - Memory Block Header. Servers double duty as a linked
// list node iff the block it describes is in the heaps's "free" list.
typedef struct BlockHead {
  size_t size;              // Size of the block, with header, in bytes
  char *data_addr;          // Ptr to the block's data field
  struct BlockHead *next;   // Next block (unused if block not in free list)
  struct BlockHead *prev;   // Prev block (unused if block not in free list)
  int prev_in_use;       // Nonzero if prev contiguous block is in free list
} BlockHead;                // Data field immediately follows above 6 bytes

// Struct HeapHead - The heap header.
typedef struct HeapHead {
    size_t size;                // Total size of heap with header, in bytes
    char *start_addr;           // Ptr to first byte of heap
    BlockHead *first_free;      // Ptr to head of the "free" memory list 
} HeapHead;                     // Memory blocks field follows above 4 bytes

// Global heap ptr
HeapHead *g_heap = NULL;

#define START_HEAP_SZ 1                     // Initial heap allocation (mb)
#define CHUNK_AT .25                        // Percent to chunk blocks at / 100
#define BLOCK_HEAD_SZ sizeof(BlockHead)     // Size of BlockHead struct (bytes)
#define HEAP_HEAD_SZ sizeof(HeapHead)       // Size of HeapHead struct (bytes)
#define MIN_BLOCK_SZ (BLOCK_HEAD_SZ + 1)  // Min block sz = header + 1 byte

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
    printf("-----\nBlock\n");
    printf("Size (bytes): %u\n", block->size);
    printf("SAddr: %u\n", block);
    printf("DAddr: %u\n", block->data_addr);
    printf("Next: %u\n", block->next);
    printf("Prev: %u\n", block->prev);
    printf("PrevUsed: %d\n", block->prev_in_use);
}

// Debug function, prints the given heap's properties.
void heap_print() {
    printf("-----\nHeap\n");
    printf("Size (bytes): %u\n", g_heap->size);
    printf("Start: %u\n", g_heap->start_addr);
    printf("FirstFree: %u\n", g_heap->first_free);

    BlockHead *next = g_heap->first_free;
    while(next) {
        block_print(next);
        next = next->next;
    }
}

// Inits the global heap with one free memory block of maximal size.
// Returns: 0 on success, or -1 on error.
int heap_init() {
    // Determine size reqs
    size_t heap_bytes_sz = START_HEAP_SZ * 1048576;  // convert mb to bytes
    size_t first_block_sz = heap_bytes_sz - (HEAP_HEAD_SZ);

    // Allocate the heap and its first free mem block
    g_heap = do_mmap(heap_bytes_sz);
    BlockHead *first_block = do_mmap(first_block_sz);  // TODO: refactor

    if (!g_heap || !first_block)
        return -1;
    
    // Init the memory block
    first_block->size = first_block_sz;
    first_block->data_addr = (char*)first_block + BLOCK_HEAD_SZ;
    first_block->next = NULL;
    first_block->prev = NULL;
    first_block->prev_in_use = 1;    // First block always sets in use flag

    // Init the heap and add the block to it's "free" list
    g_heap->size = heap_bytes_sz;
    g_heap->start_addr = (char*)g_heap;
    g_heap->first_free = first_block;
    
    printf("\n*** INITIALIZED HEAP:\n");    // debug
    heap_print();                           // debug
    return 0;
}

// Expands the heap by START_HEAP_SZ mbs.
// Returns: A ptr to the new mem block in the heap, or null on error.
BlockHead* heap_expand() {
    printf("\n*** EXPANDING HEAP:\n");      // debug
    heap_print();                           // debug
    // TODO: Allocate more mem with do_mmap and add it to the heap

    // TODO: Compact heap
}

// Combines the heap's free contiguous memory blocks
void heap_compact() {
    // printf("\n*** COMPACTING HEAP:\n");    // debug
    // heap_print();                        // debug
    // TODO: If the heap is empty, free it. It will re-init if needed.
}

// Repartitions the given block to the size specified, if able.
// Assumes: The block is "free", & size specified includes room for header.
// Returns: A ptr to the original block, resized or not, depending on if able.
BlockHead *block_chunk(BlockHead *block, size_t size) {
    // Split the current block, denoting new sizes
    BlockHead *block2 = (BlockHead*)((char*)block + size);
    block2->size = block->size - size;
    size_t b1_size = block->size - block2->size;

    // Enure both blocks are large enough to be split
    if (block2->size >= MIN_BLOCK_SZ && block->size >= MIN_BLOCK_SZ) {
        // Finish chunking
        block->size = b1_size;
        block2->data_addr = (char*)block2 + BLOCK_HEAD_SZ;
        block2->prev_in_use = 0;

        // "Insert" the new block between original block and the next (if any)
        block->next = block2;
        block2->prev = block;
        
        printf("\n*** CHUNKED BLOCKs:\n");      // debug
        heap_print();                           // debug
    }

    return block;
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
    // size *= 8;  // convert to bytes

    while (curr)
        if (curr->size >= size)
            return curr;
        else
            curr = curr->next;
    return NULL;
}

// Adds the given block into the heap's "free" list.
// Assumes: Block does not already exist in the "free" list.
void block_to_free(BlockHead *block) {
    // If free list is empty, we become the first
    if (!g_heap->first_free) {
        g_heap->first_free = block;
        block->prev_in_use = 1;  // First block always sets in use flag
        return;
    }

    // Else, find insertion point, recalling "free" is sorted ASC, by address
    BlockHead* curr = g_heap->first_free;
    while (curr)
        if (curr > block)
            break;
        else
            curr = curr->next;
        
    // If inserting ourselves before all other blocks
    if (curr == g_heap->first_free) {
        printf("\nhere\n");
        block->next = curr;
        curr->prev = block;
        block->prev_in_use = 1;
        g_heap->first_free = block;
    // Else, insert ourselves immediately before the curr block
    } else {
        printf("\nhere2\n");
        curr->prev->next = block;
        block->prev = curr->prev;
        block->next = curr;
        curr->prev = block;
    }

    // If the next block is contiguous, inform it we are free
    if (((char*)block + block->size) == (char*)block->next)
        block->next->prev_in_use = 0;
}

// Removes the given block from the heap's "free" list.
void block_from_free(BlockHead *block) {
    BlockHead *next = block->next;
    BlockHead *prev = block->prev;
    next->prev_in_use = 1;

    // If not at EOL, set next node's "prev" ptr to the node before us (if any)
    if (next) 
        next->prev = prev;
        
    // If we're the head of the list, the next node becomes the new head
    if (block == g_heap->first_free)
        g_heap->first_free = next;

    // Else, the prev node gets linked to the node ahead of us
    else
        prev->next = next;

    // Clear header info that's no longer relevent
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

    size += BLOCK_HEAD_SZ;  // Make room for block header

    // Look for a block of at least this size in the heap's "free" list

    // Find a free block >= size, or expand heap to get one if neede
    BlockHead* free_block = block_findfree(size);
    if (!free_block) 
        free_block = heap_expand();

    // Break up this block if it is CHUNK_AT percent larger than needed
    if (size / free_block->size < CHUNK_AT)
        free_block = block_chunk(free_block, size);

    // Remove the block from the free list
    block_from_free(free_block);
    
    printf("\n*** ALLOCATED BLOCK (%u bytes):\n", size);  // debug
    block_print(free_block);    // debug

    // Return a ptr to it's data area
    return free_block->data_addr;
}

// Frees the memory space pointed to by ptr iff ptr != NULL
void do_free(void *ptr) {
    if (!ptr)
        return;
    
    // Step back to the block's header and add it to the "free" list
    BlockHead *used_block = (BlockHead*)((void*)ptr - BLOCK_HEAD_SZ);
    block_to_free(used_block);  // Add block back to "free" list and do compact

    printf("\n*** FREED ALLOCATED BLOCK:\n");   // debug
    block_print(used_block);                    // debug
}


/* End malloc, calloc, realloc, free -------------------------------------- */
/* Begin Debug ------------------------------------------------------------ */


int main(int argc, char **argv) {
    char *t1 = do_malloc(1048471);
    char *t2 = do_malloc(20);
    // char *t3 = do_malloc(15);
    do_free(t2);
    do_free(t1);
    // do_free(t3);
    heap_free();

}
