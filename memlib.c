// Defs and helper functions of the memory management system.
//
// Author: Dustin Fast
 
#include <stdio.h>  // debug
#include <stdlib.h>
#include <stddef.h>
#include <sys/mman.h>

/* BEGIN Definitions ------------------------------------------------------ */

/*  Here we define our heap & memory-block elements, which coalesce to
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


// Struct BlockInfo - Information about a memory block
typedef struct BlockInfo {
  char *start_addr;         // Ptr to the block's start address
  char *data_addr;          // Ptr to the start of the block's data
  size_t size;              // Size of the block, in words
  size_t in_use;            // Nonzero if block is currently in free list
  size_t prev_in_use;       // Nonzero if prev contiguous block is in free list
  struct BlockInfo* next;   // Next block (unused if block not in free list)
} BlockInfo;

// Struct HeapInfo - Information about the memory heap.
typedef struct HeapInfo {
    BlockInfo *first_free;      // Ptr to head of the "free memory" list 
    char *start_addr;           // Ptr to first byte of heap
    char *data_addr;            // Ptr to start address of heap's mem blocks.
    char *end_addr;             // Ptr to last byte of heap
    char *max_addr;             // Ptr to max legal heap address
    size_t size;                // Denotes total size of the heap
} HeapInfo;

// Global heap ptr
static HeapInfo *g_heap = NULL;

// Denote size of the above structs. Will be 48 bytes each on 64-bit machines.
#define BLOCK_INFO_SZ sizeof(BlockInfo)
#define HEAP_INFO_SZ sizeof(HeapInfo)

// Denote word size of the current environment
#define WORD_SZ sizeof(void*)   // 8 bytes, on a 64-bit machine

// Denote minumum memory block sizes
#define MIN_BLOCK_SZ (BLOCK_INFO_SZ + WORD_SZ + BLOCK_INFO_SZ)

// Denote min size of the heap as one free block plus the HeapInfo header
#define MIN_HEAP_SZ (HEAP_INFO_SZ + MIN_BLOCK_SZ)

// Define initial heap size, in MB.
#define START_HEAP_SZ 20

// Define heap alignment, making it word-addressable, like so:
//    -----------------------
//    |   |   |   |  . . .  |
//    -----------------------
//    ^   ^   ^   ^         
//    0   8   16  24
#define HEAP_ALIGN WORD_SZ

/* END Definitions -------------------------------------------------------- */
/* Begin Linked List Helpers ---------------------------------------------- */

// Searches for a mem block >= "size" bytes in the given heap's "free" list.
// Returns: A ptr to the found block, or NULL if none found.
void *get_free_block(size_t size) {
    BlockInfo* curr_block = g_heap->first_free;

    while (curr_block) {
        if (curr_block->size >= size)
            return curr_block;
        curr_block = curr_block->next;
    }
    return NULL;
}

// "Allocates" a block by removing it from the heap's "free" list.
void allocate_block(BlockInfo *block) {
    // BlockInfo *next, *prev;
    // next = block->next;

    // while (curr_block) {
    //     if (curr_block->size >= size)
    //         return curr_block;
    //     curr_block = curr_block->next;
    // }

    // // If at EOL, patch its prev pointer.
    // if (next)
    //     next->prev = prev;

    // // If we're removing the head of the free list, set the head to be
    // // the next block, otherwise patch the previous block's next pointer.
    // if (block == FREE_LIST_HEAD)
    //     FREE_LIST_HEAD = next;
    // else
    //     prev->next = next;
}

/* End Linked List Helpers ------------------------------------------------ */
/* Begin Mem Helpers ------------------------------------------------------ */

// Increments the "used" portion of the given heap by adding "add_bytes" to it.
// Returns: Ptr to the first byte of the newly added space, or NULL on fail.
void *incr_heap(HeapInfo *heap, size_t add_bytes) 
{
    char *old_end = heap->end_addr;

    if (((heap->end_addr + add_bytes) > heap->max_addr)) {
        fprintf(stderr, "ERROR: Could not incremement heap - out of space.\n");  // debug
        return NULL;
    }
    heap->end_addr += add_bytes;
    return old_end;
}

// Allocates a new mem space of size "length" using the mmap syscall.
// Returns: A ptr to the mapped address space, or NULL on fail.
void *do_mmap(size_t length) {
    int prot = PROT_EXEC | PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void *result = mmap(NULL, length, prot, flags, -1, 0);

    if (result != MAP_FAILED) {
        return result;
    }
    return NULL;
}

// Unmaps the memory at "addr" of size "length" using the munmap syscall.
// Returns: Nonzero on success, or -1 on fail.
int do_munmap(void *addr, size_t length) {
    if (length > 0) {
        int result =  munmap(addr, length);
        if (result != -1) {
            return result;
        }
    }
    return -1;
}

