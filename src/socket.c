/*
   Copyright (c) 2017, Christof Schulze <christof@christofschulze.com>
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

#include "socket.h"
#include "alloc.h"
#include "clientmgr.h"
#include "error.h"
#include "intercom_srv.h"
#include "jsonrpc.h"
#include "snapcast.h"
#include "stream.h"
#include "util.h"

#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <json-c/json.h>

#include "version.h"

#define CONTROLPROTOCOLVERSION 1

void json_object_print_and_put(int fd, json_object *obj) {
	log_verbose("%s\n", json_object_to_json_string_ext(obj, 0));
	dprintf(fd, "%s", json_object_to_json_string_ext(obj, 0));
	json_object_put(obj);
}

void json_build_host(json_object *host, const char *arch, const char *ip, const char *mac, const char *name, const char *os) {
	json_object_object_add(host, "arch", json_object_new_string(arch));
	json_object_object_add(host, "ip", json_object_new_string(ip));
	json_object_object_add(host, "mac", json_object_new_string(mac));
	json_object_object_add(host, "name", json_object_new_string(name));
	json_object_object_add(host, "os", json_object_new_string(os));
}

void jsonrpc_buildresult(json_object *in, int id, json_object *result) {
	json_object_object_add(in, "id", json_object_new_int(id));
	json_object_object_add(in, "jsonrpc", json_object_new_string("2.0"));
	json_object_object_add(in, "result", result);
}

void client_status(json_object *in) {}

bool group_get_muted(stream *s) {
	// TODO implement me
	return false;
}

const char *group_get_id(stream *s) {
	// TODO implement me
	return "2427bc26-e219-8d33-901c-20493f46eb42";
}

const char *group_get_name(stream *s) { return s->name; }

const char *group_get_stream_id(stream *s) { return group_get_name(s); }

void json_add_time(json_object *out, struct timespec *t) {
	json_object_object_add(out, "sec", json_object_new_int(t->tv_sec));
	json_object_object_add(out, "usec", json_object_new_int(t->tv_nsec / 1000));
}

void json_add_group(json_object *out, stream *s) {
	json_object *clients = json_object_new_array();

	for (int i = VECTOR_LEN(s->clients) - 1; i >= 0; --i) {
		struct client *c = &VECTOR_INDEX(s->clients, i);

		json_object *client = json_object_new_object();
		json_object *config = json_object_new_object();
		json_object *volume = json_object_new_object();

		json_object_object_add(config, "instance", json_object_new_int(1));  // TODO: What is this value?
		json_object_object_add(config, "latency", json_object_new_int(c->latency));
		// json_object_object_add(config, "name", json_object_new_string(c->name));
		json_object_object_add(config, "name", json_object_new_string(""));
		json_object_object_add(config, "volume", volume);
		json_object_object_add(volume, "muted", json_object_new_boolean(c->muted));
		json_object_object_add(volume, "percent", json_object_new_int(c->volume_percent));

		json_object_object_add(client, "config", config);
		json_object_object_add(client, "connected", json_object_new_boolean(c->connected));

		json_object *host = json_object_new_object();
		json_build_host(host, "unknown", print_ip(&c->ip), print_mac(c->mac), c->name, "unknown");
		json_object_object_add(client, "host", host);

		json_object_object_add(client, "id", json_object_new_int(c->id));

		json_object *lastseen = json_object_new_object();
		json_add_time(lastseen, &c->lastseen);
		json_object_object_add(client, "lastSeen", lastseen);

		json_object *snapclient = json_object_new_object();
		json_object_object_add(snapclient, "name", json_object_new_string("Snapclient"));
		json_object_object_add(snapclient, "protocolVersion", json_object_new_int(c->protoversion));
		json_object_object_add(snapclient, "version", json_object_new_string("unknown"));

		json_object_object_add(client, "snapclient", snapclient);

		json_object_array_add(clients, client);
	}

	json_object_object_add(out, "clients", clients);
	json_object_object_add(out, "id", json_object_new_string(group_get_id(s)));
	json_object_object_add(out, "muted", json_object_new_boolean(group_get_muted(s)));
	json_object_object_add(out, "name", json_object_new_string(group_get_name(s)));
	json_object_object_add(out, "stream_id", json_object_new_string(group_get_stream_id(s)));
}

void json_add_groups(json_object *out) {
	json_object *groups = json_object_new_array();

	for (int i = 0; i < VECTOR_LEN(snapctx.streams); ++i) {
		stream *s = &VECTOR_INDEX(snapctx.streams, i);
		json_object *group = json_object_new_object();
		json_add_group(group, s);
		json_object_array_add(groups, group);
	}

	json_object_object_add(out, "groups", groups);
}

void json_build_serverstatus_streams(json_object *in) {
	// "raw": "pipe:////tmp/snapfifo?buffer_ms=20&codec=pcm&name=default&sampleformat=48000:16:2",
	json_object *streams = json_object_new_array();

	for (int i = VECTOR_LEN(snapctx.streams) - 1; i >= 0; --i) {
		stream *s = &VECTOR_INDEX(snapctx.streams, i);

		json_object *stream = json_object_new_object();
		json_object *meta = json_object_new_object();
		json_object *uri = json_object_new_object();
		json_object *query = json_object_new_object();
		json_object_object_add(stream, "id", json_object_new_string(s->name));
		json_object_object_add(meta, "STREAM", json_object_new_string(s->name));
		json_object_object_add(stream, "meta", meta);
		json_object_object_add(stream, "status", json_object_new_string(print_inputpipe_status(s->inputpipe.state)));

		json_object_object_add(stream, "uri", uri);
		json_object_object_add(uri, "fragment", json_object_new_string(""));
		json_object_object_add(uri, "host", json_object_new_string(""));
		json_object_object_add(uri, "path", json_object_new_string(s->inputpipe.fname));

		json_object_object_add(query, "buffer_ms", json_object_new_int(s->inputpipe.read_ms));
		json_object_object_add(query, "codec", json_object_new_string(print_codec(s->codec)));
		json_object_object_add(query, "name", json_object_new_string(s->name));  // the object contains s->name a lot
		char streamformat[20] = {};
		snprintf(streamformat, 20, "%zd:%d:%d", s->inputpipe.samples, s->inputpipe.samplesize * 8, s->inputpipe.channels);
		json_object_object_add(query, "sampleformat", json_object_new_string(streamformat));
		json_object_object_add(uri, "query", query);

		json_object_object_add(uri, "raw", json_object_new_string(s->raw));
		json_object_object_add(uri, "scheme", json_object_new_string(print_stream_protocol(s->protocol)));

		json_object_array_add(streams, stream);

		json_object_object_add(in, "streams", streams);  // streams - this is at the very least poorly named
	}
}

void json_build_serverstatus_server(json_object *in) {
	// TODO: There is too much data in the API that serves no visible purpose. Get rid of it.
	json_object *server = json_object_new_object();
	json_object *host = json_object_new_object();

	char hostname[NI_MAXHOST + 1];
	gethostname(hostname, NI_MAXHOST);
	json_build_host(host, "unknown_arch", "", "", hostname, "Linux");

	json_object *snapserver = json_object_new_object();
	json_object_object_add(snapserver, "controlProtocolVersion", json_object_new_int(CONTROLPROTOCOLVERSION));
	json_object_object_add(snapserver, "name", json_object_new_string("Snapserver"));
	json_object_object_add(snapserver, "protocolVersion", json_object_new_int(PACKET_FORMAT_VERSION));
	// json_object_object_add(snapserver, "version", json_object_new_string(SOURCE_VERSION));
	json_object_object_add(snapserver, "version", json_object_new_string(SOURCE_VERSION));

	json_object_object_add(server, "host", host);
	json_object_object_add(server, "snapserver", snapserver);
	json_object_object_add(in, "server", server);
}

void json_build_serverstatus(json_object *in) {
	json_object *response = json_object_new_object();
	json_add_groups(response);
	json_build_serverstatus_server(response);
	json_build_serverstatus_streams(response);
	json_object_object_add(in, "server", response);
}

void handle_server_getstatus(jsonrpc_request *request, int fd) {
	json_object *response = json_object_new_object();
	json_object *result = json_object_new_object();
	json_build_serverstatus(result);
	jsonrpc_buildresult(response, request->id, result);

	json_object_print_and_put(fd, response);
}

void handle_GetRPCVersion(jsonrpc_request *request, int fd) {
	json_object *response = json_object_new_object();
	json_object *result = json_object_new_object();

	json_object_object_add(result, "major", json_object_new_string(SOURCE_VERSION_MAJOR));
	json_object_object_add(result, "minor", json_object_new_string(SOURCE_VERSION_MINOR));
	json_object_object_add(result, "patch", json_object_new_string(SOURCE_VERSION_PATCH));
	jsonrpc_buildresult(response, request->id, result);

	json_object_print_and_put(fd, response);
}

void socket_init(socket_ctx *ctx) {
	log_verbose("Initializing socket: %d\n", ctx->port);

	struct sockaddr_in6 server_addr = {
	    .sin6_family = AF_INET6, .sin6_port = htons(ctx->port),
	};

	ctx->fd = socket(PF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (ctx->fd < 0)
		exit_errno("creating API socket on node-IP");

	if (setsockopt(ctx->fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
		exit_error("setsockopt(SO_REUSEADDR) failed");

	if (bind(ctx->fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		exit_errno("bind socket to node-IP failed");
	}

	if (listen(ctx->fd, 5) < 0)
		exit_errno("Could not listen on status socket");
}

bool handle_client_setstream(jsonrpc_request *request, int fd) {
	json_object *response = json_object_new_object();
	json_object *result = json_object_new_object();
	int clientid = 0;
	char *target_stream_id = NULL;
	client_t *client = NULL;
	stream *newstream = NULL;

	for (int i = VECTOR_LEN(request->parameters) - 1; i >= 0; --i) {
		parameter *p = &VECTOR_INDEX(request->parameters, i);
		if (!strncmp(p->name, "id", 2)) {
			clientid = p->value.number;
			client = find_client(clientid).client;
			if (!client) {
				log_error("received stream change request for unknown client %d\n", clientid);
				break;
			}
		} else if (!strncmp(p->name, "stream_id", 9)) {
			newstream = stream_find_name(p->value.string);
			if (newstream) {
				free(target_stream_id);
				target_stream_id = snap_alloc(strlen(p->value.string));
				strncpy(target_stream_id, p->value.string, strlen(p->value.string));
			} else {
				free(target_stream_id);
				log_error("received stream change request for unknown stream %s\n", p->value.string);
				break;
			}
		}
	}

	if (client) {
		stream *current_stream = stream_find(client);
		bool client_stream_is_changed = !!strncmp(current_stream->name, target_stream_id, strlen(current_stream->name));
		if (client_stream_is_changed) {
			intercom_stop_client(&snapctx.intercom_ctx, client);
			stream_client_remove(current_stream, client);
			stream_client_add(newstream, client);
		}
		json_object_object_add(response, "result", json_object_new_string(target_stream_id));
	}
	jsonrpc_buildresult(response, request->id, result);
	json_object_print_and_put(fd, response);
	free(target_stream_id);
	return true;
}

bool handle_client_setvolume(jsonrpc_request *request, int fd) {
	json_object *response = json_object_new_object();
	json_object *result = json_object_new_object();

	int clientid = 0;
	client_t *client = NULL;
	json_object *jobj = NULL;
	json_object *mute = NULL;
	json_object *vol_percent = NULL;
	bool bool_result = false;
	int volume_int;
	bool muted;

	for (int i = VECTOR_LEN(request->parameters) - 1; i >= 0; --i) {
		parameter *p = &VECTOR_INDEX(request->parameters, i);
		if (!strncmp(p->name, "id", 2)) {
			clientid = p->value.number;
			client = find_client(clientid).client;
			if (!client) {
				log_error("received volume change request for unknown client %d\n", clientid);
				break;
			}
		} else if (!strncmp(p->name, "volume", 6)) {
			jobj = json_tokener_parse(p->value.json_string);

			if (!jobj) {
				log_error("error parsing json %s\n", p->value.json_string);
				goto reply;
			}

			if (!json_object_object_get_ex(jobj, "percent", &vol_percent) && !json_object_object_get_ex(jobj, "muted", &mute)) {
				log_error("Either mute or volume are mandatory in a volume object. We received %s\n", p->value.json_string);
				json_object_put(jobj);
				goto reply;
			}

			if (vol_percent) {
				volume_int = json_object_get_int(vol_percent);
				json_object_put(vol_percent);
			}

			if (mute) {
				muted = json_object_get_boolean(mute);
				json_object_put(mute);
			}

			json_object_put(jobj);
		}
	}

	if (client) {
		if (vol_percent) {
			bool_result = clientmgr_client_refreshvolume(client, volume_int);
			client->volume_percent = volume_int;
		}
		if (mute) {
			bool_result = clientmgr_client_setmute(client, muted);
			client->muted = muted;
		}
	}

reply:;
	json_object *volume = json_object_new_object();
	if (client) {
		json_object_object_add(volume, "muted", json_object_new_boolean(client->muted));
		json_object_object_add(volume, "percent", json_object_new_int(client->volume_percent));
	}
	json_object_object_add(result, "volume", volume);
	jsonrpc_buildresult(response, request->id, result);
	json_object_print_and_put(fd, response);

	return bool_result;
}

int handle_request(jsonrpc_request *request, int fd) {
	if (!strncmp(request->method, "Server.GetRPCVersion", 20)) {
		handle_GetRPCVersion(request, fd);
	} else if (!strncmp(request->method, "Client.SetStream", 16)) {
		log_debug("calling server Client.SetStream\n");
		handle_client_setstream(request, fd);
	} else if (!strncmp(request->method, "Client.SetVolume", 16)) {
		log_debug("calling server Client.SetVolume\n");
		handle_client_setvolume(request, fd);
	} else if (!strncmp(request->method, "Server.GetStatus", 16)) {
		log_debug("calling server getstatus\n");
		handle_server_getstatus(request, fd);
	}
	return 1;
}

void socket_client_remove(socket_ctx *ctx, socketclient *sc) {
	close(sc->fd);
	VECTOR_DELETE(ctx->clients, VECTOR_GETINDEX(ctx->clients, sc));
}

int handle_line(socketclient *sc) {
	if (!strnlen(sc->line, LINEBUFFER_SIZE))
		return false;

	jsonrpc_request jreq = {};
	VECTOR_INIT(jreq.parameters);
	log_error("parsing line: %s\n", sc->line);
	if (!(jsonrpc_parse_string(&jreq, sc->line))) {
		log_error("parsing unsuccessful for %s\n", sc->line);
		return false;
	}

	log_debug("parsing successful\n");

	int ret = handle_request(&jreq, sc->fd);

	jsonrpc_free_members(&jreq);
	return ret;
}

int socket_handle_client(socket_ctx *ctx, socketclient *sc) {
	int len = 0;

	log_debug("handling client\n");

	if (sc->line_offset < LINEBUFFER_SIZE) {
		len = read(sc->fd, &(sc->line[sc->line_offset]), LINEBUFFER_SIZE - sc->line_offset - 1);
		log_error("read: %s length: %d offset %d\n", sc->line, len, sc->line_offset);
		if (len > 0) {
			for (int i = sc->line_offset; i < LINEBUFFER_SIZE - 1; ++i) {
				if (sc->line[i] == '\n' || sc->line[i] == '\r') {
					sc->line[sc->line_offset + len] = '\0';
					log_debug("read full line: %s", sc->line);
					if (handle_line(sc) < 0) {
						// clients sending invalid data are getting their connection closed.
						return -1;
					};

					sc->line[0] = '\0';
					sc->line_offset = 0;
					return 0;
				}
			}

			sc->line_offset += len;
			return 0;
		} else if (len == 0) {
			log_debug("received EOF from client %d, closing socket\n", sc->fd);
			return -1;
		} else {
			log_error("jo\n");
			perror("Error when reading:");
			if (errno == EAGAIN) {
				log_debug("No more data, try again\n");
				return 0;
			}
			return -1;
		}
	} else {
		log_error("linebuffer for client exhausted. Closing.\n");
		return -1;
	}
}

bool socket_get_client(socket_ctx *ctx, socketclient **sc_dest, int fd) {
	for (int i = VECTOR_LEN(ctx->clients) - 1; i >= 0; --i) {
		socketclient *sc = &VECTOR_INDEX(ctx->clients, i);
		if (fd == sc->fd) {
			if (sc_dest)
				*sc_dest = sc;

			return true;
		}
	}
	return false;
}

int socket_handle_in(socket_ctx *ctx) {
	log_error("handling socket event\n");

	int fd = accept(ctx->fd, NULL, NULL);
	int flags = fcntl(fd, F_GETFL, 0);
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
		exit_errno("could not set socket to non-blocking\n");
	}

	socketclient sc = {.fd = fd, .line_offset = 0, .line[0] = '\0'};
	memset(sc.line, 0, LINEBUFFER_SIZE);
	VECTOR_ADD(ctx->clients, sc);

	// TODO: send all relevant notifications to this client.

	return fd;
}
