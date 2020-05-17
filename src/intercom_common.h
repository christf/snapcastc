#pragma once

#include "clientmgr.h"
#include "pcmchunk.h"
#include "taskqueue.h"
#include "vector.h"
#include "stream.h"
#include "packet_types.h"
#include "pqueue.h"

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PACKET_FORMAT_VERSION 1
#define CLIENTNAME_MAXLEN
#define INTERCOM_MAX_RECENT 1000
#define NONCE_MAX 0xFFFFFFFF

enum { CLIENT_OPERATION, AUDIO_DATA, SERVER_OPERATION };  // Packet types
	enum { REQUEST, HELLO };				  // TLV types client op
enum { AUDIO,
       AUDIO_PCM,
       AUDIO_OPUS,
       AUDIO_OGG,
       AUDIO_FLAC };				   // TLV type for AUDIO packets, AUDIO will be obsoleted by the more specific types once implemented.
enum { STREAM_INFO, CLIENT_STOP, CLIENT_VOLUME };  // TLV - types for server op

typedef struct __attribute__((__packed__)) {
	uint8_t version;
	uint8_t type;
	uint16_t clientid;
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
	uint16_t node_id;
	uint32_t latency;
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

typedef struct {
	struct in6_addr serverip;
	VECTOR(intercom_packet_hdr) recent_packets;
	VECTOR(audio_packet) missing_packets;
	int fd;
	uint16_t port;
	uint16_t controlport;
	uint16_t nodeid;
	int mtu;
	int buffer_wraparound;

	size_t lastreceviedseqno;
	PQueue *receivebuffer;
	taskqueue_t *hello_task;
} intercom_ctx;

bool intercom_send_packet_unicast(intercom_ctx *ctx, const struct in6_addr *recipient, uint8_t *packet, ssize_t packet_len, int port);
void intercom_handle_in(intercom_ctx *ctx, int fd);
void intercom_recently_seen_add(intercom_ctx *ctx, intercom_packet_hdr *hdr);
void intercom_handle_packet(intercom_ctx *ctx, uint8_t *packet, ssize_t packet_len, struct in6_addr *peer, uint16_t port);

int cmp_audiopacket(const audio_packet *ap1, const audio_packet *ap2);
bool intercom_recently_seen(intercom_ctx *ctx, intercom_packet_hdr *hdr);
int assemble_header(intercom_packet_hdr *hdr, uint8_t type, uint32_t *nonce, uint16_t clientid);
uint32_t get_nonce(uint32_t *nonce);

