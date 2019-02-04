/*
   Copyright (c) 2018, Christof Schulze <christof@christofschulze.com>
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
#include "inputpipe.h"
#include "snapcast.h"
#include "types.h"
#include "util.h"
#include "vector.h"
#include "version.h"
#include "opuscodec.h"

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

#define IPOVERHEAD_BYTES 40
#define UDPOVERHEAD_BYTES 8
#define PCMCHUNK_HEADER_SIZE 17

snapctx_t snapctx = {};

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
	write(STDERR_FILENO, SIGTERM_MSG, sizeof(SIGTERM_MSG));
	_exit(EXIT_SUCCESS);
}

void resume_read(void *d) {
	log_debug("resuming reading from pipe\n");
	int *efd = d;
	add_fd(*efd, snapctx.inputpipe_ctx.fd, EPOLLIN);
}

void loop() {
	int efd;
	int maxevents = 64;
	struct epoll_event *events;

	efd = epoll_create1(0);
	if (efd == -1) {
		perror("epoll_create");
		abort();
	}

	snapctx.efd = efd;
	add_fd(efd, snapctx.taskqueue_ctx.fd, EPOLLIN);
	add_fd(efd, snapctx.inputpipe_ctx.fd, EPOLLIN);
	add_fd(efd, snapctx.intercom_ctx.fd, EPOLLIN);

	events = snap_alloc0_array(maxevents, sizeof(struct epoll_event));
	log_verbose("starting loop\n");

	while (1) {
		int n = epoll_wait(efd, events, maxevents, -1);
		for (int i = 0; i < n; i++) {
			log_debug("handling event on fd %i.\n", events[i].data.fd);

			if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
				fprintf(stderr, "epoll error received on fd %i, continuing\n", events[i].data.fd);
				perror("epoll error without contingency plan. - continuing");
				// TODO: see which fd it was, and re-initialize the corresponding system. this will happen for example on inputpipe
				// when the pipe is closed by a writing application
				sig_term_handler(0, 0, 0);
			} else if ((snapctx.taskqueue_ctx.fd == events[i].data.fd) && (events[i].events & EPOLLIN)) {
				taskqueue_run(&snapctx.taskqueue_ctx);
			} else if ((snapctx.inputpipe_ctx.fd == events[i].data.fd) && (events[i].events & EPOLLIN)) {
				int ret = inputpipe_handle(&snapctx.inputpipe_ctx);
				if (ret == -1) {
					del_fd(efd, snapctx.inputpipe_ctx.fd);
					log_debug("throttling input from fifo\n");
					post_task(&snapctx.taskqueue_ctx, 0, snapctx.readms, resume_read, NULL, &efd);
				} else if (ret == 1) {
					// TODO: only encode when there is at least one client to save CPU cycles
					encode_opus_handle(&snapctx.inputpipe_ctx.chunk);
					intercom_send_audio(&snapctx.intercom_ctx, &snapctx.inputpipe_ctx.chunk);
					print_packet(snapctx.inputpipe_ctx.chunk.data, snapctx.inputpipe_ctx.chunk.size);
				}
			} else if ((snapctx.intercom_ctx.fd == events[i].data.fd) && (events[i].events & EPOLLIN)) {
				intercom_handle_in(&snapctx.intercom_ctx, events[i].data.fd);
			} else {
				char buffer[512];
				int tmp = read(events[i].data.fd, buffer, 512);
				exit_error("  WE JUST READ %i Byte from unknown socket %i or with unknown event with content %s\n", tmp,
					  events[i].data.fd, buffer);
			}
		}
	}

	free(events);
}

void usage() {
	// TODO: write complete help message
	puts("snapcast-server -b bufferMs -s <inputpipe>");
}

void catch_sigterm() {
	static struct sigaction _sigact;

	memset(&_sigact, 0, sizeof(_sigact));
	_sigact.sa_sigaction = sig_term_handler;
	_sigact.sa_flags = SA_SIGINFO;

	sigaction(SIGTERM, &_sigact, NULL);
}

int main(int argc, char *argv[]) {
	snapctx.verbose = false;
	snapctx.debug = false;

	snapctx.intercom_ctx.mtu = 1500;
	snapctx.intercom_ctx.port = INTERCOM_PORT;
	snapctx.intercom_ctx.serverport = INTERCOM_PORT;

	snapctx.samples = 48000;  // set default
	snapctx.frame_size = 2;   // set default
	snapctx.channels = 2;     // set default
	snapctx.readms = 5;       // set default
	snapctx.opuscodec_ctx.bitrate = 96000;       // set default

	int option_index = 0;
	struct option long_options[] = {{"help", 0, NULL, 'h'}, {"version", 0, NULL, 'V'}};
	int c;
	while ((c = getopt_long(argc, argv, "Vvdh:B:s:b:p:f:", long_options, &option_index)) != -1) switch (c) {
			case 'V':
				printf("snapclient %s\n", SOURCE_VERSION);
#if defined(GIT_BRANCH) && defined(GIT_COMMIT_HASH)
				printf("branch: %s\n commit: %s\n", GIT_BRANCH, GIT_COMMIT_HASH);
#endif
				exit(EXIT_SUCCESS);
			case 'd':
				snapctx.debug = true;
			/* Falls through. */
			case 'v':
				snapctx.verbose = true;
				break;
			//			case 'f':
			//				snapctx.samples
			//				snapctx.channels
			//				snapctx.frame_size
			case 'p':
				snapctx.intercom_ctx.port = atoi(optarg);
				break;
			case 'B':
				snapctx.readms = atoi(optarg);
				break;
			case 'b':
				snapctx.bufferms = atoi(optarg);
				break;
			case 's':
				snapctx.inputpipe_ctx.fname = strdupa(optarg);
				break;
			case 'h':
			default:
				usage();
				exit(EXIT_SUCCESS);
				break;
		}

	catch_sigterm();
	taskqueue_init(&snapctx.taskqueue_ctx);
	if (snapctx.inputpipe_ctx.fname)
		inputpipe_init(&snapctx.inputpipe_ctx);

	opus_init_encoder(snapctx.intercom_ctx.mtu - IPOVERHEAD_BYTES - sizeof(intercom_packet_hdr) - PCMCHUNK_HEADER_SIZE - UDPOVERHEAD_BYTES);
	clientmgr_init(&snapctx.clientmgr_ctx);

	intercom_init(&snapctx.intercom_ctx);
	loop();

	return 0;
}
