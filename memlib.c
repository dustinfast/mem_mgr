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
        however it is immediately chunked (or expanded) to serve the current
        allocation request (and any sunseqent allocation requests).
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
    Also, when requesting a heap expansion, new addr is not contiguous
    
*/


/* BEGIN Definitions ------------------------------------------------------ */

// Memory Block Header. Servers double duty as a linked list node
typedef struct BlockHead {
  size_t size;              // Size of the block, with header, in bytes
  char *data_addr;          // Ptr to the block's data field
  struct BlockHead *next;   // Next block (unused if block not in free list)
  struct BlockHead *prev;   // Prev block (unused if block not in free list)
} BlockHead;                // Data field immediately follows above 4 bytes

// The heap header.
typedef struct HeapHead {
    size_t size;            // Total size of heap with header, in bytes
    char *start_addr;       // Ptr to first byte of heap
    BlockHead *first_free;  // Ptr to head of the "free" memory list 
} HeapHead;                 // Memory blocks follows the above 3 bytes

// Global heap ptr
HeapHead *g_heap = NULL;

#define START_HEAP_SZ (16 * 1048576)        // Heap megabytes * bytes in a mb
#define BLOCK_HEAD_SZ sizeof(BlockHead)     // Size of BlockHead struct (bytes)
#define HEAP_HEAD_SZ sizeof(HeapHead)       // Size of HeapHead struct (bytes)
#define MIN_BLOCK_SZ (BLOCK_HEAD_SZ + 1)    // Min block sz = header + 1 byte

void block_add_tofree(BlockHead *block);


/* END Definitions -------------------------------------------------------- */
/* StartPredefined Helpers ------------------------------------------------ */


// Fills the first n bytes at s with c
// RETURNS: ptr to s
static void *__memset(void *s, int c, size_t n) {
  unsigned char *p;
  size_t i;

  if (n == ((size_t) 0)) return s;
  for (i=(size_t) 0,p=(unsigned char *)s;
       i<=(n-((size_t) 1));
       i++,p++) {
    *p = (unsigned char) c;
  }
  return s;
}

// Copies n bytes from src to dest (mem areas must not overlap)
// RETURNS: ptr to dest
static void *__memcpy(void *dest, const void *src, size_t n) {
  unsigned char *pd;
  const unsigned char *ps;
  size_t i;

  if (n == ((size_t) 0)) return dest;
  for (i=(size_t) 0,pd=(unsigned char *)dest,ps=(const unsigned char *)src;
       i<=(n-((size_t) 1));
       i++,pd++,ps++) {
    *pd = *ps;
  }
  return dest;
}

// Multiplies a and b and checks for overflow. 
// Returns: Zero on fail, else nonzero.
int __try_size_t_multiply(size_t *c, size_t a, size_t b) {
  // If a == 0 or b == 0, no overflow
  if ((a == ((size_t) 0)) || (b == ((size_t) 0))) {
    *c = a * b;
    return 1;
  }

  // Else, multiply and check for overflow with t = a * q + r
  size_t t, r, q;
  t = a * b;
  q = t / a;
  r = t % a;

  if (r != ((size_t) 0)) return 0;  // If r != 0, overflow
  if (q != b) return 0;             // If q != b, overflow

  *c = t;                           // Else, no overflow
  return 1;
}

/* End Predefined Helpers ------------------------------------------------- */
/* Begin Mem Helpers ------------------------------------------------------ */


// Allocates a new mem space of "size" bytes using the mmap syscall.
// Returns: A ptr to the mapped address space, or NULL on fail.
void *do_mmap(size_t size) {
    int prot = PROT_EXEC | PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void *result = mmap(NULL, size, prot, flags, -1, 0);

    if (result == MAP_FAILED) result = NULL;
    return result;
}

// Unmaps the memory at "addr" of "size" bytes using the munmap syscall.
// Returns: Nonzero on success, or -1 on fail.
int do_munmap(void *addr, size_t size) {
    if (!size) return -1;
    return munmap(addr, size);
}

