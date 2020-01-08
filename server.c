#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#include "server.h"

struct server {
	int epoll;
	int listener;
};

int make_listener(int port)
{
	int sock;
	struct sockaddr_in addr;
	int enable = 1;
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		return -1;
	}
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
		goto error;
	}
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		goto error;
	}
	if (listen(sock, 5) < 0) {
		goto error;
	}
	return sock;
error:
	close(sock);
	return -1;
}

int server_init(struct server *s, int port)
{
	struct epoll_event event;
	s->epoll = epoll_create1(0);
	if (s->epoll < 0) {
		return -1;
	}
	s->listener = make_listener(port);
	if (s->listener < 0) {
		return -1;
	}
	event.events = EPOLLIN;
	event.data.fd = s->listener;
	if (epoll_ctl(s->epoll, EPOLL_CTL_ADD, s->listener, &event) < 0) {
		close(s->listener);
		return -1;
	}
	return 0;
}

void server_finalize(struct server *s)
{
	close(s->listener);
}

int server_accept(struct server *s)
{
	struct sockaddr_in addr;
	size_t addr_len = sizeof(addr);
	int sock = accept(s->listener, (struct sockaddr *)&addr, (socklen_t *)&addr_len);
	if (sock < 0) {
		return -1;
	}
	printf("new connection\n");
	close(sock);
	return 0;
}

const int MAX_EVENTS = 32;

int server_run(struct server *s)
{
	struct epoll_event events[MAX_EVENTS];
	int nfds;
	int i;
	while (1) {
		nfds = epoll_wait(s->epoll, events, MAX_EVENTS, -1);
		if (nfds < 0) {
			return -1;
		}
		for (i = 0; i < nfds; ++i) {
			if (events[i].data.fd == s->listener) {
				if (server_accept(s) < 0) {
					return -1;
				}
			}
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct server srv;
	if (server_init(&srv, 8080) < 0) {
		perror("failed to initialize server");
		return 1;
	}
	if (server_run(&srv) < 0) {
		perror("server error");
		server_finalize(&srv);
		return 1;
	}
	server_finalize(&srv);
	return 0;
}
