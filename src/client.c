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
#include "snapcast.h"
#include "types.h"
#include "util.h"
#include "vector.h"
#include "version.h"

#define SIGTERM_MSG "Exiting.\n"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

snapctx_t snapctx = {};

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
	alsaplayer_uninit(&snapctx.alsaplayer_ctx);
	write(STDERR_FILENO, SIGTERM_MSG, sizeof(SIGTERM_MSG));
	_exit(EXIT_SUCCESS);
}

void loop() {
	struct pollfd fds[snapctx.alsaplayer_ctx.pollfd_count + 2];  // allocate fds for alsa events

	int fd_index = 0;
	fds[fd_index].fd = snapctx.taskqueue_ctx.fd;
	fds[fd_index].events = POLLIN;
	fd_index++;
	fds[fd_index].fd = snapctx.intercom_ctx.fd;
	fds[fd_index].events = POLLIN;
	fd_index++;

	for (int i = 0; i < snapctx.alsaplayer_ctx.pollfd_count; i++) {
		struct pollfd *pfd = &snapctx.alsaplayer_ctx.ufds[i];
		fds[i + fd_index].fd = pfd->fd;
		fds[i + fd_index].events = POLLIN;
	}

	fd_index += snapctx.alsaplayer_ctx.pollfd_count;

	log_verbose("starting loop\n");

	while (1) {
		int n = poll(fds, fd_index, -1);

		if (n == -1) {
			sig_term_handler(0, 0, 0);
			exit_error("poll error - Catch me if you can!\n");
		} else if (n == 0)
			log_error("this should never happen - timeout on poll");
		else {
			for (int i = 0; i < fd_index; i++) {
				log_debug("handling event on fd %i.\n", fds[i].fd);

				if (is_alsafd(fds[i].fd, &snapctx.alsaplayer_ctx)) {
					// this is a nightmare. alsa logic is meant to be contained in alsaplayer
					unsigned short revents;
					snd_pcm_poll_descriptors_revents(snapctx.alsaplayer_ctx.pcm_handle, fds, fd_index, &revents);
					fds[i].revents = revents;
				}

				if ((fds[i].revents & POLLIN) && (fds[i].fd == snapctx.taskqueue_ctx.fd)) {
					log_debug("taskqueue ready\n");
					taskqueue_run(&snapctx.taskqueue_ctx);
				} else if ((fds[i].revents & POLLIN) && (fds[i].fd == snapctx.intercom_ctx.fd)) {
					log_debug("intercom ready for IO\n");
					intercom_handle_in(&snapctx.intercom_ctx, fds[i].fd);
				} else if ((fds[i].revents & POLLOUT) && (is_alsafd(fds[i].fd, &snapctx.alsaplayer_ctx))) {
					log_debug("alsa device ready for IO\n");
					alsaplayer_handle(&snapctx.alsaplayer_ctx);
				}

				fds[i].revents = 0;  // clear events for next iteration
			}
		}
	}
}

void usage() { puts("snapclient -h <servername>"); }

void catch_sigterm() {
	static struct sigaction _sigact;

	memset(&_sigact, 0, sizeof(_sigact));
	_sigact.sa_sigaction = sig_term_handler;
	_sigact.sa_flags = SA_SIGINFO;

	sigaction(SIGTERM, &_sigact, NULL);
}

#include "pcmchunk.h"

int main(int argc, char *argv[]) {
	snapctx.verbose = false;
	snapctx.debug = false;

	// TODO: read these from stream URI
	snapctx.alsaplayer_ctx.rate = FREQUENCY;
	snapctx.alsaplayer_ctx.channels = CHANNELS;
	snapctx.alsaplayer_ctx.frame_size = SAMPLESIZE;
	snapctx.readms = READMS;

	snapctx.intercom_ctx.port = INTERCOM_PORT;
	snapctx.intercom_ctx.mtu = 1500;
	snapctx.intercom_ctx.serverport = INTERCOM_PORT;

	// TODO: set value from stream info
	snapctx.bufferms = 1000;

	int option_index = 0;
	struct option long_options[] = {{"help", 0, NULL, 'h'}, {"version", 0, NULL, 'V'}};
	int c;
	while ((c = getopt_long(argc, argv, "VvdhH:p:", long_options, &option_index)) != -1) {
		switch (c) {
			case 'V':
				printf("snapclient %s\n", SOURCE_VERSION);
#if defined(GIT_BRANCH) && defined(GIT_COMMIT_HASH)
				printf("branch: %s\n commit: %s\n", GIT_BRANCH, GIT_COMMIT_HASH);
#endif
				exit(EXIT_SUCCESS);
			case 'p':
				snapctx.intercom_ctx.port = atoi(optarg);
				break;
			case 'H':  // TODO: deduplicate with intercom_init() and move away from switch statement
				snapctx.servername = strdupa(optarg);
				struct addrinfo hints = {};
				struct addrinfo *result = NULL, *rp;
				int s, sfd;

				hints.ai_family = AF_INET6;
				hints.ai_socktype = SOCK_DGRAM;
				hints.ai_flags = AI_V4MAPPED;
				hints.ai_protocol = 0;

				s = getaddrinfo(snapctx.servername, NULL, &hints, &result);
				if (s != 0) {
					fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
					exit_error("could not get address for host %s\n", optarg);
				}

				for (rp = result; rp != NULL; rp = rp->ai_next) {
					sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
					if (sfd == -1)
						continue;

					if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
						struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)result->ai_addr;
						memcpy(&snapctx.intercom_ctx.serverip.s6_addr, &s6->sin6_addr, sizeof(struct in6_addr));
						close(sfd);
						break;
					}

					close(sfd);
				}
				if (rp == NULL) {
					fprintf(stderr, "Could not connect\n");
					exit_error("could not connect to host %s\n", optarg);
				}

				freeaddrinfo(result);

				break;
			case 'd':
				snapctx.debug = true;
			/* Falls through. */
			case 'v':
				snapctx.verbose = true;
				break;
			case 'h':
			default:
				usage();
				exit(EXIT_SUCCESS);
				break;
		}
	}

	catch_sigterm();
	taskqueue_init(&snapctx.taskqueue_ctx);

	alsaplayer_init(&snapctx.alsaplayer_ctx);
	intercom_init(&snapctx.intercom_ctx);
	intercom_hello(&snapctx.intercom_ctx, &snapctx.intercom_ctx.serverip, snapctx.intercom_ctx.serverport);

	loop();

	return 0;
}