// // Debug function, prints the given BlockHead's properties.
// void block_print(BlockHead *block) {
//     printf("-----\nBlock\n");
//     printf("Size (bytes): %u\n", block->size);
//     printf("SAddr: %u\n", block);
//     printf("DAddr: %u\n", block->data_addr);
//     printf("Prev: %u\n", block->prev);
//     printf("Next: %u\n", block->next);
// }

// // Debug function, prints the global heap's properties.
// void heap_print() {
//     printf("-----\nHeap\n");
//     printf("Size (bytes): %u\n", g_heap->size);
//     printf("Start: %u\n", g_heap->start_addr);
//     printf("FirstFree: %u\n", g_heap->first_free);

//     BlockHead *next = g_heap->first_free;
//     while(next) {
//         block_print(next);
//         next = next->next;
//     }
// }

// Inits the global heap with one free memory block of maximal size.
void heap_init() {
    // Allocate the heap and its first free mem block
    size_t first_block_sz = START_HEAP_SZ - HEAP_HEAD_SZ;
    g_heap = do_mmap(START_HEAP_SZ);
    BlockHead *first_block = (BlockHead*)((void*)g_heap + HEAP_HEAD_SZ);

    if (!g_heap || !first_block) return;  // Ensure successful mmmap
    
    // Init the memory block
    first_block->size = first_block_sz;
    first_block->data_addr = (char*)first_block + BLOCK_HEAD_SZ;
    first_block->next = NULL;
    first_block->prev = NULL;

    // Init the heap and add the block to it's "free" list
    g_heap->size = START_HEAP_SZ;
    g_heap->start_addr = (char*)g_heap;
    g_heap->first_free = first_block;
}

// Expands the heap by START_HEAP_SZ mbs.
void heap_expand() {
    // Allocate the new space as a memory block
    BlockHead *new_block = do_mmap(START_HEAP_SZ);

    if (!new_block) return;  // Ensure succesful mmap

    // Init the new block
    new_block->size = START_HEAP_SZ;
    new_block->data_addr = (char*)new_block + BLOCK_HEAD_SZ;
    new_block->next = NULL;
    new_block->prev = NULL;

    // Denote new size of the heap and add the new block as free
    g_heap->size += START_HEAP_SZ;
    block_add_tofree(new_block);
}

// Combines the heap's free contiguous memory blocks
void heap_squeeze() {
    if (!g_heap->first_free)  return;

    BlockHead *curr = g_heap->first_free;
    while(curr->next) {
        if (((char*)curr + curr->size) == (char*)curr->next) 
            curr->size += curr->next->size;
        curr->next = curr->next->next;
    }
    
    // TODO: If the heap is empty, free it. It will re-init if needed.
}

// Repartitions the given block to the size specified, if able.
// Assumes: The block is "free" and size given includes room for header.
// Returns: A ptr to the original block, resized or not, depending on if able.
BlockHead *block_chunk(BlockHead *block, size_t size) {
    // Denote split address and resulting sizes
    BlockHead *block2 = (BlockHead*)((char*)block + size);
    size_t b2_size = block->size - size;
    size_t b1_size = block->size - b2_size;

    // Ensure partitions are large enough to be split
    if (b2_size >= MIN_BLOCK_SZ && b1_size >= MIN_BLOCK_SZ) {
        block->size = b1_size;
        block2->size = b2_size;
        block2->data_addr = (char*)block2 + BLOCK_HEAD_SZ;

        // Insert the new block between original block and the next (if any)
        block2->next = block->next;
        block->next = block2;
        block2->prev = block;

        block_add_tofree(block2);  // Add the new block to "free" list
    }

    return block;
}

