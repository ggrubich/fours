#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "client_common.h"

const char *USAGE = "client HOST[:PORT] NAME";

const int DEFAULT_PORT = 8051;

int parse_args(int argc, char **argv, struct sockaddr_in *addr, char **name)
{
	char *host, *colon, *portptr, *endptr;
	int port;
	if (argc != 3) {
		return RES_INVALID_ARGS;
	}
	host = strdup(argv[1]);
	if (!host) {
		return RES_ERR;
	}
	colon = strchr(host, ':');
	if (colon) {
		host[colon - host] = '\0';
		portptr = colon + 1;
		port = strtol(portptr, &endptr, 10);
		if (*portptr == '\0' || *endptr != '\0') {
			free(host);
			return RES_INVALID_ARGS;
		}
	} else {
		port = DEFAULT_PORT;
	}
	addr->sin_family = AF_INET;
	addr->sin_port = htons(port);
	if (inet_pton(AF_INET, host, &addr->sin_addr) == 0) {
		free(host);
		return RES_INVALID_ARGS;
	}
	*name = strdup(argv[2]);
	if (!*name) {
		free(host);
		return RES_ERR;
	}
	free(host);
	return RES_OK;
}

int main(int argc, char **argv)
{
	struct sockaddr_in addr;
	char *name;
	struct client c;
	int res;
	if ((res = parse_args(argc, argv, &addr, &name)) < 0) {
		if (res == RES_INVALID_ARGS) {
			fprintf(stderr, "invalid arguments\n");
		} else {
			perror("error parsing args");
		}
		return 1;
	}
	if ((res = client_init(&c, addr, name)) < 0) {
		perror("failed to initialize the client");
		free(name);
		return 1;
	}
	res = client_run(&c);
	client_finalize(&c);
	switch (res) {
	case RES_ERR:
		perror("client error");
		return 1;
	case RES_QUIT:
		return 0;
	case RES_DISCONNECT:
		fprintf(stderr, "disconnected by the server\n");
		return 1;
	case RES_INVALID_MSG:
		fprintf(stderr, "server sent an invalid message\n");
		return 1;
	}
	return 0;
}
