/*  

    Copyright 2018 by

    University of Alaska Anchorage, College of Engineering.

    All rights reserved.

    Contributors:  ...
		   ...
		   ...                 and
		   Christoph Lauter

    See file memory.c on how to compile this code.

    Implement the functions __malloc_impl, __calloc_impl,
    __realloc_impl and __free_impl below. The functions must behave
    like malloc, calloc, realloc and free but your implementation must
    of course not be based on malloc, calloc, realloc and free.

    Use the mmap and munmap system calls to create private anonymous
    memory mappings and hence to get basic access to memory, as the
    kernel provides it. Implement your memory management functions
    based on that "raw" access to user space memory.

    As the mmap and munmap system calls are slow, you have to find a
    way to reduce the number of system calls, by "grouping" them into
    larger blocks of memory accesses. As a matter of course, this needs
    to be done sensibly, i.e. without spoiling too much memory.

    You must not use any functions provided by the system besides mmap
    and munmap. If you need memset and memcpy, use the naive
    implementations below. If they are too slow for your purpose,
    rewrite them in order to improve them!

    Catch all errors that may occur for mmap and munmap. In these cases
    make malloc/calloc/realloc/free just fail. Do not print out any 
    debug messages as this might get you into an infinite recursion!

    Your __calloc_impl will probably just call your __malloc_impl, check
    if that allocation worked and then set the fresh allocated memory
    to all zeros. Be aware that calloc comes with two size_t arguments
    and that malloc has only one. The classical multiplication of the two
    size_t arguments of calloc is wrong! Read this to convince yourself:

    https://cert.uni-stuttgart.de/ticker/advisories/calloc.en.html

    In order to allow you to properly refuse to perform the calloc instead
    of allocating too little memory, the __try_size_t_multiply function is
    provided below for your convenience.
    
*/

#include <stddef.h>
#include <sys/mman.h>


/* Predefined helper functions */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>


static int __memory_print_debug_running = 0;
static int __memory_print_debug_init_running = 0;
static int __memory_print_debug_initialized = 0;
static int __memory_print_debug_do_it = 0;

static pthread_mutex_t memory_management_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;

static void __memory_print_debug_init() {
  char *env_var;
  
  if (__memory_print_debug_init_running) return;
  __memory_print_debug_init_running = 1;
  if (!__memory_print_debug_initialized) {
    __memory_print_debug_do_it = 0;
    env_var = getenv("MEMORY_DEBUG");
    if (env_var != NULL) {
      if (!strcmp(env_var, "yes")) {
	__memory_print_debug_do_it = 1;
      }
    }
    __memory_print_debug_initialized = 1;
  }
  __memory_print_debug_init_running = 0;
}

static void __memory_print_debug(const char *fmt, ...) {
  va_list valist;

  if (pthread_mutex_trylock(&print_lock) != 0) return;
  if (__memory_print_debug_running) {
    pthread_mutex_unlock(&print_lock);
    return;
  }
  __memory_print_debug_running = 1;
  __memory_print_debug_init();
  if (__memory_print_debug_do_it) {
    va_start(valist, fmt);
    vfprintf(stderr, fmt, valist);
    va_end(valist);
  }
  __memory_print_debug_running = 0;
  pthread_mutex_unlock(&print_lock);
}

/* End of predefined helper functions */

/* Your helper functions */
/* Begin Definitions ----------------------------------------------------DF */

// Memory Block Header. Also serves double duty as a linked list node
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

static void block_add_tofree(BlockHead *block);


/* End Definitions ------------------------------------------------------DF */
/* Begin Utility Helpers ------------------------------------------------DF */


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


/* End Utility Helpers --------------------------------------------------DF */
/* Begin Mem Helpers ----------------------------------------------------DF */


