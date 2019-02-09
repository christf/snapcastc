#pragma once

#include "clientmgr.h"
#include "pcmchunk.h"
#include "taskqueue.h"
#include "vector.h"

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PACKET_FORMAT_VERSION 1
#define CLIENTNAME_MAXLEN

enum { CLIENT_OPERATION, AUDIO_DATA, SERVER_OPERATION };  // Packet types
enum { REQUEST, HELLO };				  // TLV types client op
enum { AUDIO,
       AUDIO_PCM,
       AUDIO_OPUS,
       AUDIO_OGG,
       AUDIO_FLAC };   // TLV type for AUDIO packets, AUDIO will be obsoleted by the more specific types once implemented.
enum { STREAM_INFO, CLIENT_STOP };  // TLV - types for server op

typedef struct __attribute__((__packed__)) {
	uint8_t version;
	uint8_t type;
	uint16_t empty;
	uint32_t nonce;
} intercom_packet_hdr;

typedef struct __attribute__((__packed__)) {
	uint8_t type;
	uint8_t length;
} tlv_op;

typedef struct __attribute__((__packed__)) {
	uint8_t type;
	uint8_t length;
	uint32_t nonce;
} tlv_request;

typedef struct __attribute__((__packed__)) {
	uint8_t type;
	uint8_t length;
	uint32_t node_id;
	uint8_t volume;
} tlv_hello;

typedef struct __attribute__((__packed__)) {
	intercom_packet_hdr hdr;
	// after this a dynamic buffer is appended to hold TLV
} intercom_packet_hello;

typedef struct __attribute__((__packed__)) {
	intercom_packet_hdr hdr;
	// after this a dynamic buffer is appended to hold TLV.
} intercom_packet_op;

typedef struct __attribute__((__packed__)) {
	intercom_packet_hdr hdr;
	// after this a dynamic buffer is appended to hold TLV.
} intercom_packet_sop;

typedef struct __attribute__((__packed__)) {
	intercom_packet_hdr hdr;
	uint16_t bufferms;
	// after this a dynamic buffer is appended to hold TLV.
} intercom_packet_audio;

typedef VECTOR(client_t) client_v;

struct intercom_task {
	uint16_t packet_len;
	uint8_t *packet;
	struct in6_addr *recipient;
	taskqueue_t *check_task;
	uint8_t retries_left;
};

typedef struct {
	uint8_t *data;
	uint16_t len;
	uint32_t nonce;
} audio_packet;

struct buffer_cleanup_task {
	audio_packet ap;
};

typedef struct {
	struct in6_addr serverip;
	struct snaptx *snapctx;
	VECTOR(intercom_packet_hdr) recent_packets;
	VECTOR(audio_packet) missing_packets;
	VECTOR(audio_packet) packet_buffer;
	int fd;
	uint16_t port;
	int16_t controlport;
	uint16_t serverport;
	uint32_t nodeid;
	int mtu;
	int buffer_wraparound;

	size_t bufferwindex;
	size_t buffer_elements;
	size_t bufferrindex;
	size_t lastreceviedseqno;
	pcmChunk *buffer;

} intercom_ctx;

void intercom_send_audio(intercom_ctx *ctx, pcmChunk *chunk);
void intercom_recently_seen_add(intercom_ctx *ctx, intercom_packet_hdr *hdr);
bool intercom_send_packet_unicast(intercom_ctx *ctx, const struct in6_addr *recipient, uint8_t *packet, ssize_t packet_len, int port);
void intercom_seek(intercom_ctx *ctx, const struct in6_addr *address);
void intercom_init_unicast(intercom_ctx *ctx);
void intercom_init(intercom_ctx *ctx);
void intercom_handle_in(intercom_ctx *ctx, int fd);
bool intercom_hello(intercom_ctx *ctx, const struct in6_addr *recipient, int port);
bool intercom_stop_client(intercom_ctx *ctx, const struct in6_addr *recipient, int port);

struct timespec intercom_get_time_next_audiochunk(intercom_ctx *ctx);

bool intercom_peeknextaudiochunk(intercom_ctx *ctx, pcmChunk **ret);
void intercom_getnextaudiochunk(intercom_ctx *ctx, pcmChunk *c);