// Debug function, prints the given heap's properties
void print_heap(HeapInfo *heap) {
    printf("Heap:\n");
    printf("Size: %u\n", heap->size);
    printf("SAddr: %u\n", heap->start_addr);
    printf("DAddr: %u\n", heap->data_addr);
    printf("FFree: %u\n", heap->first_free);
    printf("End: %u\n", heap->end_addr);
    printf("Max: %u\n", heap->max_addr);
}

// Debug function, prints the given BlockInfo's properties
void print_block(BlockInfo *block) {
    printf("Block:\n");
    printf("Size: %u\n", block->size);
    printf("SAddr: %u\n", block->start_addr);
    printf("DAddr: %u\n", block->data_addr);
    printf("InUse: %u\n", block->in_use);
    printf("PUsed: %u\n", block->prev_in_use);
    printf("Next: %u\n", block->next);
}

// Inits the global heap with one free memory block.
// Returns: 0 on success, or -1 on error.
int init_heap() {
    // Allocate the heap and its single first free mem block
    size_t heap_size = (START_HEAP_SZ * (1 << START_HEAP_SZ));
    g_heap = do_mmap(heap_size);
    BlockInfo *first_free = do_mmap(MIN_BLOCK_SZ);

    if (!g_heap || !first_free)
        return -1;
    
    // Init the first free block
    first_free->start_addr = (char*)first_free;
    first_free->data_addr = (char*)first_free + BLOCK_INFO_SZ;
    first_free->size = MIN_BLOCK_SZ;
    first_free->in_use = 0;
    first_free->prev_in_use = 1;  // First block always sets prev = true
    first_free->next = NULL;

    // Init the g_heap
    g_heap->first_free = first_free;  // First free block is one we just created
    g_heap->size = heap_size;
    g_heap->start_addr = (char*)g_heap;
    g_heap->data_addr = (char*)g_heap + HEAP_INFO_SZ;
    g_heap->end_addr = (char*)g_heap + MIN_BLOCK_SZ;
    g_heap->max_addr = (char*)g_heap + heap_size;
    
    return 0;
}

// Frees the memory associated with the given heap
void free_heap() {
    printf("Freeing heap...\n");
    print_heap(g_heap);

    while(g_heap->first_free) {
        print_block(g_heap->first_free);
        BlockInfo *next = g_heap->first_free->next;
        do_munmap(g_heap->first_free->start_addr, g_heap->first_free->size);
        g_heap->first_free = next;
    }

    do_munmap(g_heap->start_addr, g_heap->size);
    printf("Done freeing heap.\n");
}

/* End Mem Helpers -------------------------------------------------------- */
/* Begin malloc, calloc, realloc, free ------------------------------------ */

// Allocates "size" bytes of memory in user space.
// RETURNS: A ptr to the allocated memory on success, else returns NULL.
void *__malloc_impl(size_t size) {
    if (size > 0) {
        if (!g_heap)                // If heap not yet initialized, init it
            init_heap();


        // Make room for block header and ensure min size
        size += BLOCK_INFO_SZ;      
        if (size < MIN_BLOCK_SZ)
            size = MIN_BLOCK_SZ;
        
        // Ensure word-boundry alignment by rounding up
        size = HEAP_ALIGN * ((size + HEAP_ALIGN - 1) / HEAP_ALIGN);

        // Look for a block of this size in the heap's "free" list
        BlockInfo* free_block = get_free_block(size);

        // TODO: If none found, expand heap

        // Remove the block from the free list


        // BlockInfo *first_free = do_mmap(MIN_BLOCK_SZ);

        // if (!heap || !first_free)
        //     return NULL;
        
        // // Init the first free block
        // first_free->start_addr = (char*)first_free;
        // first_free->data_addr = (char*)first_free + BLOCK_INFO_SZ;
        // first_free->size = MIN_BLOCK_SZ;
        // first_free->in_use = 0;
        // first_free->prev_in_use = 1;  // First block always sets prev = true
        // first_free->next = NULL;
        

        void *result = do_mmap(size);

        if (result != MAP_FAILED)
            return result;
    }
    return NULL;
}

// Frees the memory space pointed to by ptr iff ptr != NULL
void __free_impl(void *ptr) {
    // if (ptr != NULL)
    //     do_munmap(ptr);

    // TODO: If the heap is empty, free it. It will re-init if needed.
}

/* End malloc, calloc, realloc, free -------------------------------------- */


/* Begin Debug ------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (!g_heap) {
        init_heap();
        free_heap();
    }

}
