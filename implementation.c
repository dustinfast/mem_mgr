/*  

    Copyright 2018 ba

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

static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;

static void __memory_print_debug_init() {
  char *env_var;
  
  if (__memory_print_debug_init_running) return;
  __memory_print_debug_init_running = 1;
  if (!__memory_print_debug_initialized) {
    __memory_print_debug_do_it = 0;
    env_var = getenv("MEMORY_DEBUG");
    if (env_var != NULL) {
      if (!strcmp(env_var, "b") || !strcmp(env_var, "both")) {
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
    size_t size;            // Total sz of heap+blocks+headers, in bytes
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

// Debug function, prints the given BlockHead's properties.
void block_print(BlockHead *block) {
     __memory_print_debug("-----\nBlock\n");
     __memory_print_debug("Size (bytes): %u\n", block->size);
     __memory_print_debug("SAddr: %u\n", block);
     __memory_print_debug("DAddr: %u\n", block->data_addr);
     __memory_print_debug("Prev: %u\n", block->prev);
     __memory_print_debug("Next: %u\n", block->next);
}

// Debug function, prints the global heap's properties.
void heap_print() {
     __memory_print_debug("-----\nHeap\n");
     __memory_print_debug("Size (bytes): %u\n", g_heap->size);
     __memory_print_debug("Start: %u\n", g_heap->start_addr);
     __memory_print_debug("FirstFree: %u\n", g_heap->first_free);

    BlockHead *next = g_heap->first_free;
    while(next) {
        block_print(next);
        next = next->next;
    }
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
    // __memory_print_debug("** IN chunk: b1= %u, b1sz= %u, reqsize= %u\n", block, block->size, size);
    // __memory_print_debug("** IN chunk\n");

    // Denote split address and resulting sizes
    BlockHead *block2 = (BlockHead*)((char*)block + size);
    size_t b2_size = block->size - size;
    size_t b1_size = block->size - b2_size;
    
    if (!block2)
         return NULL;

    // Ensure partitions are large enough to be split
    if (b2_size >= MIN_BLOCK_SZ && b1_size >= MIN_BLOCK_SZ) {        
        // block_print(block);
        //  __memory_print_debug("\n");
        
        block->size = b1_size;
        block2->size = b2_size;
        block2->data_addr = (char*)block2 + BLOCK_HEAD_SZ;

        // Insert the new block between original block and the next (if any)
        // We do it here rather than with a call to block_add_tofree to avoid
        // the overhead of finding the insertion point - we already know it.
        if (block->next)
            block->next->prev = block2;

        block2->next = block->next;
        block->next = block2;
        block2->prev = block;

        // block_print(block);
        // block_print(block2);
        // __memory_print_debug("-- DONE IN chunk: Did b1= %u, b1sz= %u\n", block, block->size, size);
        // __memory_print_debug("                      b2= %u, b2sz= %u\n", block2, block2->size, size);
    }
    // else {
    //     __memory_print_debug("-- DONE IN chunk (did not chunk)\n");
    // }
        // __memory_print_debug("-- DONE IN chunk\n");


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
// Frees all unallocated memory blocks, and then the heap itself.
// Assumes: When this function is called, all blocks in the heap are free and
// all contiguous blocks have been combined.
static void heap_free() {
    if (!g_heap) 
        return;

    __memory_print_debug("\n####### IN heap_free: heap = %u, sz = %u\n", g_heap, g_heap->size);
    heap_print();    
    
    // Free each non-contiguous block
    BlockHead *curr = g_heap->first_free;
    while(curr && curr->next) {
        // Advance in list until finding a next block that is non-contiguous
        if (((char*)curr + curr->size) != (char*)curr->next)  {
            // Free that next block
            BlockHead *freeme = curr->next;
            curr = curr->next->next;

            g_heap->size -= freeme->size;

            int r1 = do_munmap(freeme, freeme->size);
            __memory_print_debug("** IN Freeing %u gave %d\n", g_heap, r1);

        } else {
            curr = curr->next;
        }
    }

    // The only block left should now be the single block the heap started with,
    // which can be freed all at once with the header

    size_t freeme_sz = g_heap->size;  // debug
    int r2 = do_munmap((void*)g_heap, freeme_sz);
    // __memory_print_debug("** Freeing HEAP at %u for sz %u gave %d\n", g_heap, freeme_sz, r2);

    g_heap = NULL;
    __memory_print_debug("####### DONE IN heap_free\n");

}

/* -- heap_squeeze -- */
// Combines the heap's free contiguous memory blocks
static void heap_squeeze() {
    
    if (!g_heap->first_free)
        return;

    // __memory_print_debug("** IN heap_squeeze\n");
    // heap_print();
    BlockHead *curr = g_heap->first_free;
    while(curr) {
        if (((char*)curr + curr->size) == (char*)curr->next)  {

            // __memory_print_debug("** A CURR:\n");
            // block_print(curr);
            // __memory_print_debug("** A CURR->Next:\n");
            // if (curr->next)
            //     block_print(curr->next);

            curr->size += curr->next->size;
            curr->next = curr->next->next;
            if (curr->next)
                curr->next->prev = curr;

            // __memory_print_debug("** B CURR:\n");
            // block_print(curr);
            // __memory_print_debug("** B CURR->Next:\n");
            // if (curr->next)
            //     block_print(curr->next);

            continue;
        }
        curr = curr->next;
    }
    // __memory_print_debug("-- DID heap_squeeze\n");
    // heap_print();

}


