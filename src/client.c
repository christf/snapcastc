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
#include "alsaplayer.h"
#include "error.h"
#include "pcmchunk.h"
#include "snapcast.h"
#include "syscallwrappers.h"
#include "types.h"
#include "util.h"
#include "vector.h"
#include "version.h"
#include "intercom_client.h"

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
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

snapctx_t snapctx = {};

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
	alsaplayer_remove_task(&snapctx.alsaplayer_ctx);
	alsaplayer_uninit(&snapctx.alsaplayer_ctx);
	write(STDERR_FILENO, SIGTERM_MSG, sizeof(SIGTERM_MSG));
	_exit(EXIT_SUCCESS);
}

int alsa_get_fd_amount() {
	int fd_amount;

	alsaplayer_ctx aplay_tmp_ctx = {
	    // just set some sane defaults such that we can determine how many FD alsa will need.
	    .frame_size = 2,
	    .channels = 2,
	    .rate = 48000,
	    .pcm.name = strdupa(snapctx.alsaplayer_ctx.pcm.name),
	    .card = strdupa(snapctx.alsaplayer_ctx.card),
	    .mixer = strdupa(snapctx.alsaplayer_ctx.mixer),
	};
	alsaplayer_init(&aplay_tmp_ctx);

	fd_amount = aplay_tmp_ctx.pollfd_count;

	alsaplayer_remove_task(&aplay_tmp_ctx);
	alsaplayer_uninit(&aplay_tmp_ctx);

	return fd_amount;
}

void resume_polling(void *d) {
	struct pollfd *fds = (struct pollfd*)d;
	fds->fd = snapctx.intercom_ctx.fd;
}

void loop() {
	int fd_index = 0;
	fd_index += alsa_get_fd_amount();
	struct pollfd fds[fd_index + 2];  // allocate fds for alsa events
	snapctx.alsaplayer_ctx.main_poll_fd = fds;  // alsa fds must be the first

	for (int i = 0; i < fd_index; i++) {
		fds[i].fd = -1;
		fds[i].events = POLLIN;
	}

	fds[fd_index].fd = snapctx.taskqueue_ctx.fd;
	fds[fd_index].events = POLLIN;
	fd_index++;
	fds[fd_index].fd = snapctx.intercom_ctx.fd;
	fds[fd_index].events = POLLIN;
	fd_index++;

	log_verbose("starting loop\n");

	while (1) {
		log_debug("polling\n");
		int n = poll(fds, fd_index, -1);

		if (n == -1) {
			sig_term_handler(0, 0, 0);
			exit_error("poll error - Catch me if you can!\n");
		} else if (n == 0)
			log_error("this should never happen - timeout on poll");
		else {
			for (int i = 0; i < fd_index; i++) {
				if (fds[i].revents) {
					log_debug("handling event on fd %i.\n", fds[i].fd);

					if (is_alsafd(fds[i].fd, &snapctx.alsaplayer_ctx)) {
						// this is a nightmare. alsa logic is meant to be contained in alsaplayer
						unsigned short revents;
						snd_pcm_poll_descriptors_revents(snapctx.alsaplayer_ctx.pcm_handle, fds,
										 snapctx.alsaplayer_ctx.pollfd_count, &revents);
						fds[i].revents = revents;
					}

					if ((fds[i].revents & POLLIN) && (fds[i].fd == snapctx.taskqueue_ctx.fd)) {
						log_debug("taskqueue ready\n");
						taskqueue_run(&snapctx.taskqueue_ctx);
					} else if ((fds[i].revents & POLLIN) && (fds[i].fd == snapctx.intercom_ctx.fd)) {
						log_debug("intercom ready for IO\n");
						intercom_handle_in(&snapctx.intercom_ctx, fds[i].fd);
					} else if (( fds[i].revents & POLLERR) && (fds[i].fd == snapctx.intercom_ctx.fd)) {
						log_error("Error on network layer, could not reach server. Re-initializing.\n");
						intercom_uninit(&snapctx.intercom_ctx);
						alsaplayer_uninit(&snapctx.alsaplayer_ctx);
						post_task(&snapctx.taskqueue_ctx, 1, 0, intercom_reinit, NULL, (void*)&snapctx.intercom_ctx);
					} else if (( fds[i].revents & POLLNVAL) && (fds[i].fd == snapctx.intercom_ctx.fd)) {
						fds[fd_index-1].fd = -1;
						post_task(&snapctx.taskqueue_ctx, 1, 0, resume_polling, NULL, &fds[fd_index-1]);
					} else if ((fds[i].revents & POLLOUT) && (is_alsafd(fds[i].fd, &snapctx.alsaplayer_ctx))) {
						log_debug("alsa device ready for IO\n");
						alsaplayer_handle(&snapctx.alsaplayer_ctx);
						snapctx.alsaplayer_ctx.ufds[i].revents = 0;  // clear events for next iteration
					}

					fds[i].revents = 0;  // clear events for next iteration
				}
			}
		}
	}
}

