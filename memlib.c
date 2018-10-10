// Defs and helper functions of the memory management system.
//
// TODO:
//      Finish docstrings
//      static qualifers?
//      Ensure heap_free called appropriately
//
// Author: Dustin Fast
 
#include <stdio.h>      // debug
#include <sys/types.h>  // debug
#include <sys/stat.h>   // debug
#include <fcntl.h>      // debug

#include <unistd.h> 
#include <stdlib.h>
#include <stddef.h>
#include <sys/mman.h>


/*  INTRODUCTION: 
    Our heap & memory blocks are structured like so:

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
        know nothing about the header (malloc/calloc/realloc return ptrs to
        the data fields inside the blocks, rather than to the headers.
    5. When a free() request is made, we step back BLOCK_HEAD_SZ bytes to the
        start of it's header and again reference it by its header.
    6. If at any time the heap is back to a single free block occupying it's 
        entire blocks field, the heap is freed. In this way, we:
            a. Avoid making a syscall for every allocation request.
            b. Ensure that all mem is freed (assuming its all released to us).
            
    Design Decisions:
    The kind of list to use (singly-linked/doubly-linked) as well as the 
    structure of the heap and blocks had to be determined beforehand, as they
    dictated the structure of the rest of the application - each possible
    choice had performance implications.
    When requesting a heap expansion, new addr is not contiguous, this caused 
    issues initially.
    
*/


/* Begin Definitions ------------------------------------------------------ */

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
#define WORD_SZ sizeof(void*)               // Word size on this architecture

void block_add_tofree(BlockHead *block);


/* End Definitions -------------------------------------------------------- */
/* Begin Utility Helpers -------------------------------------------------- */


/* -- str_len -- */
// Returns the length of the given null-terminated "string".
size_t str_len(char *arr) {
    int length = 0;
    for (char *c = arr; *c != '\0'; c++)
        length++;

    return length;
}

/* -- str_write -- */
// Writes the string given by arr to filedescriptor fd.
// RETURNS: The number of bytes written, or -1 on error.
int str_write(char *arr) {
    size_t curr_write = 0;
    size_t total_written = 0;
    size_t char_count = str_len(arr);

    // If empty string , do nothing
    if (!char_count) 
        return 0;

    // Write string to the given file descriptor (note ptr arith in write()).
    while (total_written < char_count) {
        curr_write = write(fileno(stdout), arr + total_written, char_count - total_written);
        
        if (curr_write < 0)
            return -1; // on error
        total_written += curr_write;
    }

    return total_written;
}

/* -- mem_set -- */
// Fills the first n bytes at s with c
// RETURNS: ptr to s
static void *mem_set(void *s, int c, size_t n) {
    char *p = (char *)s;
    while(n) {
        *p = (char)c;
        p++;
        n--;
    }
  return s;
}

/* -- mem_cpy -- */
// Copies n bytes from src to dest (mem areas must not overlap)
// RETURNS: ptr to dest
static void *mem_cpy(void *dest, const void *src, size_t n) {
    char *pd = dest;
    const char *ps = src;
    while (n) {
        *pd = *ps;
        pd++;
        ps++;
        n--;
    }

    return dest;
}

/* -- sizet_multiply -- */
// Multiplies the given icand and iplier.
// Adapted from __try_size_t_multiply, clauter 2018.
// RETURNS: a * b iff no size_t overflow, else 0. Also returns 0 if a * b = 0.
static size_t sizet_multiply(size_t a, size_t b) {
    if (a == 0 || b == 0)
        return 0;

    // Euclidean division
    size_t t, q, remnder;
    t = a * b;
    q = t / a;
    remnder = t % a;

    // If overflow
    if (remnder || q != b)
        return 0;
    
    // No overflow
    return t;
}


/* End Predefined Helpers ------------------------------------------------- */
/* Begin Mem Helpers ------------------------------------------------------ */


/* -- do_mmap -- */
// Allocates a new mem space of "size" bytes using the mmap syscall.
// Returns: On suceess, a ptr to the mapped address space, else NULL.
void *do_mmap(size_t size) {
    int prot = PROT_EXEC | PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void *result = mmap(NULL, size, prot, flags, -1, 0);

    if (result == MAP_FAILED)
        result = NULL;

    return result;
}