// Frees all "free" memory blocks, and then the heap header itself.
void heap_free() {
    while(g_heap->first_free) {
        BlockHead *freeme = g_heap->first_free;
        g_heap->first_free = g_heap->first_free->next;
        size_t sz_free = freeme->size;
        do_munmap(freeme, sz_free);
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
// Assumes: Block is valid and does not already exist in the "free" list.
void block_add_tofree(BlockHead *block) {
    // If free list is empty, set us as first and return
    if (!g_heap->first_free) {
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
        g_heap->first_free->next = block;
        block->prev = g_heap->first_free;
    }
    // If inserting ourselves before all other blocks
    else if (curr == g_heap->first_free) {
        block->next = curr;
        curr->prev = block;
        g_heap->first_free = block;
    }
    // Else, insert ourselves immediately before the curr block
    else {
        curr->prev->next = block;
        block->prev = curr->prev;
        block->next = curr;
        curr->prev = block;
    }

    heap_squeeze();  // Combine any contiguous free blocks
}

// Removes the given block from the heap's "free" list.
void block_rm_fromfree(BlockHead *block) {
    BlockHead *next = block->next;
    BlockHead *prev = block->prev;

    // If not at EOL, next node's "prev" becomes the node before us
    if (next)
        next->prev = prev;

    // If we're the head of the list, the next node becomes the new head
    if (block == g_heap->first_free)
        g_heap->first_free = next;

    // Else, the prev node gets linked to the node ahead of us
    else
        prev->next = next;

    // Clear linked list info - it's no longer relevent
    block->prev = NULL;
    block->next = NULL;
}

/* End Linked List Helpers ------------------------------------------------ */
/* Begin malloc, calloc, realloc, free ------------------------------------ */


// Allocates "size" bytes of memory to the requester.
// RETURNS: A ptr to the allocated memory location on success, else NULL.
void *do_malloc(size_t size) {
    if (!size) return NULL;

    if (!g_heap) heap_init();   // If heap not yet initialized, do it now
    size += BLOCK_HEAD_SZ;      // Make room for block header

    // Find a free block >= needed size, expanding the heap as needed
    BlockHead* free_block = block_findfree(size);
    while (!free_block) {
        heap_expand();
        free_block = block_findfree(size);
    }
    
    // Break up this block if it's larger than needed
    if (size < free_block->size)
        free_block = block_chunk(free_block, size);
    
    block_rm_fromfree(free_block);  // Remove the block from the "free" list
    return free_block->data_addr;   // Return a ptr to block's data field
}

// Allocates an array of "nmemb" elements of "size" bytes, w/each el = 0.
// RETURNS: A ptr to the mem addr iff size and nmemb != 0, else returns NULL.
void *do_calloc(size_t nmemb, size_t size) {
    if (!__try_size_t_multiply(&size, nmemb, size)) return NULL;
    BlockHead *block = __memset(do_malloc(size), 0, size);
    return block;
}

// Frees the memory space pointed to by ptr iff ptr != NULL
void do_free(void *ptr) {
    if (!ptr) return;
    BlockHead *used_block = (BlockHead*)((void*)ptr - BLOCK_HEAD_SZ);
    block_add_tofree(used_block);
}

// Changes the size of the memory at "ptr" to the given size.
// Memory contents remain unchanged from start to min(old_sz, size).

// Else, 
// If ptr points to an area of mem that was moved, peforms a free(ptr).
// ASSUMES: ptr points to mem previously allocated with one of our functions.
void *do_realloc(void *ptr, size_t size) {
    // If ptr is NULL, do an malloc(size)
    if (!ptr) return do_malloc(size);
    
    // If size == 0, do a free(ptr)
    if (!size) {
        do_free(ptr);
        return NULL;
    }
    return NULL;
    // Else, reallocate the mem location
    // TODO: realloc
}


/* End malloc, calloc, realloc, free -------------------------------------- */
/* Begin Debug ------------------------------------------------------------ */


int main(int argc, char **argv) {
    // No fault
    // char *t1 = do_malloc(1048552);      // way oversize
    // char *t2 = do_malloc(1048491);      // oversize
    // // char *t3 = do_malloc(1048471);  // undersize
    
    // char *t3 = do_malloc(20);                            // -> 60
    // // char *t1 = do_malloc(1048552);      // way oversize     -> 1048592
    // // char *t2 = do_malloc(1048491);      // almost oversize  -> 1048531

    // do_free(t2);
    // do_free(t1);
    // do_free(t3);
    // do_free(t);

    // heap_free();

}
