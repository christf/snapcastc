#include "intercom_common.h"
#include "error.h"
#include "snapcast.h"
#include "syscallwrappers.h"
#include "util.h"

uint32_t nonce = 0;
uint32_t get_nonce(uint32_t *nonce) { return htonl(((*nonce)++ % NONCE_MAX)); }

int cmp_audiopacket(const audio_packet *ap1, const audio_packet *ap2) {
	if (ap1->nonce > ap2->nonce)
		return 1;
	else if (ap1->nonce < ap2->nonce)
		return -1;
	return 0;
}

int assemble_header(intercom_packet_hdr *hdr, uint8_t type, uint32_t *nonce, uint16_t clientid) {
	hdr->version = PACKET_FORMAT_VERSION;
	hdr->type = type;
	hdr->clientid = htons(clientid);
	hdr->nonce = get_nonce(nonce);
	return sizeof(intercom_packet_hdr);
}

bool intercom_send_packet_unicast(intercom_ctx *ctx, const struct in6_addr *recipient, uint8_t *packet, ssize_t packet_len, int port) {
	struct sockaddr_in6 addr = (struct sockaddr_in6){.sin6_family = AF_INET6, .sin6_port = htons(port), .sin6_addr = *recipient};

	int rc = sendto(ctx->fd, packet, packet_len, 0, (struct sockaddr *)&addr, sizeof(addr));
	log_debug("sent intercom packet rc: %zi to %s\n", rc, print_ip(recipient));

	if (rc < 0)
		perror("sendto failed");

	return rc >= 0;
}

bool intercom_recently_seen(intercom_ctx *ctx, intercom_packet_hdr *hdr) {
	for (size_t i = VECTOR_LEN(ctx->recent_packets) - 1 ; i < VECTOR_LEN(ctx->recent_packets); i++) {
		intercom_packet_hdr *ref_hdr = &VECTOR_INDEX(ctx->recent_packets, i);

		if (ref_hdr->nonce == hdr->nonce && ref_hdr->type == hdr->type)
			return true;
	}
	return false;
}

void intercom_recently_seen_add(intercom_ctx *ctx, intercom_packet_hdr *hdr) {
	// TODO: sender IP / port must be considered
	while (VECTOR_LEN(ctx->recent_packets) > INTERCOM_MAX_RECENT) VECTOR_DELETE(ctx->recent_packets, 0);

	VECTOR_ADD(ctx->recent_packets, *hdr);
}

void intercom_handle_in(intercom_ctx *ctx, int fd) {
	ssize_t count;
	uint8_t buf[ctx->mtu];

	log_debug("HANDLING INTERCOM PACKET on fd %i using buffersize of %i ", fd, ctx->mtu);

	while (1) {
		struct sockaddr_in6 peer_addr;

		unsigned int peer_addr_len = sizeof(struct sockaddr_storage);
		count = recvfrom(fd, buf, ctx->mtu, 0, (struct sockaddr *)&peer_addr, &peer_addr_len);

		if (count == -1) {
			// If errno == EAGAIN, that means we have read all data. So go back to the main loop.
			if (errno == EBADF) {
				log_error("EBADF received on fd: %i\n", fd);
				perror("There is something crazy going on. - returning to the main loop");
			} else if (errno != EAGAIN) {
				perror("read error - this should not happen - going back to main loop");
			}
			break;
		} else if (count == 0) {
			/* End of file. The remote has closed the connection. */
			break;
		}

		struct in6_addr peer;
		memcpy(&peer.s6_addr, &peer_addr.sin6_addr, sizeof(struct in6_addr));

		uint16_t port = ntohs(peer_addr.sin6_port);
		log_debug("- read from %s(%d), %zi Bytes of data: ", print_ip(&peer), port, count);
		print_packet(buf, count);

		intercom_handle_packet(ctx, buf, count, &peer, port);
	}
}