/* -- do_munmap -- */
// Unmaps the memory at "addr" of "size" bytes using the munmap syscall.
// Returns: Nonzero on success, or -1 on fail.
int do_munmap(void *addr, size_t size) {
    if (!size)
         return -1;
    return munmap(addr, size);
}

/* -- heap_init -- */
// Inits the global heap with one free memory block of maximal size.
void heap_init() {
    // Allocate the heap and its first free mem block
    size_t first_block_sz = START_HEAP_SZ - HEAP_HEAD_SZ;
    g_heap = do_mmap(START_HEAP_SZ);
    BlockHead *first_block = (BlockHead*)((void*)g_heap + HEAP_HEAD_SZ);

    if (!g_heap || !first_block)
        return;

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

/* -- heap_expand -- */
// Adds a new block of at least "size" bytes to the heap. If "size" is less
//      than START_HEAP_SZ, START_HEAP_SZ bytes is added instead.
// Returns: On success, a ptr to the new block created, else NULL.
BlockHead *heap_expand(size_t size) {
    if (size < START_HEAP_SZ)
        size = START_HEAP_SZ;

    // Allocate the new space as a memory block
    BlockHead *new_block = do_mmap(size);

    if (!new_block)
         return NULL;  

    // Init the new block
    new_block->size = size;
    new_block->data_addr = (char*)new_block + BLOCK_HEAD_SZ;
    new_block->next = NULL;
    new_block->prev = NULL;

    // Denote new size of the heap and add the new block as free
    g_heap->size += size;
    block_add_tofree(new_block);

    return new_block;
}

/* -- block_chunk -- */
// Repartitions the given block to the size specified, if able.
// Assumes: The block is "free" and size given includes room for header.
// Returns: A ptr to the original block, resized or not, depending on if able.
BlockHead *block_chunk(BlockHead *block, size_t size) {
    // Denote split address and resulting sizes
    BlockHead *block2 = (BlockHead*)((char*)block + size);
    size_t b2_size = block->size - size;
    size_t b1_size = block->size - b2_size;
    
    if (!block2)
         return NULL;
    
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

/* -- block_getheader --*/
// Given a ptr a to a data field, returns a ptr that field's mem block header.
BlockHead *block_getheader(void *ptr) {
    if (!ptr) 
        return NULL;
    return (BlockHead*)((void*)ptr - BLOCK_HEAD_SZ);
}

/* -- heap_free -- */
// Frees all unallocated memory blocks, and then the heap header itself.
// Assumes: When this function is called, all blocks in the heap are free.
void heap_free() {
    if (!g_heap) 
        return;
    
    while(g_heap->first_free) {
        BlockHead *freeme = g_heap->first_free;
        g_heap->first_free = g_heap->first_free->next;
        size_t sz_free = freeme->size;
        do_munmap(freeme, sz_free);
    }

    do_munmap((void*)g_heap, (size_t)g_heap + HEAP_HEAD_SZ);
    g_heap = NULL;
}

/* -- heap_squeeze -- */
// Combines the heap's free contiguous memory blocks
void heap_squeeze() {

    if (!g_heap->first_free)
        return;

    BlockHead *curr = g_heap->first_free;
    while(curr) {
        if (((char*)curr + curr->size) == (char*)curr->next)  {
            curr->size += curr->next->size;
            curr->next = curr->next->next;
            continue;
        }
        curr = curr->next;
    }
}


/* End Mem Helpers -------------------------------------------------------- */
/* Begin Linked List Helpers ---------------------------------------------- */


/* -- block_findfree -- */
// Searches for a mem block >= "size" bytes in the given heap's "free" list.
// Returns: On success, a ptr to the block found, else NULL;
void *block_findfree(size_t size) {
    BlockHead *curr = g_heap->first_free;
    
    // Find and return the first free mem block of at least the given size
    while (curr) {
        if (curr->size >= size)
            return curr;
        else
            curr = curr->next;
    }

    // Else, if no free block found, expand the heap to get one
    return heap_expand(size);
}

/* -- block_add_tofree -- */
// Adds the given block into the heap's "free" list.
// Assumes: Block is valid and does not already exist in the "free" list.
void block_add_tofree(BlockHead *block) {
    // If free list is empty, set us as first and return
    if (!g_heap->first_free) {
        g_heap->first_free = block;
        return;
    }

    // Else, find list insertion point (recall list is sorted ASC by address)
    BlockHead *curr = g_heap->first_free;
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

    // Else if inserting ourselves before all other blocks
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

/* -- block_rm_fromfree */
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


/* -- do_malloc -- */
// Allocates "size" bytes of memory to the requester.
// RETURNS: A ptr to the allocated memory location on success, else NULL.
void *do_malloc(size_t size) {
    if (!size)
        return NULL;

    // If heap not yet initialized, do it now
    if (!g_heap) 
        heap_init();

    // Make room for block header
    size += BLOCK_HEAD_SZ;

    // Find a free block >= needed size
    BlockHead *free_block = block_findfree(size);  // Expands heap as needed

    if (!free_block)
        return NULL;

    // Break up this block if it's larger than needed
    if (size < free_block->size)
        free_block = block_chunk(free_block, size);
        
    // Remove block from the "free" list and return ptr to its data field
    block_rm_fromfree(free_block);

    return free_block->data_addr;
}

/* -- do_calloc -- */
// Allocates an array of "nmemb" elements of "size" bytes, w/each set to 0
// RETURNS: A ptr to the mem addr on success, else NULL.
void *do_calloc(size_t nmemb, size_t size) {
    size_t total_sz = sizet_multiply(nmemb, size);

    if (total_sz)
        return mem_set(do_malloc(total_sz), 0, total_sz);
    return NULL;
}

/* -- do_free -- */
// Frees the memory space pointed to by ptr iff ptr != NULL
void do_free(void *ptr) {
    if (!ptr) 
        return;

    // Get ptr to header and add to "free" list
    block_add_tofree(block_getheader(ptr));

    // Determine total size of all free memory blocks, for use below
    size_t free_sz = 0;
    BlockHead *curr = g_heap->first_free;
    while(curr) {
        free_sz += curr->size;
        curr = curr->next ;
    }

    // If total sz free == heap size, free the heap - it reinits as needed
    if (free_sz == g_heap->size - HEAP_HEAD_SZ)
        heap_free();
}

/* -- do_realloc -- */
// Changes the size of the allocated memory at "ptr" to the given size.
// Returns: Ptr to the mapped mem address on success, else NULL.
void *do_realloc(void *ptr, size_t size) {
    // If size == 0, free mem at the given ptr
    if (!size) {
        do_free(ptr);
        return NULL;
    }
    
    // Else if ptr is NULL, do an malloc(size)
    if (!ptr)
        return do_malloc(size);
    
    // Else, reallocate the mem location
    BlockHead *new_block = do_malloc(size);
    BlockHead *old_block = block_getheader(ptr);

    size_t cpy_len = size;
    if (size > old_block->size)
        size = old_block->size;

    mem_cpy(new_block, ptr, cpy_len);
    do_free(ptr);

    return new_block;
}


/* End malloc, calloc, realloc, free -------------------------------------- */
/* Begin Test Code -------------------------------------------------------- */


/* --- main --- */
// Tests the do_calloc/do_realloc/do_free functions wholistically 
int main(int argc, char **argv) {
    // "string" container
    char *currline = do_malloc(0);;
    char *curr_start = currline;

    // Array of all strings
    char **arr = do_calloc(2, 8);
    char **arr_start = arr;

    // Create line 1, reallocating room for each new char
    currline = do_malloc(1);
    *currline = 'h';

    currline = do_realloc(currline, 2);
    curr_start = currline;
    currline++;
    *currline = 'i';

    currline = do_realloc(curr_start, 3);
    curr_start = currline;
    currline++;
    currline++;
    *currline = '\0';
    
    // Append line 1 to array
    *arr = curr_start;
    arr++;

    // Create line 2
    currline = do_malloc(3);
    curr_start = currline;
    *currline = 'b';
    currline++;
    *currline = 'y';
    currline++;
    *currline = 'e';
    currline++;
    *currline = '\0';
    
    // Append line 2 to array
    *arr = curr_start;
    
    // Output each line to stdout, freeing mem as we go
    for (int i = 0; i < 2; i++) {
        str_write(*arr_start);
        str_write("\n");
        do_free(*arr_start);
        arr_start++;
    }
    do_free(arr);
    
    return 0;
}
