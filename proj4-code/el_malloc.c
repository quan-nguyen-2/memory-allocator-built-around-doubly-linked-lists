// el_malloc.c: implementation of explicit list allocator functions.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "el_malloc.h"

// Global control functions

// Global control variable for the allocator. Must be initialized in
// el_init().
el_ctl_t el_ctl = {};

// Create an initial block of memory for the heap using mmap(). Initialize the
// el_ctl data structure to point at this block. The initial size/position of
// the heap for the memory map are given in the symbols EL_HEAP_INITIAL_SIZE
// and EL_HEAP_START_ADDRESS. Initialize the lists in el_ctl to contain a
// single large block of available memory and no used blocks of memory.
int el_init() {
    void *heap = mmap(EL_HEAP_START_ADDRESS, EL_HEAP_INITIAL_SIZE,
                      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(heap == EL_HEAP_START_ADDRESS);

    el_ctl.heap_bytes = EL_HEAP_INITIAL_SIZE; // make the heap as big as possible to begin with
    el_ctl.heap_start = heap; // set addresses of start and end of heap
    el_ctl.heap_end = PTR_PLUS_BYTES(heap, el_ctl.heap_bytes);

    if (el_ctl.heap_bytes < EL_BLOCK_OVERHEAD) {
        fprintf(stderr,"el_init: heap size %ld to small for a block overhead %ld\n",
                el_ctl.heap_bytes,EL_BLOCK_OVERHEAD);
        return -1;
    }

    el_init_blocklist(&el_ctl.avail_actual);
    el_init_blocklist(&el_ctl.used_actual);
    el_ctl.avail = &el_ctl.avail_actual;
    el_ctl.used = &el_ctl.used_actual;

    // establish the first available block by filling in size in
    // block/foot and null links in head
    size_t size = el_ctl.heap_bytes - EL_BLOCK_OVERHEAD;
    el_blockhead_t *ablock = el_ctl.heap_start;
    ablock->size = size;
    ablock->state = EL_AVAILABLE;
    el_blockfoot_t *afoot = el_get_footer(ablock);
    afoot->size = size;
    el_add_block_front(el_ctl.avail, ablock);
    return 0;
}

// Clean up the heap area associated with the system
void el_cleanup() {
    munmap(el_ctl.heap_start, el_ctl.heap_bytes);
    el_ctl.heap_start = NULL;
    el_ctl.heap_end = NULL;
}

// Pointer arithmetic functions to access adjacent headers/footers

// Compute the address of the foot for the given head which is at a higher
// address than the head.
el_blockfoot_t *el_get_footer(el_blockhead_t *head) {
    size_t size = head->size;
    el_blockfoot_t *foot = PTR_PLUS_BYTES(head, sizeof(el_blockhead_t) + size);
    return foot;
}

// TODO
// Compute the address of the head for the given foot, which is at a
// lower address than the foot.
el_blockhead_t *el_get_header(el_blockfoot_t *foot){
  size_t size = foot->size; // Get the size of the block
  
  // Calculate the position of the block header
  // Move back from the foot by the size of header and block size
  el_blockhead_t *head = PTR_PLUS_BYTES(foot, -(sizeof(el_blockhead_t) + size));
  
  return head; // Return the pointer to the block header
}

// Return a pointer to the block that is one block higher in memory
// from the given block. This should be the size of the block plus
// the EL_BLOCK_OVERHEAD which is the space occupied by the header and
// footer. Returns NULL if the block above would be off the heap.
// DOES NOT follow next pointer, looks in adjacent memory.
el_blockhead_t *el_block_above(el_blockhead_t *block){
  // Calculate the pointer to the block above the given block
  el_blockhead_t *higher_block = PTR_PLUS_BYTES(block, block->size + EL_BLOCK_OVERHEAD);
  
  // Ensure the pointer doesn't exceed the heap_end pointer
  if((void *) higher_block >= (void*) el_ctl.heap_end){
    return NULL; // Return NULL if it exceeds heap_end
  } else {
    return higher_block; // Return the pointer to the block above
  }
}


// TODO
// Return a pointer to the block that is one block lower in memory
// from the given block. Uses the size of the preceding block found
// in its foot. DOES NOT follow block->next pointer, looks in adjacent
// memory. Returns NULL if the block below would be outside the heap.
//
// WARNING: This function must perform slightly different arithmetic
// than el_block_above(). Take care when implementing it.
el_blockhead_t *el_block_below(el_blockhead_t *block){
  // Check if the block is at the beginning of the heap
  if(block == el_ctl.heap_start) {
    return NULL; // Return NULL if it's at the start
  }
  
  // Calculate the pointer to the foot of the lower block
  el_blockfoot_t *lower_foot = PTR_MINUS_BYTES(block, sizeof(el_blockfoot_t));
  
  // Retrieve the header of the lower block
  el_blockhead_t *lower_head = el_get_header(lower_foot);
  
  return lower_head; // Return the header of the lower block
}


// Block list operations

// Print an entire blocklist. The format appears as follows.
//
// {length:   2  bytes:  3400}
//   [  0] head @ 0x600000000000 {state: a  size:   128}
//         foot @ 0x6000000000a0 {size:   128}
//   [  1] head @ 0x600000000360 {state: a  size:  3192}
//         foot @ 0x600000000ff8 {size:  3192}
//
// Note that the '@' column uses the actual address of items which
// relies on a consistent mmap() starting point for the heap.
void el_print_blocklist(el_blocklist_t *list) {
    printf("{length: %3lu  bytes: %5lu}\n", list->length, list->bytes);
    el_blockhead_t *block = list->beg;
    for (int i=0 ; i < list->length; i++) {
        printf("  ");
        block = block->next;
        printf("[%3d] head @ %p ", i, block);
        printf("{state: %c  size: %5lu}\n", block->state, block->size);
        el_blockfoot_t *foot = el_get_footer(block);
        printf("%6s", "");          // indent
        printf("  foot @ %p ", foot);
        printf("{size: %5lu}", foot->size);
        printf("\n");
    }
}

// Print out basic heap statistics. This shows total heap info along
// with the Available and Used Lists. The output format resembles the following.
//
// HEAP STATS (overhead per node: 40)
// heap_start:  0x600000000000
// heap_end:    0x600000001000
// total_bytes: 4096
// AVAILABLE LIST: {length:   2  bytes:  3400}
//   [  0] head @ 0x600000000000 {state: a  size:   128}
//         foot @ 0x6000000000a0 {size:   128}
//   [  1] head @ 0x600000000360 {state: a  size:  3192}
//         foot @ 0x600000000ff8 {size:  3192}
// USED LIST: {length:   3  bytes:   696}
//   [  0] head @ 0x600000000200 {state: u  size:   312}
//         foot @ 0x600000000358 {size:   312}
//   [  1] head @ 0x600000000198 {state: u  size:    64}
//         foot @ 0x6000000001f8 {size:    64}
//   [  2] head @ 0x6000000000a8 {state: u  size:   200}
//         foot @ 0x600000000190 {size:   200}
void el_print_stats() {
    printf("HEAP STATS (overhead per node: %lu)\n", EL_BLOCK_OVERHEAD);
    printf("heap_start:  %p\n", el_ctl.heap_start);
    printf("heap_end:    %p\n", el_ctl.heap_end);
    printf("total_bytes: %lu\n", el_ctl.heap_bytes);
    printf("AVAILABLE LIST: ");
    el_print_blocklist(el_ctl.avail);
    printf("USED LIST: ");
    el_print_blocklist(el_ctl.used);
}

// Initialize the specified list to be empty. Sets the beg/end
// pointers to the actual space and initializes those data to be the
// ends of the list. Initializes length and size to 0.
void el_init_blocklist(el_blocklist_t *list) {
    list->beg = &(list->beg_actual);
    list->beg->state = EL_BEGIN_BLOCK;
    list->beg->size = EL_UNINITIALIZED;
    list->end = &(list->end_actual);
    list->end->state = EL_END_BLOCK;
    list->end->size = EL_UNINITIALIZED;
    list->beg->next = list->end;
    list->beg->prev = NULL;
    list->end->next = NULL;
    list->end->prev = list->beg;
    list->length = 0;
    list->bytes = 0;
}

// TODO
// Add to the front of list; links for block are adjusted as are links
// within list. Length is incremented and the bytes for the list are
// updated to include the new block's size and its overhead.
void el_add_block_front(el_blocklist_t *list, el_blockhead_t *block){
   // Add the new block at the front of the list
   block->next = list->beg->next;
   block->prev = list->beg;

   // Update the list pointers to include the new block
   list->beg->next->prev = block;
   list->beg->next = block;

   // Update list metadata: increase block count and total bytes
   list->length++;
   list->bytes += block->size + EL_BLOCK_OVERHEAD; 
}


// TODO
// Unlink block from the specified list.
// Updates the length and bytes for that list including
// the EL_BLOCK_OVERHEAD bytes associated with header/footer.
void el_remove_block(el_blocklist_t *list, el_blockhead_t *block){
  el_blockhead_t *next_block = block->next;
  el_blockhead_t *prev_block = block->prev;

  // Adjust pointers to remove the block from the list
  if (next_block != NULL) {
    next_block->prev = prev_block;
  }
  if (prev_block != NULL) {
    prev_block->next = next_block;
  }

  // Update list metadata: decrement block count and bytes
  list->length--;
  list->bytes -= (block->size + EL_BLOCK_OVERHEAD);
}



// Allocation-related functions

// TODO
// Find the first block in the available list with block size of at
// least (size + EL_BLOCK_OVERHEAD). Overhead is accounted for so this
// routine may be used to find an available block to split: splitting
// requires adding in a new header/footer. Returns a pointer to the
// found block or NULL if no of sufficient size is available.
el_blockhead_t *el_find_first_avail(size_t size){
  // Start iterating from the beginning of the available block list
  el_blockhead_t *current_block = el_ctl.avail->beg->next;

  // Iterate until reaching the end of the available blocks
  while(current_block != el_ctl.avail->end){
    // Check if the current block can accommodate the requested size
    if(current_block->size >= size + EL_BLOCK_OVERHEAD) {
      return current_block; // Return the block if it fits the size requirements
    }
    current_block = current_block->next; // Move to the next block
  }
  return NULL; // Return NULL if no suitable block is found
}


// TODO
// Set the pointed to block to the given size and add a footer to it. Creates
// another block above it by creating a new header and assigning it the
// remaining space. Ensures that the new block has a footer with the correct
// size. Returns a pointer to the newly created block while the parameter block
// has its size altered to parameter size. Does not do any linking of blocks.
// If the parameter block does not have sufficient size for a split (at least
// new_size + EL_BLOCK_OVERHEAD for the new header/footer) makes no changes and
// returns NULL.
el_blockhead_t *el_split_block(el_blockhead_t *block, size_t new_size){
  // Ensure the block can be split into the requested size
  if(block->size < new_size + EL_BLOCK_OVERHEAD) {
    return NULL; // Return NULL if the block cannot be split
  }

  // Store references to the current block's header and footer
  el_blockhead_t *lower_head = block;
  el_blockfoot_t *upper_foot = el_get_footer(block);

  // Store the original size of the block
  size_t original_size = block->size;

  // Update the size of the lower block
  lower_head->size = new_size;

  // Update the size of the lower block's footer
  el_blockfoot_t *lower_foot = el_get_footer(block);
  lower_foot->size = new_size;

  // Calculate the header of the upper block
  el_blockhead_t *upper_head = el_block_above(lower_head);

  // Update the size of the upper block
  upper_head->size = original_size - new_size - EL_BLOCK_OVERHEAD;

  // Update the size of the upper block's footer
  upper_foot->size = upper_head->size;

  // Return the header of the upper block
  return upper_head;
}




// TODO
// Return pointer to a block of memory with at least the given size
// for use by the user. The pointer returned is to the usable space,
// not the block header. Makes use of find_first_avail() to find a
// suitable block and el_split_block() to split it. Returns NULL if
// no space is available.
void *el_malloc(size_t nbytes){
  // Find an available block that fits the requested size
  el_blockhead_t *user_block = el_find_first_avail(nbytes);

  // Return NULL if no suitable block is found
  if (!user_block) {
    return NULL;
  }

  // Remove the found block from the available list
  el_remove_block(el_ctl.avail, user_block);

  // Split the block into the requested size and get any remaining block
  el_blockhead_t *remaining_block = el_split_block(user_block, nbytes);

  // Add the user block to the used list
  el_add_block_front(el_ctl.used, user_block);
  user_block->state = EL_USED;

  // If there's a remaining block after splitting, add it to the available list
  if (remaining_block) {
    el_add_block_front(el_ctl.avail, remaining_block);
    remaining_block->state = EL_AVAILABLE;
  }

  // Calculate and return the pointer for the user data
  void *user_ptr = PTR_PLUS_BYTES(user_block, sizeof(el_blockhead_t));
  return user_ptr;
}




// De-allocation/free() related functions

// TODO
// Attempt to merge the block 'lower' with the next block in memory. Does
// nothing if lower is NULL or not EL_AVAILABLE and does nothing if the next
// higher block is NULL (because lower is the last block) or not EL_AVAILABLE.
//
// Otherwise, locates the next block with el_block_above() and merges these two
// into a single block. Adjusts the fields of lower to incorporate the size of
// higher block and the reclaimed overhead. Adjusts footer of higher to
// indicate the two blocks are merged. Removes both lower and higher from the
// available list and re-adds lower to the front of the available list.
void el_merge_block_with_above(el_blockhead_t *lower){
  // Check if the lower block or its upper block are not available for merging
  if(!lower || lower->state == EL_USED) {
    return;
  }

  // Find the upper block of the lower block
  el_blockhead_t *higher = el_block_above(lower);

  // Check if the higher block exists and is available for merging
  if(!higher || higher->state == EL_USED) {
    return;
  }

  // Calculate the combined size of the merged blocks
  size_t new_size = lower->size + higher->size + EL_BLOCK_OVERHEAD;

  // Remove both blocks from the available list
  el_remove_block(el_ctl.avail, lower);
  el_remove_block(el_ctl.avail, higher);

  // Update the size of the lower block to represent the merged size
  lower->size = new_size;

  // Get the footer of the higher block and update its size as well
  el_blockfoot_t *foot = el_get_footer(higher);
  foot->size = new_size;

  // Add the merged block back to the available list
  el_add_block_front(el_ctl.avail, lower);
}







// TODO
// Free the block pointed to by the given ptr. The area immediately
// preceding the pointer should contain an el_blockhead_t with information
// on the block size. Attempts to merge the free'd block with adjacent
// blocks using el_merge_block_with_above().
void el_free(void *ptr){
  // Calculate the block header from the given pointer
  el_blockhead_t *user_block = PTR_PLUS_BYTES(ptr, -sizeof(el_blockhead_t));

  // Check if the block is already available; if so, no action required
  if(user_block->state == EL_AVAILABLE) {
    return;
  }

  // Remove the block from the used list and mark it as available
  el_remove_block(el_ctl.used, user_block);
  user_block->state = EL_AVAILABLE;

  // Add the block to the available list
  el_add_block_front(el_ctl.avail, user_block);

  // Merge the block with the one above and below if possible
  el_merge_block_with_above(user_block);
  el_merge_block_with_above(el_block_below(user_block));
}

