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
#include "opuscodec.h"
#include "snapcast.h"
#include "socket.h"
#include "stream.h"
#include "types.h"
#include "util.h"
#include "vector.h"
#include "version.h"

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

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
	write(STDERR_FILENO, SIGTERM_MSG, sizeof(SIGTERM_MSG));
	_exit(EXIT_SUCCESS);
}

bool is_inputpipe(int fd) { return !!stream_find_fd(fd); }

void loop() {
	int maxevents = 64;
	struct epoll_event *events;

	snapctx.efd = epoll_create1(0);
	if (snapctx.efd == -1) {
		perror("epoll_create");
		abort();
	}

	add_fd(snapctx.efd, snapctx.taskqueue_ctx.fd, EPOLLIN);
	add_fd(snapctx.efd, snapctx.intercom_ctx.fd, EPOLLIN);
	add_fd(snapctx.efd, snapctx.socket_ctx.fd, EPOLLIN);

	events = snap_alloc0_array(maxevents, sizeof(struct epoll_event));
	log_verbose("starting loop\n");

	while (1) {
		int n = epoll_wait(snapctx.efd, events, maxevents, -1);
		for (int i = 0; i < n; i++) {
			log_debug("handling event on fd %i.\n", events[i].data.fd);

			if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
				char strbuf[512];
				snprintf(strbuf, 512, "epoll error received on fd %i", events[i].data.fd);
				perror(strbuf);

				if (is_inputpipe(events[i].data.fd)) {
					log_error("input pipe was closed (mpd stopped?). Re-initializing-.\n");
					perror("received signal was");
					stream *s = stream_find_fd(events[i].data.fd);
					if (s) {
						if (s->inputpipe.state == PLAYING)
							del_fd(snapctx.efd, events[i].data.fd);
						inputpipe_uninit(&s->inputpipe);
						clientmgr_stop_clients(s);
						inputpipe_init(&s->inputpipe);
						add_fd(snapctx.efd, s->inputpipe.fd, EPOLLIN);
					}
				} else if (socket_get_client(&snapctx.socket_ctx, NULL, events[i].data.fd)) {
					log_error("error received on one of the socket clients. Closing %d\n", events[i].data.fd);
					socketclient *sc = NULL;
					socket_get_client(&snapctx.socket_ctx, &sc, events[i].data.fd);
					del_fd(snapctx.efd, sc->fd);
					socket_client_remove(&snapctx.socket_ctx, sc);
				} else
					sig_term_handler(0, 0, 0);
			} else if ((snapctx.taskqueue_ctx.fd == events[i].data.fd) && (events[i].events & EPOLLIN)) {
				taskqueue_run(&snapctx.taskqueue_ctx);
			} else if (is_inputpipe(events[i].data.fd) && (events[i].events & EPOLLIN)) {
				stream *s = stream_find_fd(events[i].data.fd);
				int ret = inputpipe_handle(&s->inputpipe);
				if (ret == -1) {
					del_fd(snapctx.efd, events[i].data.fd);
					s->inputpipe.state = THROTTLE;
					log_debug("throttling input from fifo, resuming read in %d ms\n", s->inputpipe.read_ms);
					s->inputpipe.resume_task = post_task(&snapctx.taskqueue_ctx, s->inputpipe.read_ms / 1000, s->inputpipe.read_ms % 1000,
						  inputpipe_resume_read, NULL, s);
				} else if (ret == 1) {
					encode_opus_handle(&s->opuscodec_ctx, &s->inputpipe.chunk);
					intercom_send_audio(&snapctx.intercom_ctx, s);
					print_packet(s->inputpipe.chunk.data, s->inputpipe.chunk.size);
				}
			} else if ((snapctx.intercom_ctx.fd == events[i].data.fd) && (events[i].events & EPOLLIN)) {
				intercom_handle_in(&snapctx.intercom_ctx, events[i].data.fd);
			} else if ((snapctx.socket_ctx.fd == events[i].data.fd) && (events[i].events & EPOLLIN)) {
				int cfd = socket_handle_in(&snapctx.socket_ctx);
				if (cfd > 0) {
					add_fd(snapctx.efd, cfd, EPOLLIN);
				}
			} else if (socket_get_client(&snapctx.socket_ctx, NULL, events[i].data.fd) && (events[i].events & EPOLLIN)) {
				socketclient *sc = NULL;
				if (socket_get_client(&snapctx.socket_ctx, &sc, events[i].data.fd)) {
					if (socket_handle_client(&snapctx.socket_ctx, sc) < 0) {
						log_error("closing client: %d\n", sc->fd);
						del_fd(snapctx.efd, sc->fd);
						socket_client_remove(&snapctx.socket_ctx, sc);
					}
				} else {
					log_error("socketclient with fd %d not found\n", events[i].data.fd);
				}
			} else {
				if (events[i].events == EPOLLIN) {
					char buffer[512];
					int tmp = read(events[i].data.fd, buffer, 512);
					if (tmp < 0)
						perror("read:");
					else if (tmp == 0) {  // EOF. Close silently
						log_error("just read EOF on fd %d\n", events[i].data.fd);
					} else {
						log_error("  WE JUST READ %i Byte from unknown socket %i or with unknown event with content %s\n",
							  tmp, events[i].data.fd, buffer);
					}
					break;
				}
				log_error("event is not EPOLLIN but still from an unknown fd. - This likely is a bug.\n");
			}
			log_debug("handled event on fd %d - Event %d out of %d\n", events[i].data.fd, i + 1, n);
			events[i].events = 0;  // clear events for next iteration
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
	bool input = false;
	snapctx.verbose = false;
	snapctx.debug = false;
	snapctx.operating_mode = SERVER;

	snapctx.intercom_ctx.mtu = 1500;
	snapctx.intercom_ctx.port = INTERCOM_PORT;

	snapctx.socketport = 1705;
	VECTOR_INIT(snapctx.streams);

	int option_index = 0;
	struct option long_options[] = {{"help", 0, NULL, 'h'}, {"version", 0, NULL, 'V'}};
	int c;
	while ((c = getopt_long(argc, argv, "Vvdh:s:b:p:f:P:", long_options, &option_index)) != -1) switch (c) {
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
			case 'P':
				snapctx.socketport = atoi(optarg);
				break;
			case 'p':
				snapctx.intercom_ctx.port = atoi(optarg);
				break;
			case 'b':
				snapctx.bufferms = atoi(optarg);
				break;
			case 's':
				input = true;
				stream s = {};
				if (!stream_parse(&s, optarg))
					exit_error("could not parse stream options %s\n", optarg);
				s.inputpipe.initialized = false;
				VECTOR_ADD(snapctx.streams, s);
				break;
			case 'h':
			default:
				usage();
				exit(EXIT_SUCCESS);
				break;
		}

	catch_sigterm();
	taskqueue_init(&snapctx.taskqueue_ctx);

	if (!input)
		exit_error("-s must be specified at least once\n");

	intercom_init(&snapctx.intercom_ctx);

	snapctx.socket_ctx.port = snapctx.socketport;
	socket_init(&snapctx.socket_ctx);

	loop();

	return 0;
}
