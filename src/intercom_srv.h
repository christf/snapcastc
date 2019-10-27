#pragma once

#include "intercom_common.h"

#define SNAPCASTC_SERVER_MODE 1

void intercom_init(intercom_ctx *ctx);
void intercom_send_audio(intercom_ctx *ctx, stream *s);
bool intercom_stop_client(intercom_ctx *ctx, const client_t *client);
bool intercom_set_volume(intercom_ctx *ctx, const client_t *client, uint8_t volume);