void usage() {
	puts("snapclient [-l | -H <servername> [-L <latency_ms>] [-p <serverport>] [-c <card>] [-m <mixer>] [-s <pcm name>] [-i <clientID>]  [-v] [-d] ] ");
	puts("");
	puts("-l		list pcm devices");
	puts("-L		set pcm latency");
	puts("-V		show version");
	puts("");
	puts("example: snapcast-client -H media -c hw:1 -m snapclient -s snapclient -i 2 -v");
}

void catch_sigterm() {
	static struct sigaction _sigact;

	memset(&_sigact, 0, sizeof(_sigact));
	_sigact.sa_sigaction = sig_term_handler;
	_sigact.sa_flags = SA_SIGINFO;

	sigaction(SIGTERM, &_sigact, NULL);
}

int obtain_ip_from_name(const char *hostname, struct in6_addr *addr) {
	struct addrinfo hints = {};
	struct addrinfo *result = NULL, *rp;
	int s, sfd;

	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_V4MAPPED;
	hints.ai_protocol = 0;

	//	char service[7];
	//	snprintf(service, 7, "%d", snapctx.intercom_ctx.port);

	s = getaddrinfo(hostname, NULL, &hints, &result);
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
			memcpy(&addr->s6_addr, &s6->sin6_addr, sizeof(struct in6_addr));
			close(sfd);
			break;
		}

		close(sfd);
	}
	if (rp == NULL) {
		exit_error("could not connect to host %s\n", optarg);
	}

	freeaddrinfo(result);
	return 0;
}

int main(int argc, char *argv[]) {
	snapctx.verbose = false;
	snapctx.debug = false;

	snapctx.alsaplayer_ctx.pollfd_count = 0;
	snapctx.alsaplayer_ctx.initialized = false;

	snapctx.bufferms = 1000;
	snapctx.alsaplayer_ctx.card = strdup("default");
	snapctx.alsaplayer_ctx.mixer = strdup("Master");
	snapctx.alsaplayer_ctx.pcm.name = strdup("default");

	snapctx.intercom_ctx.port = INTERCOM_PORT;
	snapctx.intercom_ctx.mtu = 1500;  // Do we need to expose this to the user via cli?
	obtainrandom(&snapctx.intercom_ctx.nodeid, sizeof(uint32_t), 0);

	int option_index = 0;
	struct option long_options[] = {{"help", 0, NULL, 'h'}, {"version", 0, NULL, 'V'}};
	int c;
	while ((c = getopt_long(argc, argv, "lVvdhH:p:s:i:L:c:m:", long_options, &option_index)) != -1) {
		switch (c) {
			case 'V':
				printf("snapclient %s\n", SOURCE_VERSION);
#if defined(GIT_BRANCH) && defined(GIT_COMMIT_HASH)
				printf("branch: %s\n commit: %s\n", GIT_BRANCH, GIT_COMMIT_HASH);
#endif
				exit(EXIT_SUCCESS);
			case 'c':
				free(snapctx.alsaplayer_ctx.card);
				snapctx.alsaplayer_ctx.card = strdup(optarg);
				break;
			case 'm':
				free(snapctx.alsaplayer_ctx.mixer);
				snapctx.alsaplayer_ctx.mixer = strdup(optarg);
				break;
			case 's':
				free(snapctx.alsaplayer_ctx.pcm.name);
				snapctx.alsaplayer_ctx.pcm.name = strdup(optarg);
				break;
			case 'l':
				alsaplayer_pcm_list();
				exit(EXIT_SUCCESS);
			case 'p':
				snapctx.intercom_ctx.port = atoi(optarg);
				break;
			case 'i':
				snapctx.intercom_ctx.nodeid = atol(optarg);
				break;
			case 'H':
				snapctx.servername = strdup(optarg);
				obtain_ip_from_name(optarg, &snapctx.intercom_ctx.serverip);
				break;
			case 'd':
				snapctx.debug = true;
			/* Falls through. */
			case 'v':
				snapctx.verbose = true;
				break;
			case 'L':
				snapctx.alsaplayer_ctx.latency_ms = atol(optarg);
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

	intercom_init(&snapctx.intercom_ctx);

	// we have realtime business when feeding the alsa buffer, setting prio may help on an otherwise busy client.
	if (setpriority(PRIO_PROCESS, 0, -5)) {
		log_error("could not set priority. Adjusting priority helps on otherwise busy clients. Continuing.\n");
	}

	loop();

	return 0;
}
