#include "my_malloc.h"

/* You *MUST* use this macro when calling my_sbrk to allocate the
 * appropriate size. Failure to do so may result in an incorrect
 * grading!
 */
#define SBRK_SIZE 2048

/* Please use this value as your canary! */
#define CANARY 0x2110CAFE

/* Size of the metadata and 2 canaries */
#define METASIZE (sizeof(metadata_t) + 2*sizeof(int))

/* If you want to use debugging printouts, it is HIGHLY recommended
 * to use this macro or something similar. If you produce output from
 * your code then you may receive a 20 point deduction. You have been
 * warned.
 */
#ifdef DEBUG
#define DEBUG_PRINT(x) printf x
#else
#define DEBUG_PRINT(x)
#endif


/* our freelist structure - this is where the current freelist of
 * blocks will be maintained. failure to maintain the list inside
 * of this structure will result in no credit, as the grader will
 * expect it to be maintained here.
 * Technically this should be declared static for the same reasons
 * as above, but DO NOT CHANGE the way this structure is declared
 * or it will break the autograder.
 */
metadata_t* freelist;

void* my_malloc(size_t size)
{
	// true amount of space needed accounting for metadata and canaries
	size_t realSize = size + METASIZE;

	// if they ask for too much, return null and set error to too large request
	if (realSize > SBRK_SIZE) {
		ERRNO = SINGLE_REQUEST_TOO_LARGE;
		return NULL;
	} else {
		// If no free data, make an sbrk call
		if (freelist == NULL) {
			void* newSBRK = my_sbrk(SBRK_SIZE);
			// If sbrk returns null, there's not enough memory, so set the error and return null
			if (newSBRK == NULL) {
				ERRNO = OUT_OF_MEMORY;
				return NULL;

			} else {

				// Otherwise, make new metadata for this chunk of memory and put it in the freelist
				metadata_t md = {SBRK_SIZE, 0, NULL, NULL};
				*((metadata_t*)newSBRK) = md;
				freelist = newSBRK;
			}
		}

		// Now that we know we have free memory, we iterate through to find the smallest block that will fit our needs
		metadata_t* curr = freelist;
		metadata_t* prev = NULL;
		metadata_t* min = NULL;
		while (curr != NULL) {
			if (curr->block_size > realSize) {
				if (min == NULL || min->block_size > curr->block_size) {
					min = curr;
				}
			}
			prev = curr;
			curr = curr->next;
		}


		// If min is null, none of our free memory was large enough, so we need to get more with sbrk
		if (min == NULL) {

			void* newSBRK = my_sbrk(SBRK_SIZE);
			// Again if there isn't enough memory to give, we return null with an out of memory error
			if (newSBRK == NULL) {
				ERRNO = OUT_OF_MEMORY;
				return NULL;
			} else {
				// If sbrk succeeds, we make new metadata for it, set the canaries, and return the pointer to the user space
				metadata_t md = {realSize, size, NULL, NULL};
				if (realSize + METASIZE + 1 < SBRK_SIZE) {
					//If there's room to break the block, we make another metadata 
					// for the other section and put it on the end of the freelist
					metadata_t postAlloc = {SBRK_SIZE - realSize, 0, NULL, prev};
					*((metadata_t*)(((char*) newSBRK) + METASIZE + size)) = postAlloc;
					if (prev != NULL) {
						prev->next = ((metadata_t*)(((char*) newSBRK) + METASIZE + size));
					}
				} else {
					//Otherwise we give the user the whole thing
					md.block_size = SBRK_SIZE;
				}

				//Setting metadata and canaries
				*((metadata_t*)newSBRK) = md;
				*((int*)(((char*) newSBRK) + sizeof(metadata_t))) = CANARY;
				*((int*)(((char*) newSBRK) + sizeof(metadata_t) + sizeof(int) + size)) = CANARY;

				// Return the user's pointer with no error
				ERRNO = NO_ERROR;
				return ((void*)(((char*) newSBRK) + METASIZE - sizeof(int)));
			}
		} else {
			//Otherwise we have a chunk of memory that they can use
			if (realSize + METASIZE + 1 < min->block_size) {
				// If it is big enough to split, we break off a chunk with new metadata and put it in the free list
				metadata_t postAlloc = {min->block_size - realSize, 0, min->next, min->prev};
				*((metadata_t*)(((char*) min) + METASIZE + size)) = postAlloc;
				if (min->prev != NULL) {
					min->prev->next = ((metadata_t*)(((char*) min) + METASIZE + size));
				}
				if(min->next != NULL) {
					min->next->prev = ((metadata_t*)(((char*) min) + METASIZE + size));
				}
				// If the memory was at the front of the list, we make this new half the front
				if (freelist == min) {
					freelist = ((metadata_t*)(((char*) min) + METASIZE + size));
				}
				min->block_size = realSize;


			} else {
				// If we can't split it, we need to take it out of the free list
				if (min->prev != NULL) {
					min->prev->next = NULL;
				}
			}
			// Get rid of min's pointers since it is no longer in the freelist. If min was the head, we move the head pointer.
			if (freelist == min) {
				freelist = min->next;
			}

			// Set the canaries and return the pointer to the user section.
			*((int*)(((char*) min) + sizeof(metadata_t))) = CANARY;
			*((int*)(((char*) min) + (sizeof(metadata_t) + sizeof(int) + size))) = CANARY;
			min->request_size = size;
			ERRNO = NO_ERROR;
			return ((void*)(((char*) min) + METASIZE - sizeof(int)));
		}
	}
  return NULL;
}

