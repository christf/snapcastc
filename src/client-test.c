/*
 *   Copyright (c) 2019, Christof Schulze <christof@christofschulze.com>
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *   ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "alloc.h"
#include "error.h"
#include "intercom_client.h"
#include "snapcast.h"
#include "socket.h"
#include "stream.h"
#include "types.h"
#include "util.h"
#include "vector.h"

#define SIGTERM_MSG "Exiting.\n"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

snapctx_t snapctx = {};

int tests_run = 0;

#define FAIL()                                                                      \
	{                                                                           \
		fprintf(stderr, "\nfailure in %s() line %d\n", __func__, __LINE__); \
		return 1;                                                           \
	}

#define _assert(test)           \
	do {                    \
		if (!(test))    \
			FAIL(); \
	} while (0)

#define _verify(test)                                                        \
	do {                                                                 \
		int r = test();                                              \
		tests_run++;                                                 \
		if (r) {                                                     \
			fprintf(stderr, "Test case %d failed\n", tests_run); \
			return r;                                            \
		}                                                            \
	} while (0)

int pqueue() {
	pcmChunk c1, c2, c3, c4, c5 = {};
	pcmChunk *t;
	PQueue *receivebuffer = pqueue_new(receivebuffer_cmp, 40);

	c1.play_at_tv_sec = 8;
	c2.play_at_tv_sec = 2;
	c3.play_at_tv_sec = 3;
	c4.play_at_tv_sec = 3;
	c5.play_at_tv_sec = 1;
	pqueue_enqueue(receivebuffer, &c1);
	pqueue_enqueue(receivebuffer, &c2);
	pqueue_enqueue(receivebuffer, &c3);
	pqueue_enqueue(receivebuffer, &c4);
	pqueue_enqueue(receivebuffer, &c5);

	t = pqueue_dequeue(receivebuffer);
	printf("chunk: %s\n", print_chunk(t));
	printf("pqueue elements: %zu, capacity %lu\n", receivebuffer->size, receivebuffer->capacity);

	t = pqueue_dequeue(receivebuffer);
	printf("chunk: %s\n", print_chunk(t));
	printf("pqueue elements: %zu, capacity %lu\n", receivebuffer->size, receivebuffer->capacity);

	t = pqueue_dequeue(receivebuffer);
	printf("chunk: %s\n", print_chunk(t));
	printf("pqueue elements: %zu, capacity %lu\n", receivebuffer->size, receivebuffer->capacity);

	t = pqueue_dequeue(receivebuffer);
	printf("chunk: %s\n", print_chunk(t));
	printf("pqueue elements: %zu, capacity %lu\n", receivebuffer->size, receivebuffer->capacity);

	t = pqueue_dequeue(receivebuffer);
	printf("chunk: %s\n", print_chunk(t));

	printf("pqueue elements: %zu, capacity %lu\n", receivebuffer->size, receivebuffer->capacity);
	pqueue_delete(receivebuffer);

	return 0;
}

int all_tests() {
	_verify(pqueue);
	return 0;
}

int main(int argc, char **argv) {
	snapctx.verbose = true;
	snapctx.debug = true;
	int result = all_tests();
	if (result == 0)
		printf("PASSED\n");
	else
		printf("FAILED\n");
	printf("Tests run: %d\n", tests_run);

	return result != 0;
}
