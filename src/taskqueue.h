#pragma once

#include <stdbool.h>
#include <time.h>

typedef struct taskqueue taskqueue_t;

typedef struct {
	struct snapctx *snapctx;
	taskqueue_t *queue;
	int fd;
} taskqueue_ctx;

/** Element of a priority queue */
struct taskqueue {
	taskqueue_t **pprev; /**< \e next element of the previous element (or \e
				children of the parent) */
	taskqueue_t *next;   /**< Next sibling in the heap */

	taskqueue_t *children; /**< Heap children */

	struct timespec due; /**< The priority */

	void (*function)(void *);
	void (*cleanup)(void *);
	void *data;
};

/** Checks if an element is currently part of a priority queue */
static inline bool taskqueue_linked(taskqueue_t *elem) { return elem->pprev; }

void taskqueue_insert(taskqueue_t **queue, taskqueue_t *elem);
void taskqueue_remove(taskqueue_t *elem);

void taskqueue_init(taskqueue_ctx *ctx);
void taskqueue_run(taskqueue_ctx *ctx);
void taskqueue_schedule(taskqueue_ctx *ctx);
taskqueue_t *post_task(taskqueue_ctx *ctx, time_t timeout, long millisecs, void (*function)(void *), void (*cleanup)(void *),
		       void *data);
bool reschedule_task(taskqueue_ctx *ctx, taskqueue_t *task, time_t timeout, long millisecs);