void my_free(void* ptr)
{
	// We take the pointer the user gave us and shift it back so we have the metadata
	metadata_t* mdptr = ((metadata_t*)(((char*) ptr) - sizeof(int) - sizeof(metadata_t)));
	// Check if the first canary is corrupted. If it is, return with corrupted error
	int* canary1 = ((int*)(((char*) mdptr) + sizeof(metadata_t)));
	if (*canary1 != CANARY) {
		ERRNO = CANARY_CORRUPTED;
		return;
	} else {
		// If it isn't, check if the second one is
		int* canary2 = ((int*)(((char*) mdptr) + mdptr->block_size - sizeof(int)));
		if (*canary2 != CANARY) {
			ERRNO = CANARY_CORRUPTED;
			return;
		}
	}

	// With nothing corrupted, we need to iterate through the freelist to see if we can merge
	// The newly freed memory with other free memory
	metadata_t* curr = freelist;
	metadata_t* prev = NULL;
	metadata_t* postMD = ((metadata_t*)(((char*) mdptr) + mdptr->block_size));
	int merged = 0;
	while (curr != NULL) {
		metadata_t* nextMD = ((metadata_t*)(((char*) curr) + curr->block_size));
		// Check if curr is right before the freed memory
		if (nextMD == mdptr) {
			// If it is, we need to handle an edge case where we already merged the memory with the head
			// Of the free list	
			if (nextMD == freelist) {
				freelist = curr;
				freelist->prev->next = NULL;
				freelist->prev = NULL;
			}
			// Then merge everything together
			merged = 1;
			curr->block_size += mdptr->block_size;
			mdptr->block_size = 0;
			mdptr->request_size = 0;
			mdptr->prev = NULL;
			mdptr->next = NULL;
			prev = curr;
			curr = curr->next;
		} else if (postMD == curr) {
			// Then check if curr is right after the freed memory
			metadata_t* next = curr->next;
			if (!merged) {
				// If it hasn't already merged with something before it, we merge the two
				merged = 1;
				mdptr->block_size += curr->block_size;
				mdptr->prev = curr->prev;
				if (curr->prev != NULL) {
					curr->prev->next = mdptr;
				}
				mdptr->next = curr->next;
				if (curr->next != NULL) {
					curr->next->prev = mdptr;
				}
				if (freelist == curr) {
					freelist = mdptr;
				}
				curr->block_size = 0;
				mdptr->request_size = 0;
				curr->next = NULL;
				curr->prev = NULL;
			} else {
				// Otherwise we need to merge it with the section before the freed memory
				curr->prev->block_size += curr->block_size;
				curr->prev->next = curr->next;
				if (curr->next != NULL) {
					curr->next->prev = curr->prev;
				}
				
				curr->block_size = 0;
				curr->next = NULL;
				curr->prev = NULL;
			}
			if (next != NULL) {
				prev = next->prev;
			}
			curr = next;
		} else {
			// If not, keep iterating
			prev = curr;
			curr = curr->next;
		}
	}

	// If we never did any merges, we need to just free up the memory and add it to the freelist	
	if(!merged) {
		mdptr->request_size = 0;
		mdptr->next = NULL;
		if (prev != NULL) {
			prev->next = mdptr;
			mdptr->prev = prev;
		} else {
			freelist = mdptr;
		}
	} 
}