/* -- do_mmap -- */
// Allocates a new mem space of "size" bytes using the mmap syscall.
// Returns: On suceess, a ptr to the mapped address space, else NULL.
static void *do_mmap(size_t size) {
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
static int do_munmap(void *addr, size_t size) {
    if (!size)
         return -1;
    return munmap(addr, size);
}

/* -- heap_init -- */
// Inits the global heap with one free memory block of maximal size.
static void heap_init() {
        __memory_print_debug("************ INIT \n");

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
static BlockHead *heap_expand(size_t size) {
    __memory_print_debug("**** EXPANDING: %u\n", size);

    if (size < START_HEAP_SZ)
        size = START_HEAP_SZ;
    // __memory_print_debug("**** EXPANDING NEW: %u\n", size);

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
static BlockHead *block_chunk(BlockHead *block, size_t size) {
    __memory_print_debug("** IN chunk\n");

    // Denote split address and resulting sizes
    BlockHead *block2 = (BlockHead*)((char*)block + size);
    size_t b2_size = block->size - size;
    size_t b1_size = block->size - b2_size;
    
    if (!block2)
         return NULL;

    // Ensure partitions are large enough to be split
    if (b2_size >= MIN_BLOCK_SZ && b1_size >= MIN_BLOCK_SZ) {
        // __memory_print_debug("**In1 : %u - %u\n", b1_size, b2_size);
        
        block->size = b1_size;
        block2->size = b2_size;
        block2->data_addr = (char*)block2 + BLOCK_HEAD_SZ;

        // Insert the new block between original block and the next (if any)
        // We do it here rather than with a call to block_add_tofree to avoid
        // the overhead of finding the insertion point - we already know it.
        block2->next = block->next;
        block->next = block2;
        block2->prev = block;

        if (block2->next)
            block2->next->prev = block2;

        __memory_print_debug("## Chunked b1: %u  b2:%u\n", block->size, block2->size);
    } else {
        __memory_print_debug("## Elected not to chunk: %u  from %u\n", b1_size, block->size);
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
static void heap_free() {
    __memory_print_debug("** IN heap_free\n");

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
static void heap_squeeze() {
    __memory_print_debug("** In heap_squeeze\n");

    if (!g_heap->first_free)
        return;

    BlockHead *curr = g_heap->first_free;
    __memory_print_debug("** 1a\n");
    while(curr) {
        __memory_print_debug("** 1b: %u\n", curr->next);
        if (((char*)curr + curr->size) == (char*)curr->next)  {
            __memory_print_debug("** 2\n");
            curr->size += curr->next->size;
            __memory_print_debug("** 3\n");
            curr->next = curr->next->next;
            __memory_print_debug("** 4\n");
            continue;
        }
        if (curr != curr->next)
            curr = curr->next;
        else {
            __memory_print_debug("BRROKE\n");
            return;
        }

    }
}


/* End Mem Helpers ------------------------------------------------------DF */
/* Begin Linked List Helpers --------------------------------------------DF */


/* -- block_findfree -- */
// Searches for a mem block >= "size" bytes in the given heap's "free" list.
// Returns: On success, a ptr to the block found, else NULL;
static void *block_findfree(size_t size) {
    __memory_print_debug("** IN findfree\n");

    BlockHead *curr = g_heap->first_free;

    // Find and return the first free mem block of at least the given size
    while (curr)
        if (curr->size >= size) {
            return curr;
        }
        else {
            curr = curr->next;
        }

    // Else, if no free block found, expand the heap to get one
    return heap_expand(size);
}

/* -- block_add_tofree -- */
// Adds the given block into the heap's "free" list.
// Assumes: Block is valid and does not already exist in the "free" list.
static void block_add_tofree(BlockHead *block) {
    __memory_print_debug("** IN add_tofree\n");

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
    
    // heap_squeeze();  // Combine any contiguous free blocks
}

/* -- block_rm_fromfree */
// Removes the given block from the heap's "free" list.
static void block_rm_fromfree(BlockHead *block) {
    __memory_print_debug("** IN rm_fromfree\n");

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


/* End Linked List Helpers ----------------------------------------------DF */
/* Begin malloc, calloc, realloc, free ----------------------------------DF */


/* -- do_malloc -- */
// Allocates "size" bytes of memory to the requester.
// RETURNS: A ptr to the allocated memory location on success, else NULL.
static void *do_malloc(size_t size) {
    __memory_print_debug("** IN do_malloc\n");

    if (!size)
        return NULL;

    // If heap not yet initialized, do it now
    if (!g_heap) 
        heap_init();

    // Make room for block header
    size += BLOCK_HEAD_SZ;

    // Find a free block >= needed size (expanding heap as needed)
    BlockHead *free_block = block_findfree(size);

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
static void *do_calloc(size_t nmemb, size_t size) {
    size_t total_sz = sizet_multiply(nmemb, size);

    if (total_sz)
        return mem_set(do_malloc(total_sz), 0, total_sz);
    
    return NULL;
}

/* -- do_free -- */
// Frees the memory space pointed to by ptr iff ptr != NULL
static void do_free(void *ptr) {
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
static void *do_realloc(void *ptr, size_t size) {
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

/* End malloc, calloc, realloc, free ------------------------------------DF */
/* End of your helper functions */

/* Start of the actual malloc/calloc/realloc/free functions */

void __free_impl(void *ptr) {
    do_free(ptr);
}

void *__malloc_impl(size_t size) {
    return do_malloc(size);
}

void *__calloc_impl(size_t nmemb, size_t size) {
    return do_calloc(nmemb, size);
}

void *__realloc_impl(void *ptr, size_t size) {
  return do_realloc(ptr, size);
}

/* End of the actual malloc/calloc/realloc/free functions */