/* End Mem Helpers ------------------------------------------------------DF */
/* Begin Linked List Helpers --------------------------------------------DF */


/* -- block_findfree -- */
// Searches for a mem block >= "size" bytes in the given heap's "free" list.
// Returns: On success, a ptr to the block found, else NULL;
static void *block_findfree(size_t size) {
    // __memory_print_debug("** IN findfree\n");

    BlockHead *curr = g_heap->first_free;

    // Find and return the first free mem block of at least the given size
    while (curr) {
        if (curr->size >= size) {
            // __memory_print_debug("-- DONE IN findfree (no expand)\n");
            return curr;
        }
        else {
            curr = curr->next;
        }
    }

    // __memory_print_debug("-- DONE IN findfree (w/expand)\n");
    
    // Else, if no free block found, expand the heap to get one
    return heap_expand(size);
}

/* -- block_add_tofree -- */
// Adds the given block into the heap's "free" list.
// Assumes: Block is valid and does not already exist in the "free" list.
static void block_add_tofree(BlockHead *block) {
    // __memory_print_debug("** IN add_tofree\n");
    // block_print(block);
    // If free list is empty, set us as first and return
    if (!g_heap->first_free) {
        g_heap->first_free = block;
        // __memory_print_debug("-- DONE IN add_tofree 0\n");
        return;
    }

    // Else, find list insertion point (recall list is sorted ASC by address)
    BlockHead *curr = g_heap->first_free;
    while (curr)
        if (curr > block)
            break;
        else
            curr = curr->next;
       
    // If no smaller addr found, assume head is smaller and insert just after it
    if (!curr) { 
        // __memory_print_debug("** add_tofree 1:\n");

            if (!(block == g_heap->first_free)) {
                block->prev = g_heap->first_free;
                g_heap->first_free->next = block;
            }
        // __memory_print_debug("** add_tofree 1b:\n");
           
    // block_print(block);
    }

    // Else if inserting ourselves before all other blocks
    else if (curr == g_heap->first_free) {
        // __memory_print_debug("** add_tofree 2:\n");

        block->next = curr;
        curr->prev = block;
        g_heap->first_free = block;
        // __memory_print_debug("** add_tofree 2b:\n");

    }

    // Else, insert ourselves immediately before the curr block
    else {
        // __memory_print_debug("** add_tofree 3:\n");

        if (curr->prev)
            curr->prev->next = block;
        // __memory_print_debug("** add_tofree 3b:\n");
        block->prev = curr->prev;
        // __memory_print_debug("** add_tofree 3c:\n");
        block->next = curr;
        // __memory_print_debug("** add_tofree 3d:\n");
        curr->prev = block;
        // __memory_print_debug("** add_tofree 3e:\n");

    }
    // __memory_print_debug("-- DONE IN add_tofree\n");
    
    heap_squeeze();  // Combine any contiguous free blocks
    
    // __memory_print_debug("-- DONE2 add_tofree\n");
}

/* -- block_rm_fromfree */
// Removes the given block from the heap's "free" list.
static void block_rm_fromfree(BlockHead *block) {
    // __memory_print_debug("** IN rm_fromfree\n");

    BlockHead *next = block->next;
    BlockHead *prev = block->prev;
    // __memory_print_debug("** 1\n");

    // If not at EOL, next node's "prev" becomes the node before us
    if (next)
        next->prev = prev;
    // __memory_print_debug("** 2\n");

    // If we're the head of the list, the next node becomes the new head
    if (block == g_heap->first_free) {
        // __memory_print_debug("** 3\n");
        if (next)
            next->prev = NULL;
        g_heap->first_free = next;
        // __memory_print_debug("** 3b\n");
    } else if (prev && prev->next) {
    // Else, the prev node gets linked to the node ahead of us
        // __memory_print_debug("** 4\n");
        prev->next = next;
        // __memory_print_debug("** 4b\n");

    }

    // Clear linked list info - it's no longer relevent
    block->prev = NULL;
    block->next = NULL;
    // __memory_print_debug("-- DONE IN rm_fromfree\n");

}


