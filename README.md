# Homework 3: 

## Memory Block and Heap Structure

Our heap and memory blocks are structured like so:
```
 Memory Block       Memory Block           Heap
(Unallocated)       (Allocated)       --------------(*c)
-------------(*a)  -------------      |  HeapHead  |      
| BlockHead |      | BlockHead |      --------------  
-------------      -------------(*b)  |     ...    |  
|    ...    |      |    ...    |      |   (blocks  |
|   (data   |      |   (data   |      |    field)  |
|   field)  |      |   field)  |      |     ...    |
|    ...    |      |    ...    |      |     ...    |
-------------      -------------      --------------
                                      Note: 'blocks field' not
                                      necessarily contiguous in
                                      memory.
```

## Walkthrough

1. The heap may not exist when a memory allocation is requested. If
not, it is initialized to START_HEAP_SZ mbs with a single free memory block occupying it's entire "blocks" field. The block's data field is then `START_HEAP_SZ - HEAP_HEAD_SZ - BLOCK_HEAD_SZ` bytes wide, however it is immediately chunked (or expanded) to serve the current allocation request (and any sunseqent allocation requests).
2. If an allocation request cannot be served because a chunk of at least the requested size (plus it's header) is not available, the heap is expanded with another block of  AT LEAST size `START_HEAP_SZ`.
3. The heap header contains a ptr to the head of a doubly linked list of currently unallocated memory blocks. The "nodes" of this list are the headers of each memory block in the list - i.e., each mem block header has a "next" and "prev" ptr.
4. We pass around free memory blocks by their block header (*a). When a block is not free, we don't do anything with it - it belongs to the caller, who know nothing about the header, as calls to malloc, calloc, and realloc return ptrs to the data fields inside the blocks (*b), rather than to the block headers themselves.
5. When a `free(ptr)` request is made, we step back `BLOCK_HEAD_SZ` bytes from `ptr`, to the start of it's block header so we can work with it as one of our blocks.
6. If at any time the heap contains free blocks occupying it's entire blocks field (accounting for non-contiguous space), the heap is freed. In this way, we:  
        a. Avoid making a syscall for every allocation request.  
        b. Ensure that all mem is freed (though this is dependent on all callers release everything we allocate to them).
        
## Design Decisions
The kind of list to use (singly-linked/doubly-linked) as well as the structure of the memory blocks and heap had to be determined beforehand, as they dictated the the rest of the application. Each possible choice had performance implications, and ultimately the following the were chosen - 
1. **A doubly linked list**: Although implementing a doubly linked list was more cumbersome, it allowed quickly stepping forward and back to prev/next blocks, versus iterating the whole list each time.
2. **Memory Blocks**: It was decided that each allocated memory block would maintain its header, rather than freeing the header and re-attaching it when the location was released by the caller. This eliminated the extra overhead of removing/reattaching it on each allocate/free cycle.
3. **Heap Structure**: Accounting for non-contiguous memory within the heap's data blocks field (resulting from expanding the heap) initially presented a challenge, however it could not be overlooked; an implementation limited to only an intial heap size, however large it might have been, would have not only been poor style, but fell outside the specification given in the assignment.