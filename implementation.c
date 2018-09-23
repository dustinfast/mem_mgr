/*  Copyright 2018 - University of Alaska Anchorage, College of Engineering.
    All rights reserved.

    Contributors:  
		   Skeleton provided by Christoph Lauter.
           Modified by Dustin Fast, 2018, for CSCE A321, "Assignment 3".

    Usage:
        See file memory.c on how to compile this code.

    TODO:
        Implement the following functions based on "raw" access to user space
        memory as provided by the kernel. Use mmap and munmap to create private
        anonymous mem mappings for acces to that mem.
            __malloc_impl   : behavior = malloc
            __calloc_impl   : behvaior = calloc
            __realloc_impl  : behavior = realloc
            __free_imp      : behavior = free


        Note: mmap and munmap sys calls are slow - need to reduce num calls
        by "grouping" them into larger block of mem accesses.

    Notes:
        If you need memset and memcpy, use the naive implementations below.

        Catch all mmap and munmap - don't print err msg, just fail

    calloc multiplication issue:
        Your __calloc_impl will probably just call your __malloc_impl, check
        if that allocation worked and then set the fresh allocated memory
        to all zeros. Be aware that calloc comes with two size_t arguments
        and that malloc has only one. The classical multiplication of the two
        size_t arguments of calloc is wrong:
        https://cert.uni-stuttgart.de/ticker/advisories/calloc.en.html

        In order to allow you to properly refuse to perform the calloc instead
        of allocating too little memory, the __try_size_t_multiply function is
        provided below for your convenience.
    
*/

#include <stddef.h>
#include <sys/mman.h>


/////////////////////////////////
/* Predefined helper functions */

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
// RETURNS: If no overflow, returns 1 after &setting c = a * b
//          Else, just returns 0
static int __try_size_t_multiply(size_t *c, size_t a, size_t b) {
  // If a == 0 or b == 0, no overflow
  if ((a == ((size_t) 0)) ||
      (a == ((size_t) 0))) {
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

/* End of predefined helper functions */
////////////////////////////////////////

////////////////////////////
/* Your helper functions */

// Define struct

// Define typedef 

// Global ptr to linked list of currfree mem blocks, ordered by addresses (ASC)

/* End of your helper functions */
/////////////////////////////////


//////////////////////////////////////////////////////////////
/* Start of the actual malloc/calloc/realloc/free functions */

void __free_impl(void *);

void *__malloc_impl(size_t size) {
  /* STUB */
  return NULL;
}

void *__calloc_impl(size_t nmemb, size_t size) {
  /* STUB */
  return NULL;  
}

void *__realloc_impl(void *ptr, size_t size) {
  /* STUB */
  return NULL;  
}

void __free_impl(void *ptr) {
  /* STUB */
}

/* End of the actual malloc/calloc/realloc/free functions */
////////////////////////////////////////////////////////////
