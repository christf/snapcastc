#include <stdlib.h>

#include "alloc.h"
#include "pqueue.h"
#include "util.h"

/* Util macros */
#define LEFT(x) (2 * (x) + 1)
#define RIGHT(x) (2 * (x) + 2)
#define PARENT(x) ((x) / 2)

void pqueue_heapify(PQueue *q, size_t idx);

/**
* Allocates memory for a new Priority Queue structure .

* 'cmp' function:
*   returns 0 if d1 and d2 have the same priorities
*   returns [negative value] if d1 have a smaller priority than d2
*   returns [positive value] if d1 have a greater priority than d2
*/
PQueue *pqueue_new(int (*cmp)(const void *d1, const void *d2), size_t capacity) {
	PQueue *res = NULL;
	if (!cmp) {
		exit_error("pqueue: comparator must not be NULL\n");
	}
	res = snap_alloc(sizeof(*res));
	res->cmp = cmp;
	/* The inner representation of data inside the queue is an array of void* */
	res->data = snap_alloc(capacity * sizeof(*(res->data)));
	res->size = 0;
	res->capacity = capacity;
	return (res);
}

/**
* De-allocates memory for a given Priority Queue structure .
*/
void pqueue_delete(PQueue *q) {
	if (NULL == q) {
		log_debug("Priority Queue is already NULL. Nothing to free.\n");
		return;
	}
	log_debug("pqueue delete\n");
	free(q->data);
	q->data = NULL;
	free(q);
	q = NULL;
}

/**
* Adds a new element to the Priority Queue .
*/
void pqueue_enqueue(PQueue *q, const void *data) {
	size_t i;
	void *tmp = NULL;
	// NP_CHECK(q);
	if (q->size >= q->capacity) {
		log_debug("Priority Queue is full. Cannot add another element.\n");
		return;
	}
	/* Adds element last */
	q->data[q->size] = (void *)data;
	i = q->size;
	q->size++;
	/* The new element is swapped with its parent as long as its
	precedence is higher */
	while (i > 0 && q->cmp(q->data[i], q->data[PARENT(i)]) > 0) {
		tmp = q->data[i];
		q->data[i] = q->data[PARENT(i)];
		q->data[PARENT(i)] = tmp;
		i = PARENT(i);
	}
}

void *pqueue_peek(PQueue *q) {
	if (!q || q->size < 1) {
		return NULL;
	}
	log_debug("peek\n");
	return q->data[0];
}
/**
* Returns the element with the biggest priority from the queue .
*/
void *pqueue_dequeue(PQueue *q) {
	void *data = NULL;
	data = pqueue_peek(q);
	log_debug("dequeue\n");
	if (data) {
		q->data[0] = q->data[q->size - 1];
		q->size--;
		/* Restore heap property */
		pqueue_heapify(q, 0);
	}
	return (data);
}

/**
* Turn an "almost-heap" into a heap .
*/
void pqueue_heapify(PQueue *q, size_t idx) {
	/* left index, right index, largest */
	void *tmp = NULL;
	size_t l_idx, r_idx, lrg_idx;
	// NP_CHECK(q);

	l_idx = LEFT(idx);
	r_idx = RIGHT(idx);

	/* Left child exists, compare left child with its parent */
	if (l_idx < q->size && q->cmp(q->data[l_idx], q->data[idx]) > 0) {
		lrg_idx = l_idx;
	} else {
		lrg_idx = idx;
	}

	/* Right child exists, compare right child with the largest element */
	if (r_idx < q->size && q->cmp(q->data[r_idx], q->data[lrg_idx]) > 0) {
		lrg_idx = r_idx;
	}

	/* At this point largest element was determined */
	if (lrg_idx != idx) {
		/* Swap between the index at the largest element */
		tmp = q->data[lrg_idx];
		q->data[lrg_idx] = q->data[idx];
		q->data[idx] = tmp;
		/* Heapify again */
		pqueue_heapify(q, lrg_idx);
	}
}
