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
        is then START_HEAP_SZ - HEAP_HEAD_SZ - BLOCK_HEAD_SZ bytes wide,
        however it is immediately chunked to serve the current allocation
        request (and any sunseqent allocation requests).
    2. If an allocation request cannot be served because a chunk of at least
        the requested size (plus it's header) is not available, the heap is
        expanded with another block of size START_HEAP_SZ.
    3. The heap header contains a ptr to the head of a doubly linked list of
        currently unallocated memory blocks. The "nodes" of this list are
        the headers of each memory block in the list - i.e., each mem block
        header has a "next" and "prev" ptr.
        Note: the first block in this list should always have it's prev_in_use
        member set to 1. This denotes to heap_squeeze() it needs to stop here.
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
    Also, when requesting a heap expansion, new addr is not contiguous
    
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

#define START_HEAP_SZ (1 * 1048576)   // Initial heap sz = mbs * bytes in a mb
// #define START_HEAP_SZ (1 * 1048576)   // Initial heap sz = mbs * bytes in a mb
#define BLOCK_HEAD_SZ sizeof(BlockHead)   // Size of BlockHead struct (bytes)
#define HEAP_HEAD_SZ sizeof(HeapHead)     // Size of HeapHead struct (bytes)
#define MIN_BLOCK_SZ (BLOCK_HEAD_SZ + 1)  // Min block sz = header + 1 byte

void block_add_tofree(BlockHead *block);


/* END Definitions -------------------------------------------------------- */
/* Begin Mem Helpers ------------------------------------------------------ */


// Allocates a new mem space of "size" bytes using the mmap syscall.
// Returns: A ptr to the mapped address space, or NULL on fail.
void *do_mmap(size_t size) {
    int prot = PROT_EXEC | PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void *result = mmap(NULL, size, prot, flags, -1, 0);

    if (result != MAP_FAILED)
        return result;
    return NULL;
}

// Unmaps the memory at "addr" of "size" bytes using the munmap syscall.
// Returns: Nonzero on success, or -1 on fail.
int do_munmap(void *addr, size_t size) {
    if (size > 0) {
        int result =  munmap(addr, size);
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
    printf("Prev: %u\n", block->prev);
    printf("Next: %u\n", block->next);
    // printf("PrevUsed: %d\n", block->prev_in_use);
}

// Debug function, prints the global heap's properties.
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
    // printf("END OF HEAP\n");         // debug
}

// Inits the global heap with one free memory block of maximal size.
void heap_init() {
    // Allocate the heap and its first free mem block
    size_t first_block_sz = START_HEAP_SZ - HEAP_HEAD_SZ;
    g_heap = do_mmap(START_HEAP_SZ);
    BlockHead *first_block = (BlockHead*)((void*)g_heap + HEAP_HEAD_SZ);

    // Ensure success
    if (!g_heap || !first_block)
        return;
    
    // Init the memory block
    first_block->size = first_block_sz;
    first_block->data_addr = (char*)first_block + BLOCK_HEAD_SZ;
    first_block->next = NULL;
    first_block->prev = NULL;
    first_block->prev_in_use = 1;  // First block, so set prev_in_use = 1

    // Init the heap and add the block to it's "free" list
    g_heap->size = START_HEAP_SZ;
    g_heap->start_addr = (char*)g_heap;
    g_heap->first_free = first_block;
    
    // printf("\n*** INITIALIZED HEAP:\n");    // debug
    // heap_print();                           // debug
}

// Expands the heap by START_HEAP_SZ mbs.
// Returns: A ptr to the new mem block in the heap, or null on error.
BlockHead* heap_expand() {
    // Allocate the new space as a memory block
    BlockHead *new_block = do_mmap(START_HEAP_SZ);

    // Ensure success
    if (!new_block)
        return NULL;

    // Init the new block
    new_block->size = START_HEAP_SZ;
    new_block->data_addr = (char*)new_block + BLOCK_HEAD_SZ;
    new_block->next = NULL;
    new_block->prev = NULL;
    new_block->prev_in_use = -1;

    // printf("\n *** NEW BLOCK:\n");          // debug
    // block_print(new_block);                 // debug

    // Denote new size of the heap and add the new block as free
    g_heap->size += START_HEAP_SZ;
    block_add_tofree(new_block);
    
    // printf("\n*** EXPANDED HEAP:\n");       // debug
    // heap_print();                           // debug

    return new_block;
}

// Combines the heap's free contiguous memory blocks
void heap_squeeze() {
    // printf("\n*** SQUEEZING HEAP:\n");      // debug
    // heap_print();                        // debug
    
    if (!g_heap->first_free)
        return;

    // size_t = tfree = 0;
    BlockHead *curr = g_heap->first_free;
    while(curr->next) {
        // If the next block is contiguous, combine it with this one.
        if (((char*)curr + curr->size) == (char*)curr->next) 
            curr->size += curr->next->size;
        curr->next = curr->next->next;
    }
    
    // printf("\n*** SQUEEZED HEAP:\n");      // debug
    // heap_print();                        // debug
    
    // TODO: If the heap is empty, free it. It will re-init if needed.
}

// Repartitions the given block to the size specified, if able.
// Assumes: The block is "free" and size given includes room for header.
// Returns: A ptr to the original block, resized or not, depending on if able.
BlockHead *block_chunk(BlockHead *block, size_t size) {
    // Denoting split addr and resulting sizes
    BlockHead *block2 = (BlockHead*)((char*)block + size);
    size_t b2_size = block->size - size;
    size_t b1_size = block->size - b2_size;

    // printf("\n*** CHUNKING:\n");
    // // heap_print();
    // printf("\nReqSz: %u\n", size);              // debug
    // printf("B1Sz: %u\n", b1_size);              // debug
    // printf("B2Sz: %u\n", b2_size);              // debug

    // Do the partition if both blocks large enough to be split
    if (b2_size >= MIN_BLOCK_SZ && b1_size >= MIN_BLOCK_SZ) {
        block->size = b1_size;
        block2->size = b2_size;
        block2->data_addr = (char*)block2 + BLOCK_HEAD_SZ;
        block2->prev_in_use = 0;

        // "Insert" the new block between original block and the next (if any)
        block2->next = block->next;
        block->next = block2;
        block2->prev = block;

        // Add new block to "free" list
        block_add_tofree(block2);
        
        // printf("\n*** CHUNKED BLOCKs:\n");      // debug
        // heap_print();                           // debug
    }
    //  else {
    //     printf("\n *** DIDN'T CHUNK");       // debug
    // }

    return block;
}

// Frees all free memory blocks, and then the heap header.
void heap_free() {
    // printf("\n*** FREEING HEAP:\n");            // debug
    // heap_print();                               // debug

    while(g_heap->first_free) {
        BlockHead *freeme = g_heap->first_free;
        g_heap->first_free = g_heap->first_free->next;
        size_t sz_free = freeme->size;
        // printf("Freeing:");
        // block_print(freeme);
        // printf("SZ: %u", sz_free);
        do_munmap(freeme, sz_free);

        // printf("\nNext: %u\n", g_heap->first_free);
    }
    do_munmap((void*)g_heap, (size_t)g_heap + HEAP_HEAD_SZ);
    // printf("\n*** DONE FREEING HEAP:\n");      // debug
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
void block_add_tofree(BlockHead *block) {
    // Assume prev block in use, we'll double-check it below if needed
    block->prev_in_use = 1;

    // If free list is empty, set us as first and return
    if (!g_heap->first_free) {
        // printf("\n*** ADDED %u TO FREE 'FIRST':\n", block);       // debug        
        g_heap->first_free = block;
        return;
    }

    // Else, find list insertion point (recall list is sorted ASC by address)
    BlockHead* curr = g_heap->first_free;
    while (curr)
        if (curr > block)
            break;
        else
            curr = curr->next;
        
    // If no smaller address found, insert ourselves after the head
    if (!curr) { 
        // printf("\n*** ADDED %u TO FREE 'AFTER':\n", block);       // debug  
        g_heap->first_free->next = block;
        block->prev = g_heap->first_free;

    // If inserting ourselves before all other blocks
    } else if (curr == g_heap->first_free) {
        // printf("\n*** ADDED %u TO FREE as 'HEAD':\n", block);      // debug        
        block->next = curr;
        curr->prev = block;
        g_heap->first_free = block;

    // Else, insert ourselves immediately before the curr block
    } else {
        // printf("\n*** ADDED %u TO FREE 'BEFORE':\n", block);      // debug
        curr->prev->next = block;
        block->prev = curr->prev;
        block->next = curr;
        curr->prev = block;
    }

    // If the next block is contiguous, inform it we are free
    if (((char*)block + block->size) == (char*)block->next)
        block->next->prev_in_use = 0;

    // block_print(block);                                 // debug
    // heap_print();

    heap_squeeze();                 // Squeeze any contiguous free blocks
}

// Removes the given block from the heap's "free" list.
void block_rm_fromfree(BlockHead *block) {
    BlockHead *next = block->next;
    BlockHead *prev = block->prev;

    // If not at EOL, next node's "prev" becomes the node before us
    if (next) {
        next->prev = prev;
        next->prev_in_use = 1;      // Also inform it we're now in use
    }

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
        
    if (!g_heap)                    // If heap not yet initialized, do it now
        heap_init();

    size += BLOCK_HEAD_SZ;          // Make room for block header

    // printf("\n*** REQUESTED %u BYTES.\n", size);  // debug

    // Find a free block >= needed size, expanding the heap as needed
    BlockHead* free_block = block_findfree(size);
    while (!free_block) {
        heap_expand();
        free_block = block_findfree(size);
    }
    
    // printf("RSz: %u\n", size)                           // debug
    // printf("BSz: %u\n", free_block->size);              // debug

    // Break up this block if it's larger than needed
    if (size < free_block->size)
        free_block = block_chunk(free_block, size);
    
    block_rm_fromfree(free_block);  // Remove the block from the free list
    
    // printf("\n*** ALLOCATED BLOCK (%u bytes):\n", free_block->size);  // debug
    // heap_print();

    return free_block->data_addr;   // Return a ptr to block's data field
}

// Frees the memory space pointed to by ptr iff ptr != NULL
void do_free(void *ptr) {
    if (!ptr)
        return;

    // Step back to the block's header and add it to the "free" list
    BlockHead *used_block = (BlockHead*)((void*)ptr - BLOCK_HEAD_SZ);
    block_add_tofree(used_block);

    // printf("\n*** FREED ALLOCATED BLOCK: %u\n", used_block);   // debug
    // block_print(used_block);                    // debug
}


/* End malloc, calloc, realloc, free -------------------------------------- */
/* Begin Debug ------------------------------------------------------------ */


int main(int argc, char **argv) {
    // No fault
    // char *t1 = do_malloc(1048552);      // way oversize
    // char *t2 = do_malloc(1048491);      // oversize
    // char *t3 = do_malloc(1048471);  // undersize
    
    // fault
    char *t3 = do_malloc(20);                            // -> 60
    char *t1 = do_malloc(1048552);      // way oversize     -> 1048592
    char *t2 = do_malloc(1048491);      // almost oversize  -> 1048531

    do_free(t1);
    do_free(t2);
    do_free(t3);

    heap_free();

}
