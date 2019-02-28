/*  Snapcastc - synchronous audio playback
 * Copyright (C) 2019 - Christof Schulze <christof@christofschulze.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "alsaplayer.h"
#include "clientmgr.h"
#include "inputpipe.h"
#include "intercom.h"
#include "opuscodec.h"
#include "socket.h"
#include "stream.h"
#include "taskqueue.h"
#include "types.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

enum operatingmode { CLIENT, SERVER };

struct snapctx {
	char *servername;  // snapcast server
	char *selfname;
	struct in6_addr ownip;

	taskqueue_ctx taskqueue_ctx;
	alsaplayer_ctx alsaplayer_ctx;
	clientmgr_ctx clientmgr_ctx;
	intercom_ctx intercom_ctx;
	opuscodec_ctx opuscodec_ctx;
	socket_ctx socket_ctx;

	VECTOR(stream) streams;

	int efd;
	uint16_t bufferms;

	int samples;
	int frame_size;
	int channels;

	size_t readms;

	uint16_t socketport;

	enum operatingmode operating_mode;

	bool debug;
	bool verbose;
};

extern snapctx_t snapctx;

void add_fd(int efd, int fd, uint32_t events);
void del_fd(int efd, int fd);
