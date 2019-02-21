#pragma once

#include "vector.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#define LINEBUFFER_SIZE 1024

enum socket_command { CLIENT_GETSTATUS, CLIENT_SET_VOLUME, CLIENT_SETLATENCY, CLIENT_SETNAME, SERVER_GETRPCVERSION, SERVER_GETSTATUS };
// GROUP_GETSTATUS, GROUP_GETMUTE, GROUP_SETSTREAM, GROUP_SETCLIENTS, SERVER_DELETECLIENT

// enum socket_notifications {}
// CLIENT_ONCONNECT, CLIENT_ONDISCONNECT, CLIENT_ONVOLUMECHANGED,
// CLIENT_ONLATENCYCHANGED, CLIENT_ONNAMECHANGED, GROUP_ONMUTE,
// GROUP_ONSTREAMCHANGED, STREAM_ONUPDATE, SERVER_ONUPDATE

typedef struct {
	int fd;
	char line[LINEBUFFER_SIZE];
	int line_offset;
} socketclient;

typedef struct {
	int fd;
	uint16_t port;
	VECTOR(socketclient) clients;
} socket_ctx;

void socket_init(socket_ctx *ctx);
int socket_handle_in(socket_ctx *ctx);
bool socket_get_client(socket_ctx *ctx, socketclient **sc_dest, int fd);
int socket_handle_client(socket_ctx *ctx, socketclient *sc);
void socket_client_remove(socket_ctx *ctx, socketclient *sc);
