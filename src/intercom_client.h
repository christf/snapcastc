#pragma once 

#include "intercom_common.h"

struct intercom_task {
	uint16_t packet_len;
	uint8_t *packet;
	struct in6_addr *recipient;
	uint8_t retries_left;
	intercom_ctx *ctx;
};

#define SNAPCASTC_CLIENT_MODE 1

bool intercom_hello(intercom_ctx *ctx, const struct in6_addr *recipient, const int port);

int tlv_get_length(uint8_t *packet);

void intercom_reinit(void *ctx);
void intercom_init(intercom_ctx *ctx);
void intercom_uninit(intercom_ctx *ctx);
struct timespec intercom_get_time_next_audiochunk(intercom_ctx *ctx);
bool intercom_getnextaudiochunk(intercom_ctx *ctx, pcmChunk *c);
bool intercom_peeknextaudiochunk(intercom_ctx *ctx, pcmChunk **ret);

