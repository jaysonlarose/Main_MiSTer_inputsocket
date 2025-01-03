#define MAX_CONNECTIONS 10

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <strings.h>
#include <linux/input.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include "input.h"
#include "cfg.h"

struct __attribute__((__packed__)) input_socket_packet {
	uint8_t opcode;
	uint8_t index;
	uint8_t player_id;
	uint16_t vendor_id;
	uint16_t product_id;
	uint16_t type;
	uint16_t code;
	int32_t value;
	time_t tv_sec;
	suseconds_t tv_usec;
};

#define OP_INPUT 0
#define OP_PING  1
#define OP_PONG  2
bool initialized = false;

struct pollfd sockets[MAX_CONNECTIONS + 1];

void input_socket_init(void) {
	if (cfg.input_socket_enabled) {
		if (!initialized) {
			int port = cfg.input_socket_bindport;
			struct sockaddr_in addr;
			struct hostent *host;
			bzero(&addr, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			if (strlen(cfg.input_socket_bindhost) == 0) {
				addr.sin_addr.s_addr = INADDR_ANY;
			} else {
				host = gethostbyname(cfg.input_socket_bindhost);
				bcopy((char *) host->h_addr, (char *) &addr.sin_addr.s_addr, host->h_length);
			}
			int reuse = 1;
			int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
			if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &reuse, sizeof(reuse)) < 0) {
				printf("can't setsockopt\n");
				return;
			}
			if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
				printf("can't bind port\n");
				return;
			}
			if (listen(sock, 1) != 0) {
				printf("can't listen to port\n");
				return;
			}
			for (int i = 1; i < MAX_CONNECTIONS + 1; i++) {
				sockets[i].fd = -1;
			}
			sockets[0].fd = sock;
			sockets[0].events = POLLIN;
			initialized = true;
			printf("Listening on port %d\n", port);
			printf("  sizeof struct timeval: %d\n", sizeof(struct timeval));
			printf("  sizeof struct input_event: %d\n", sizeof(struct input_event));
			printf("  sizeof struct input_socket_packet: %d\n", sizeof(struct input_socket_packet));
		} else {
			printf("sockets already initialized, reinit unneccesary\n");
		}
	} else {
		printf("input socket disabled.\n");
	}
}

void input_socket_send(uint8_t inputno, uint8_t player_id, uint16_t vid, uint16_t pid, struct input_event *ev) {
	if (cfg.input_socket_enabled) {
		struct input_socket_packet packet;
		packet.opcode     = OP_INPUT;
		packet.index      = inputno;
		packet.player_id  = player_id;
		packet.vendor_id  = vid;
		packet.product_id = pid;
		packet.type       = ev->type;
		packet.code       = ev->code;
		packet.value      = ev->value;
		packet.tv_sec     = ev->time.tv_sec;
		packet.tv_usec    = ev->time.tv_usec;

		for (int i = 1; i < MAX_CONNECTIONS + 1; i++) {
			if (sockets[i].fd >= 0) {
				write(sockets[i].fd, (char *) &packet, sizeof(packet));
			}
		}
	}
}

int input_socket_poll(int timeout) {
	if (cfg.input_socket_enabled) {
		int return_value = poll(sockets, MAX_CONNECTIONS + 1, timeout);
		if (return_value < 0) {
			printf("input_socket_poll polling error!\n");
			return -1;
		} else if (return_value == 0) {
			return 0;
		}
		for (int i = 0; i < MAX_CONNECTIONS + 1; i++) {
			if (sockets[i].fd >= 0) {
				// close and un-slot the socket if it's in an error state
				if (sockets[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
					printf("closing socket %d\n", i);
					close(sockets[i].fd);
					sockets[i].fd = -1;
				// does the socket require servicing?
				} else if (sockets[i].revents & POLLIN) {
					// socket 0 is our listen socket; accept a new client.
					if (i == 0) {
						// accept connection
						int client = accept(sockets[i].fd, NULL, NULL);
						// look for an empty slot to put it in
						for (int j = 1; j < MAX_CONNECTIONS + 1; j++) {
							if (sockets[j].fd < 0) {
								printf("accepting socket %d\n", j);
								sockets[j].fd = client;
								sockets[j].events = POLLIN;
								sockets[j].revents = 0;
								client = -1;
								break;
							}
						}
						// no slot? un-accept the connection.
						if (client >= 0) {
							printf("rejecting connection\n");
							close(client);
						}
					// any other socket? I'm sure it has something interesting to say, but we just don't care
					} else {
						char trash[1];
						int rlen = read(sockets[i].fd, trash, 1);
						if (rlen == 0) {
							printf("closing socket %d\n", i);
							shutdown(sockets[i].fd, SHUT_RDWR);
							close(sockets[i].fd);
							sockets[i].fd = -1;
						} else {
							uint8_t opcode = trash[0];
							if (opcode == OP_PING) {
								trash[0] = OP_PONG;
								write(sockets[i].fd, (char *) trash, 1);
							} else {
								printf("Unknown opcode %d, dropping client\n", opcode);
								shutdown(sockets[i].fd, SHUT_RDWR);
								close(sockets[i].fd);
								sockets[i].fd = -1;
							}
						}
					}
				}
			}
		}
	}	
	return 0;
}

void input_socket_destroy(void) {
	if (cfg.input_socket_enabled) {
		printf("shutting down open sockets\n");
		for (int i = 0; i < MAX_CONNECTIONS + 1; i++) {
			if (sockets[i].fd >= 0) {
				close(sockets[i].fd);
			}
		}
	}
}
