/*  

    Copyright 2018 by

    University of Alaska Anchorage, College of Engineering.

    All rights reserved.

    Contributor: Christoph Lauter

    Compile this file like that:

    gcc -fPIC -Wall -g -O0 -c memory.c 
    gcc -fPIC -Wall -g -O0 -c implementation.c
    gcc -fPIC -shared -o memory.so memory.o implementation.o -lpthread

    To try the code out:

    export LD_LIBRARY_PATH=`pwd`:"$LD_LIBRARY_PATH"
    export LD_PRELOAD=`pwd`/memory.so 
    export MEMORY_DEBUG=yes
    ls

    (If you are building elsewhere than you are testing, adapt 
     the `pwd` statement to your environment.)

    You will see a debug message on stderr per
    malloc/calloc/realloc/free call.

    If you still want to use your memory management implementation
    but you don't need the debug messages, set MEMORY_DEBUG to no
    before starting the process.

    You do not need to change anything in this file. You don't need to
    understand this file but it may be a good learning exercise to
    understand it. Your actual implementation goes into the file
    implementation.c, which also has some predefined parts.

    Be careful while debugging your code: 

    * Run your tests in another terminal as the one you use for
      compilation or don't export the environment variables. When a
      terminal is set up as shown above (with the export of the
      LD_PRELOAD variable), *any* command runs with your (buggy ?)
      implementation of malloc/calloc/realloc/free.  This means even
      the compiler runs with that implementation.

    * If you want to use gdb to debug your code, you can get inspiration
      on how to setup gdb in such a way that gdb does not use the (buggy?) 
      malloc/calloc/realloc/free but the application does on the following
      site:

      https://stackoverflow.com/questions/10448254/how-to-use-gdb-with-ld-preload

    * Write a test program (with main() etc.) that exercises the
      corner cases of malloc/calloc/realloc/free (e.g. malloc of size
      0, realloc with exiting pointer NULL, realloc with size 0 etc.)
      Compare runs with the libc malloc/calloc/realloc/free
      implementation with your implementation. The manual page
      man malloc is your friend.

    * Find a way to make sure that you are not leaking any mappings
      created with mmap. All mappings must be unmapped using munmap,
      at least for programs that free all their memory before exiting.

    * The ultimate test is to run some huge application such as libreoffice
      or chromium with your wrapper. It can be slow but it must not crash.

    * Don't get frustrated too early. It can be done - your instructor
      implemented the whole thing and got it to the point where it
      runs libreoffice. However: start working on this assignment really 
      early: it takes some time to debug it. It took the instructor about 6h
      to implement his version.

*/

#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>


void *__malloc_impl(size_t);
void *__calloc_impl(size_t, size_t);
void *__realloc_impl(void *, size_t);
void __free_impl(void *);

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

void *malloc(size_t size) {
  void *ptr;

  __memory_print_debug("TRYING: malloc(0x%zx)\n", size);
  pthread_mutex_lock(&memory_management_lock);
  ptr = __malloc_impl(size);
  pthread_mutex_unlock(&memory_management_lock);
  __memory_print_debug("RESULT: malloc(0x%zx) = %p\n", size, ptr);
  return ptr;
}

void *calloc(size_t nmemb, size_t size) {
  void *ptr;

  __memory_print_debug("TRYING: calloc(0x%zx, 0x%zx)\n", nmemb, size);
  pthread_mutex_lock(&memory_management_lock);
  ptr = __calloc_impl(nmemb, size);
  pthread_mutex_unlock(&memory_management_lock);
  __memory_print_debug("RESULT: calloc(0x%zx, 0x%zx) = %p\n", nmemb, size, ptr);
  return ptr;
}

void *realloc(void *old_ptr, size_t size) {
  void *ptr;

  __memory_print_debug("TRYING: realloc(%p, 0x%zx)\n", old_ptr, size);
  pthread_mutex_lock(&memory_management_lock);
  ptr = __realloc_impl(old_ptr, size);
  pthread_mutex_unlock(&memory_management_lock);
  __memory_print_debug("RESULT: realloc(%p, 0x%zx) = %p\n", old_ptr, size, ptr);
  return ptr;
}

void free(void *ptr) {
  __memory_print_debug("TRYING: free(%p)\n", ptr);
  pthread_mutex_lock(&memory_management_lock);
  __free_impl(ptr);
  pthread_mutex_unlock(&memory_management_lock);
  __memory_print_debug("RESULT: free(%p)\n", ptr);
}
