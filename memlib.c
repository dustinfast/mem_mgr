// Defs and helper functions of the memory management system.
//
// TODO:
//      Macros for better perf?
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

#define START_HEAP_SZ (1)         // Heap megabytes * bytes in a mb
// #define START_HEAP_SZ (1 * 1048576)         // Heap megabytes * bytes in a mb
#define BLOCK_HEAD_SZ sizeof(BlockHead)     // Size of BlockHead struct (bytes)
#define HEAP_HEAD_SZ sizeof(HeapHead)       // Size of HeapHead struct (bytes)
#define MIN_BLOCK_SZ (BLOCK_HEAD_SZ + 1)    // Min block sz = header + 1 byte
#define WORD_SZ sizeof(void*)               // Word size on this architecture

void block_add_tofree(BlockHead *block);


/* END Definitions -------------------------------------------------------- */
/* StartPredefined Helpers ------------------------------------------------ */


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

// Fills the first n bytes at s with c
// RETURNS: ptr to s
// static void *__memset(void *s, int c, size_t n) {
//     char *p = (char *)s;
//     while(n) {
//         *p = (char)c;
//         p++;
//         n--;
//     }
//   return s;
// }

// // Copies n bytes from src to dest (mem areas must not overlap)
// // RETURNS: ptr to dest
// static void *__memcpy(void *dest, const void *src, size_t n) {
//     unsigned char *pd = (unsigned char *)dest;
//     const unsigned char *ps = (const unsigned char *)src;
//     while (n) {
//         *pd = *ps;
//         pd++;
//         ps++;
//         n--;
//     }
//     return dest;
// }

static void *__memset(void *s, int c, size_t n) {
  str_write("** In memset...\n");  // debug

  unsigned char *p;
  size_t i;

  if (n == ((size_t) 0)) return s;
  for (i=(size_t) 0,p=(unsigned char *)s;
       i<=(n-((size_t) 1));
       i++,p++) {
    *p = (unsigned char) c;
  }
  str_write("## OK memset...\n");  // debug
  return s;
}

static void *__memcpy(void *dest, const void *src, size_t n) {
  str_write("** In memcpy...\n");  // debug

  unsigned char *pd;
  const unsigned char *ps;
  size_t i;

  if (n == ((size_t) 0)) return dest;
  for (i=(size_t) 0,pd=(unsigned char *)dest,ps=(const unsigned char *)src;
       i<=(n-((size_t) 1));
       i++,pd++,ps++) {
    *pd = *ps;
  }
  str_write("## OK memcpy...\n");  // debug
  return dest;
}

int __try_size_t_multiply(size_t a, size_t b) {
  str_write("** In try multiply...\n");  // debug

  size_t t, r, q;

  /* If any of the arguments a and b is zero, everthing works just fine. */
  if ((a == ((size_t) 0)) || (b == ((size_t) 0)))
    return 0;

  /* Here, neither a nor b is zero. 

     We perform the multiplication, which may overflow, i.e. present
     some modulo-behavior.

  */
  t = a * b;

  /* Perform Euclidian division on t by a:

     t = a * q + r

     As we are sure that a is non-zero, we are sure
     that we will not divide by zero.

  */
  q = t / a;
  r = t % a;

  /* If the rest r is non-zero, the multiplication overflowed. */
  if (r != ((size_t) 0))
      return 0;

  /* Here the rest r is zero, so we are sure that t = a * q.

     If q is different from b, the multiplication overflowed.
     Otherwise we are sure that t = a * b.

  */
  if (q != b)
    return 0;

  str_write("## OK try multiply...\n");  // debug
  return t;
}

/* End Predefined Helpers ------------------------------------------------- */
/* Begin Mem Helpers ------------------------------------------------------ */


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


/* -- do_mmap -- */
// Allocates a new mem space of "size" bytes using the mmap syscall.
// Returns: On suceess, a ptr to the mapped address space, else NULL.
void *do_mmap(size_t size) {
    str_write("** In do_mmap...\n");  // debug

    int prot = PROT_EXEC | PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void *result = mmap(NULL, size, prot, flags, -1, 0);

    if (result == MAP_FAILED) {
        str_write("!! Fail do_mmap...\n");  // debug    
        result = NULL;
    }
    str_write("## OK do_mmap...\n");  // debug    
    return result;
}