/* End Linked List Helpers ----------------------------------------------DF */
/* Begin malloc, calloc, realloc, free ----------------------------------DF */


/* -- do_malloc -- */
static size_t g_allocsz = 0;
static size_t g_freesz = 0;
// Allocates "size" bytes of memory to the requester.
// RETURNS: A ptr to the allocated memory location on success, else NULL.
static void *do_malloc(size_t size) {
    // __memory_print_debug("++ IN do_malloc\n");

    if (!size)
        return NULL;

    // If heap not yet initialized, do it now
    if (!g_heap) 
        heap_init();

    // Make room for block header
    size += BLOCK_HEAD_SZ;

    // Find a free block >= needed size (expands heap as needed)
    BlockHead *free_block = block_findfree(size);

    if (!free_block)
        return NULL;

    // Break up this block if it's larger than needed
    if (size < free_block->size)
        free_block = block_chunk(free_block, size);
    
    g_allocsz += free_block->size;  // debug
    // __memory_print_debug("** IN do_malloc: allocated %u\n", free_block->size);
    __memory_print_debug("** DID malloc - total allocated=%u\n", g_allocsz);
    // block_print(free_block);  // debug
    // Remove block from the "free" list and return ptr to its data field
    block_rm_fromfree(free_block);

    // __memory_print_debug("-- DONE IN do_malloc\n");

    return free_block->data_addr;
}

/* -- do_calloc -- */
// Allocates an array of "nmemb" elements of "size" bytes, w/each set to 0
// RETURNS: A ptr to the mem addr on success, else NULL.
static void *do_calloc(size_t nmemb, size_t size) {
    // __memory_print_debug("++ IN do_calloc\n");
    size_t total_sz = sizet_multiply(nmemb, size);
    return mem_set(do_malloc(total_sz), 0, total_sz);
}

/* -- do_free -- */
// Frees the memory space pointed to by ptr iff ptr != NULL
static void do_free(void *ptr) {
    if (!ptr) 
        return;
    
    // __memory_print_debug("++ IN do_free\n");

    // debug
    // Determine total size of all free memory blocks, for use below
    size_t free_sz_pre = 0;
    BlockHead *curr_pre = g_heap->first_free;
    while(curr_pre) {
        free_sz_pre += curr_pre->size;
        curr_pre = curr_pre->next ;
    }
    // __memory_print_debug("** IN do_free: PRE g_allocsz= %u \n", g_allocsz);

    // g_freesz += block_getheader(ptr)->size;  // debug
    g_allocsz -= block_getheader(ptr)->size;  // debug
    __memory_print_debug("** IN do_free: Freeing %u \n", block_getheader(ptr)->size);
    
    // Get ptr to header and add to "free" list
    block_add_tofree(block_getheader(ptr));

    // Determine total size of all free memory blocks, for use below
    // size_t free_sz = 0;
    // BlockHead *curr = g_heap->first_free;
    // while(curr) {
    //     free_sz += curr->size;
    //     curr = curr->next ;
    // }

    // __memory_print_debug("** IN do_free Result: %u free (heapsz-header=%u)\n", free_sz, (g_heap->size - HEAP_HEAD_SZ));
    // __memory_print_debug("** IN do_free G_TOTALS: %u freed. %u still allocated\n", g_freesz, g_allocsz);
    // __memory_print_debug("----\n");

    // If total sz free == heap size, free the heap - it reinits as needed
    // if (free_sz == g_heap->size - HEAP_HEAD_SZ) {

    if(g_allocsz == 0)
        heap_free();
    
    // __memory_print_debug("-- DONE IN do_free\n");

}

/* -- do_realloc -- */
// Changes the size of the allocated memory at "ptr" to the given size.
// Returns: Ptr to the mapped mem address on success, else NULL.
static void *do_realloc(void *ptr, size_t size) {
    __memory_print_debug("++ IN do_realloc\n");
    // If size == 0, free mem at the given ptr
    if (!size) {
        do_free(ptr);
        return NULL;
    }
    
    // Else if ptr is NULL, do an malloc(size)
    if (!ptr) {
        // __memory_print_debug("\n-- DONE IN do_realloc\n");
        return do_malloc(size);
    }

    // Else, reallocate the mem location
    char *new_block_data = do_malloc(size);
    BlockHead *new_block = block_getheader(new_block_data);
    BlockHead *old_block = block_getheader(ptr);

    // __memory_print_debug("** IN realloc: old_sz=%u, new_sz=%u\n", old_block->size, new_block->size);

    size_t cpy_len = size;
    if (size > old_block->size)
        cpy_len = old_block->size;

    mem_cpy(new_block_data, ptr, cpy_len);
    do_free(ptr);

    __memory_print_debug("** DONE in realloc - total allocated=%u\n", g_allocsz);
    
    return new_block_data;
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