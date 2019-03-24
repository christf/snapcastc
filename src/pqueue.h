#pragma once

#include <sys/types.h>

/**
* Debugging macro .
*
* Checks for a NULL pointer, and prints the error message, source file and
* line via 'stderr' .
* If the check fails the program exits with error code (-1) .
#define NP_CHECK(ptr)                                                                          \
	{                                                                                      \
		if (NULL == (ptr)) {                                                           \
			fprintf(stderr, "%s:%d NULL POINTER: %s n", __FILE__, __LINE__, #ptr); \
			exit(-1);                                                              \
		}                                                                              \
	}
*/

/**
* Priority Queue Structure
*/
typedef struct PQueue_s {
	/* The actual size of heap at a certain time */
	size_t size;
	/* The amount of allocated memory for the heap */
	size_t capacity;
	/* An array of (void*), the actual max-heap */
	void **data;
	/* A pointer to a comparator function, used to prioritize elements */
	int (*cmp)(const void *d1, const void *d2);
} PQueue;

/** Allocates memory for a new Priority Queue .
Needs a pointer to a comparator function, thus establishing priorities .
*/
PQueue *pqueue_new(int (*cmp)(const void *d1, const void *d2), size_t capacity);

/** De-allocates memory for a given Priority Queue */
void pqueue_delete(PQueue *q);

/** Add an element inside the Priority Queue */
void pqueue_enqueue(PQueue *q, const void *data);

/** Removes the element with the greatest priority from within the Queue */
void *pqueue_dequeue(PQueue *q);

void *pqueue_peek(PQueue *q);
