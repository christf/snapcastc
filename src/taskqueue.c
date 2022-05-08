/*
  Copyright (c) 2012-2016, Matthias Schiffer <mschiffer@universe-factory.net>
  Copyright (c) 2016, Nils Schneider <nils@nilsschneider.net>
  Copyright (c) 2017-2018, Christof Schulze <christof@christofschulze.com>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "alloc.h"
#include "error.h"
#include "snapcast.h"
#include "taskqueue.h"
#include "timespec.h"
#include "util.h"

void taskqueue_init(taskqueue_ctx *ctx) {
	ctx->fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	ctx->queue = NULL;
}

/** this will add timeout seconds and millisecs milliseconds to the current time
 * to calculate at which time a task should run given an offset */
struct timespec settime(time_t timeout, long millisecs) {
	struct timespec due;
	clock_gettime(CLOCK_MONOTONIC, &due);

	struct timespec t = {.tv_sec = timeout, .tv_nsec = millisecs * 1000000l};

	return timeAdd(&due, &t);
}

/** Enqueues a new task. A task with a timeout of zero is scheduled immediately.
 */
taskqueue_t *post_task(taskqueue_ctx *ctx, time_t timeout, long millisecs, void (*function)(void *),
		       void (*cleanup)(void *), void *data) {
	taskqueue_t *task = snap_alloc(sizeof(taskqueue_t));
	task->children = task->next = NULL;
	task->pprev = NULL;

	task->due = settime(timeout, millisecs);
	task->running = false;

	task->function = function;
	task->cleanup = cleanup;
	task->data = data;
	taskqueue_insert(&ctx->queue, task);
	taskqueue_schedule(ctx);

	return task;
}

void drop_task(taskqueue_ctx *ctx, taskqueue_t *task) {
	if (task && task->running) {
		return; // nothing to do, will be cleaned up and freed after the task finished running
	}

	taskqueue_remove(task);

	if (task->cleanup != NULL)
		task->cleanup(task->data);

	free(task);
	task = NULL;
	taskqueue_schedule(ctx);
}

/** Changes the timeout of a task.
  */
bool reschedule_task(taskqueue_ctx *ctx, taskqueue_t *task, time_t timeout, long millisecs) {
	if (task == NULL || !taskqueue_linked(task))
		return false;

	struct timespec due = settime(timeout, millisecs);

	if (timespec_cmp(&due, &task->due)) {
		task->due = due;
		taskqueue_remove(task);
		taskqueue_insert(&ctx->queue, task);
		taskqueue_schedule(ctx);
	}

	return true;
}

void taskqueue_schedule(taskqueue_ctx *ctx) {
	if (ctx->queue == NULL) {
		log_debug("Taskqueue is empty, not scheduling another task\n");
		return;
	}

	struct itimerspec t = {.it_value = ctx->queue->due};
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	log_debug("It is now: %s, scheduling next task for %s\n", print_timespec(&now), print_timespec(&ctx->queue->due));
	timerfd_settime(ctx->fd, TFD_TIMER_ABSTIME, &t, NULL);
}

void taskqueue_run(taskqueue_ctx *ctx) {
	unsigned long long nEvents;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	size_t rsize = read(ctx->fd, &nEvents, sizeof(nEvents));
	if ( ! rsize)
		log_error("could not read from taskqueue fd\n");

	if (ctx->queue == NULL)
		return;

	while (ctx->queue && timespec_cmp(&(ctx->queue->due), &now) <= 0) {
		taskqueue_t *task = ctx->queue;
		log_debug("The time is now: %s, running task that was due at %s\n", print_timespec(&now), print_timespec(&task->due));
		taskqueue_remove(task);
		task->running = true;
		task->function(task->data);

		if (task->cleanup)
			task->cleanup(task->data);

		free(task);
	}

	taskqueue_schedule(ctx);
}

/** Links an element at the position specified by \e queue */
static inline void taskqueue_link(taskqueue_t **queue, taskqueue_t *elem) {
	if (elem->next)
		exit_bug("taskqueue_link: element already linked");

	elem->pprev = queue;
	elem->next = *queue;
	if (elem->next)
		elem->next->pprev = &elem->next;

	*queue = elem;
}

/** Unlinks an element */
static inline void taskqueue_unlink(taskqueue_t *elem) {
	*elem->pprev = elem->next;
	if (elem->next)
		elem->next->pprev = elem->pprev;

	elem->next = NULL;
}

/**
   Merges two priority queues

   \e queue2 may be empty (NULL)
*/
static taskqueue_t *taskqueue_merge(taskqueue_t *queue1, taskqueue_t *queue2) {
	if (!queue1)
		exit_bug("taskqueue_merge: queue1 unset");
	if (queue1->next)
		exit_bug("taskqueue_merge: queue2 has successor");
	if (!queue2)
		return queue1;
	if (queue2->next)
		exit_bug("taskqueue_merge: queue2 has successor");

	taskqueue_t *lo, *hi;

	if (timespec_cmp(&queue1->due, &queue2->due) < 0) {
		lo = queue1;
		hi = queue2;
	} else {
		lo = queue2;
		hi = queue1;
	}

	taskqueue_link(&lo->children, hi);

	return lo;
}

/** Merges a list of priority queues */
static taskqueue_t *taskqueue_merge_pairs(taskqueue_t *queue0) {
	if (!queue0)
		return NULL;

	if (!queue0->pprev)
		exit_bug("taskqueue_merge_pairs: unlinked queue");

	taskqueue_t *queue1 = queue0->next;

	if (!queue1)
		return queue0;

	taskqueue_t *queue2 = queue1->next;

	queue0->next = queue1->next = NULL;

	return taskqueue_merge(taskqueue_merge(queue0, queue1), taskqueue_merge_pairs(queue2));
}

/** Inserts a new element into a priority queue */
void taskqueue_insert(taskqueue_t **queue, taskqueue_t *elem) {
	if (elem->pprev || elem->next || elem->children)
		exit_bug("taskqueue_insert: tried to insert linked queue element");

	*queue = taskqueue_merge(elem, *queue);
	(*queue)->pprev = queue;
}

/** Removes an element from a priority queue */
void taskqueue_remove(taskqueue_t *elem) {
	if (!taskqueue_linked(elem)) {
		if (elem->children || elem->next)
			exit_bug("taskqueue_remove: corrupted queue item");

		return;
	}

	taskqueue_t **pprev = elem->pprev;

	taskqueue_unlink(elem);

	taskqueue_t *merged = taskqueue_merge_pairs(elem->children);
	if (merged)
		taskqueue_link(pprev, merged);

	elem->pprev = NULL;
	elem->children = NULL;
}
