// Defs and helper functions of the memory management system.
//
// Author: Dustin Fast
 
#include <stdio.h>  // debug
#include <stddef.h>
#include <sys/mman.h>

/* BEGIN Definitions ------------------------------------------------------ */

/*  Here we define our heap & memory-block elements, which are ultimately
    structured as follows:

     Memory Block        Memory Block            Heap
    (Unallocated)        (Allocated)        --------------
    --------------      --------------      |  HeapInfo  |      
    | BlockInfo  |      | BlockInfo  |      --------------  
    --------------      --------------      |     ...    |  
    |   (free    |      |    ...     |      | (allocated |
    |    space)  |      |   (block   |      |   blocks)  |
    --------------      |    data)   |      |     ...    |
    | BlockInfo  |      |    ...     |      |     ...    |
    --------------      --------------      --------------

    Note: We don't actually define our heap or memory blocks anywhere,
          they exist "notionally" in memory. 
    Note: Allocated memory blocks don't get a BlockInfo footer - the footer
          is only used as a helper to combine unused blocks of memory.
    Note: Calls to our malloc, etc., functions return ptrs to the block
          data inside the memory block, not to the memory block itself.
*/

// Global heap ptr
static void *g_heap = NULL;

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
    BlockInfo *first_free;    // Ptr to head of the "free memory" list 
    char *start_addr;       // Ptr to first byte of heap
    char *data_addr;        // Ptr to start address of heap's mem blocks.
    char *end_addr;         // Ptr to last byte of heap
    char *max_addr;         // Ptr to max legal heap address
    size_t size;            // Denotes total size of the heap
} HeapInfo;

// Denote size of the above structs. Will be 48 bytes each on 64-bit machines.
#define BLOCK_INFO_SZ sizeof(BlockInfo)
#define HEAP_INFO_SZ sizeof(HeapInfo)

// Denote word size of the current environment
#define WORD_SZ sizeof(void*)   // 8 bytes, on a 64-bit machine

// Denote minumum memory block sizes
#define MIN_USED_BLOCK_SZ (BLOCK_INFO_SZ + WORD_SZ)
#define MIN_FREE_BLOCK_SZ (BLOCK_INFO_SZ + WORD_SZ + BLOCK_INFO_SZ)

// Denote min size of the heap as one free block plus the HeapInfo header
#define MIN_HEAP_SZ (HEAP_INFO_SZ + MIN_FREE_BLOCK_SZ)

// Define initial heap size, in MB.
#define START_HEAP_SIZE 20

// Define heap alignment = 8 bytes, making it 64-bit word-addressable, like so:
//    -----------------------
//    |   |   |   |  . . .  |
//    -----------------------
//    ^   ^   ^   ^         
//    0   8   16  24
#define HEAP_ALIGN 8

/* END Definitions -------------------------------------------------------- */
/* Begin Helpers ---------------------------------------------------------- */

// Increments the "used" portion of the heap by adding "add_bytes" to it.
// Returns: Ptr to the first byte of the newly added space, or NULL on fail.
// void *incr_heap(size_t add_bytes) 
// {
//     char *old_end = heap.end;

//     if (((heap.end + add_bytes) > heap.mem_max_addr)) {
//         fprintf(stderr, "ERROR: Could not incremement heap - out of space.\n");  // debug
//         return NULL;
//     }
//     heap.end += add_bytes;
//     return old_end;
// }

// Allocates a new mem space of size "length" using the mmap syscall.
// Returns: A ptr to the mapped address space, or NULL on fail.
void *do_mem_map(size_t length) {
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
int do_mem_unmap(void *addr, size_t length) {
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
    printf("Size: %d\n", heap->size);
    printf("SAddr: %d\n", heap->start_addr);
    printf("DAddr: %d\n", heap->data_addr);
    printf("FFree: %d\n", heap->first_free);
    printf("End: %d\n", heap->end_addr);
    printf("Max: %d\n", heap->max_addr);
}

// Debug function, prints the given BlockInfo's properties
void print_block(BlockInfo *block) {
    printf("Block:\n");
    printf("Size: %d\n", block->size);
    printf("SAddr: %d\n", block->start_addr);
    printf("DAddr: %d\n", block->data_addr);
    printf("InUse: %d\n", block->in_use);
    printf("PUsed: %d\n", block->prev_in_use);
    printf("Next: %d\n", block->next);
}

// Inits a new heap with one free memory block.
// Returns: A fully initialized heap of "mb_size" MBs, or NULL on error.
void *new_heap(size_t mb_size) {
    // Allocate the heap and first free mem block
    size_t heap_size = (mb_size * (1 << mb_size));
    HeapInfo *heap = do_mem_map(heap_size);
    BlockInfo *first_free = do_mem_map(MIN_FREE_BLOCK_SZ);

    if (!heap || !first_free)
        return NULL;
    
    // Init the first free block
    first_free->start_addr = (char*)first_free;
    first_free->data_addr = (char*)first_free + BLOCK_INFO_SZ;
    first_free->size = MIN_FREE_BLOCK_SZ;
    first_free->in_use = 0;
    first_free->prev_in_use = 1;  // First block always sets prev = in use
    first_free->next = NULL;

    // Init the heap
    heap->first_free = first_free;  // First free block is one we just created
    heap->size = heap_size;
    heap->start_addr = (char*)heap;
    heap->data_addr = (char*)heap + HEAP_INFO_SZ;
    heap->end_addr = (char*)heap + MIN_FREE_BLOCK_SZ;
    heap->max_addr = (char*)heap + heap_size;
    
    return heap;
}

// Frees the memory associated with the given heap
void free_heap(HeapInfo *heap) {
    printf("Freeing heap...\n");
    print_heap(heap);

    while(heap->first_free) {
        print_block(heap->first_free);
        BlockInfo *next = heap->first_free->next;
        do_mem_unmap(heap->first_free->start_addr, heap->first_free->size);
        heap->first_free = next;
    }

    do_mem_unmap(heap->start_addr, heap->size);
    printf("Done freeing heap.\n");
}
/* End Helpers ------------------------------------------------------------ */
/* Begin Debug ------------------------------------------------------------ */

int main(int argc, char **argv) {
    // void *heap = new_heap(20);
    if (!g_heap)
        g_heap = new_heap(START_HEAP_SIZE);

    free_heap(g_heap);


}