/* -- do_munmap -- */
// Unmaps the memory at "addr" of "size" bytes using the munmap syscall.
// Returns: Nonzero on success, or -1 on fail.
int do_munmap(void *addr, size_t size) {
    str_write("** In do_munmap...\n");  // debug

    if (!size){
        str_write("!! Fail do_munmap...\n");  // debug
         return -1;
    }

    int r = munmap(addr, size);
    str_write("## OK do_munmap...\n");  // debug
    return r;
}

/* -- heap_init -- */
// Inits the global heap with one free memory block of maximal size.
void heap_init() {
    str_write("** In heap_init...\n");  // debug

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

    str_write("## OK heap_init...\n");  // debug
}

/* -- heap_expand -- */
// Adds a new block of at least "size" bytes to the heap. If "size" is less
//      than START_HEAP_SZ, START_HEAP_SZ bytes is added instead.
// Returns: On success, a ptr to the new block created, else NULL.
BlockHead *heap_expand(size_t size) {
    str_write("** In heap_expand...\n");  // debug
    
    if (size < START_HEAP_SZ) {
        size = START_HEAP_SZ;
    }

    // Allocate the new space as a memory block
    BlockHead *new_block = do_mmap(size);

    if (!new_block) {
        str_write("!! Fail heap_init...\n");  // debug
         return NULL;  // Ensure succesful mmap
    }

    // Init the new block
    new_block->size = size;
    new_block->data_addr = (char*)new_block + BLOCK_HEAD_SZ;
    new_block->next = NULL;
    new_block->prev = NULL;

    // Denote new size of the heap and add the new block as free
    g_heap->size += size;
    block_add_tofree(new_block);

    str_write("## OK heap_expand...\n");  // debug
    return new_block;
}

/* -- block_chunk -- */
// Repartitions the given block to the size specified, if able.
// Assumes: The block is "free" and size given includes room for header.
// Returns: A ptr to the original block, resized or not, depending on if able.
BlockHead *block_chunk(BlockHead *block, size_t size) {
    str_write("** In block_chunk...\n");  // debug

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

    str_write("## OK block_chunk...\n");  // debug
    return block;
}

/* -- block_getheader --*/
// Given a ptr a to a data field, returns a ptr that field's mem block header.
BlockHead *block_getheader(void *ptr) {
    str_write("** In block_getheader...\n");  // debug
    if (!ptr) return NULL;

    str_write("## OK block_getheader...\n");  // debug
    return (BlockHead*)((void*)ptr - BLOCK_HEAD_SZ);
}

/* -- heap_free -- */
// Frees all unallocated memory blocks, and then the heap header itself.
// Assumes: When this function is called, all blocks in the heap are free.
void heap_free() {
    str_write("** In heap_free...\n");  // debug

    if (!g_heap) return;
    
    while(g_heap->first_free) {
        BlockHead *freeme = g_heap->first_free;
        g_heap->first_free = g_heap->first_free->next;
        size_t sz_free = freeme->size;
        do_munmap(freeme, sz_free);
    }

    do_munmap((void*)g_heap, (size_t)g_heap + HEAP_HEAD_SZ);
    g_heap = NULL;
    str_write("## OK heap_free...\n");  // debug

}

/* -- heap_squeeze -- */
// Combines the heap's free contiguous memory blocks
void heap_squeeze() {
    str_write("** In heap_squeeze...\n");  // debug

    if (!g_heap->first_free)  return;

    BlockHead *curr = g_heap->first_free;
    while(curr) {
        if (((char*)curr + curr->size) == (char*)curr->next)  {
            curr->size += curr->next->size;
            curr->next = curr->next->next;
            continue;
        }
        curr = curr->next;
    }

    str_write("## OK heap_squeeze...\n");  // debug
}


/* End Mem Helpers -------------------------------------------------------- */
/* Begin Linked List Helpers ---------------------------------------------- */


/* -- block_findfree -- */
// Searches for a mem block >= "size" bytes in the given heap's "free" list.
// Returns: On success, a ptr to the block found, else NULL;
void *block_findfree(size_t size) {
    str_write("** In block_findfree...\n");  // debug

    BlockHead *curr = g_heap->first_free;
    
    while (curr) {
        if (curr->size >= size)
            return curr;
        else
            curr = curr->next;
    }

    // If no free block found, expand the heap to get one
    void *r = heap_expand(size);

    str_write("## OK block_findfree...\n");  // debug
    return r;
}

