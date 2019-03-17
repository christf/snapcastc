/*
   Copyright (c) 2019, Christof Schulze <christof@christofschulze.com>
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
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
   */

#include "alloc.h"
#include "error.h"
#include "intercom.h"
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

int test_vector_init() {
	VECTOR(int) v;
	// initializing vector with bogus values
	v.desc.length = 5;
	v.desc.allocated = 12;

	VECTOR_INIT(v);
	_assert(v.desc.length == 0);
	_assert(v.desc.allocated == 0);

	VECTOR_ADD(v, 12);
	_assert(v.desc.length == 1);

	return 0;
}

int pcmchunk_header() {
	log_error("CHUNK_HEADER_SIZE: %d\n", CHUNK_HEADER_SIZE);
	_assert (17 == CHUNK_HEADER_SIZE);
	return 0;
}

int parse_streaminfo() {
	char optarg[1024];
	stream s = {};
	snprintf(optarg, 1024, "pipe:///tmp/snapfifo?buffer_ms=20&codec=opus&name=default&sampleformat=48000:16:2&timeout_ms=1000");

	if (!stream_parse(&s, optarg))
		FAIL();

	if (s.protocol != PIPE)
		FAIL();
	if (strcmp(s.inputpipe.fname, "/tmp/snapfifo"))
		FAIL();
	if (s.inputpipe.pipelength_ms != 1000)
		FAIL();
	if (strcmp(s.name, "default"))
		FAIL();
	if (s.inputpipe.read_ms != 20)
		FAIL();
	if (s.codec != OPUS)
		FAIL();
	if (s.inputpipe.samples != 48000)
		FAIL();
	if (s.inputpipe.samplesize != 2)
		FAIL();
	if (s.inputpipe.channels != 2)
		FAIL();

	return 0;
}

int all_tests() {
	_verify(test_vector_init);
	_verify(parse_streaminfo);
	_verify(pcmchunk_header);
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