/* -- block_add_tofree -- */
// Adds the given block into the heap's "free" list.
// Assumes: Block is valid and does not already exist in the "free" list.
void block_add_tofree(BlockHead *block) {
    str_write("** In block_addtofree...\n");  // debug

    // If free list is empty, set us as first and return
    if (!g_heap->first_free) {
        g_heap->first_free = block;
        return;
    }

    // Else, find list insertion point (recall list is sorted ASC by address)
    BlockHead *curr = g_heap->first_free;
    while (curr) {
        if (curr > block)
            break;
        else
            curr = curr->next;
    }
        
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
    str_write("## OK block_addtofree...\n");  // debug
}

/* -- block_rm_fromfree */
// Removes the given block from the heap's "free" list.
void block_rm_fromfree(BlockHead *block) {
    str_write("** In block_rmfromfree...\n");  // debug

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

    str_write("## OK block_rmfromfree...\n");  // debug

}


/* End Linked List Helpers ------------------------------------------------ */
/* Begin malloc, calloc, realloc, free ------------------------------------ */


/* -- do_malloc -- */
// Allocates "size" bytes of memory to the requester.
// RETURNS: A ptr to the allocated memory location on success, else NULL.
void *do_malloc(size_t size) {
    str_write("** In do_malloc...\n");  // debug

    if (!size) return NULL;

    // If heap not yet initialized, do it now
    if (!g_heap) heap_init();

    // Make room for block header
    size += BLOCK_HEAD_SZ;

    // Find a free block >= needed size
    BlockHead *free_block = block_findfree(size);  // Expands heap as needed
    if (!free_block) {
        str_write("!! Fail do_malloc...\n");  // debug
        return NULL;
    }

    // Break up this block if it's larger than needed
    if (size < free_block->size)
        free_block = block_chunk(free_block, size);
    
    // Remove block from the "free" list and return ptr to its data field
    block_rm_fromfree(free_block);

    str_write("## OK do_malloc...\n");  // debug
    return free_block->data_addr;
}

/* -- do_calloc -- */
// Allocates an array of "nmemb" elements of "size" bytes, w/each set to 0
// RETURNS: A ptr to the mem addr on success, else NULL.
void *do_calloc(size_t nmemb, size_t size) {
    str_write("** In do_calloc...\n");  // debug

    size_t total_sz = __try_size_t_multiply(nmemb, size);

    if (total_sz) {
        void *r = __memset(do_malloc(total_sz), 0, total_sz);
        str_write("## OK do_calloc...\n");  // debug    
        return r;
    }
    
    str_write("!! Fail do_calloc...\n");  // debug    
    return NULL;
}

/* -- do_free -- */
// Frees the memory space pointed to by ptr iff ptr != NULL
void do_free(void *ptr) {
    str_write("** In do_free...\n");  // debug

    if (!ptr) return;

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

    str_write("## OK do_free...\n");  // debug    
}

/* -- do_realloc -- */
// Changes the size of the allocated memory at "ptr" to the given size.
// Returns: Ptr to the mapped mem address on success, else NULL.
void *do_realloc(void *ptr, size_t size) {
    str_write("** In do_realloc...\n");  // debug
    // If size == 0, do a free(ptr)
    if (!size) {
        do_free(ptr);
        return NULL;
    }
    
    // If ptr is NULL, do an malloc(size)
    if (!ptr) {
        str_write("## OK do_realloc...\n");  // debug
        return do_malloc(size);
    }

    // Else, reallocate the mem location
    BlockHead *new_block = do_malloc(size);
    BlockHead *old_block = block_getheader(ptr);

    size_t cpy_len = size;
    if (cpy_len > old_block->size)
        cpy_len = old_block->size;

    __memcpy(new_block, old_block, cpy_len);
    do_free(ptr);

    str_write("## OK do_realloc...\n");  // debug
    return new_block;
}


/* End malloc, calloc, realloc, free -------------------------------------- */
/* Begin Debug ------------------------------------------------------------ */


int main(int argc, char **argv) {
    // char *a = do_calloc(1, 1024);
    // do_free(a);

    
    return 0;
}
