#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#include "hashmap.h"
#include "buffer.h"

const size_t MIN_READ = 64;
const size_t MIN_WRITE = 64;

struct client {
	int sock;
	struct buffer input;
	struct buffer output;
};

struct server {
	int epoll;
	int listener;
	// clients by file descrptior, maps int to struct client
	struct hashmap clients;
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
	hashmap_init(&s->clients, &hashmap_ptr_equals, &hashmap_ptr_hash, NULL, &free);
	return 0;
}

void server_finalize(struct server *s)
{
	close(s->listener);
	hashmap_finalize(&s->clients);
}

int server_accept(struct server *s)
{
	struct client *client = malloc(sizeof(*client));
	struct epoll_event event;
	if (!client) {
		return -1;
	}
	int sock = accept(s->listener, NULL, NULL);
	if (sock < 0) {
		return -1;
	}
	client->sock = sock;
	buffer_init(&client->input);
	buffer_init(&client->output);
	if (hashmap_insert(&s->clients, (void *)(intptr_t)sock, (void *)client) < 0) {
		close(sock);
		return -1;
	}
	event.events = EPOLLIN;
	event.data.fd = sock;
	if (epoll_ctl(s->epoll, EPOLL_CTL_ADD, sock, &event) < 0) {
		hashmap_remove(&s->clients, (void *)(intptr_t)sock);
		close(sock);
		return -1;
	}
	printf("accepted client %d\n", sock);
	return 0;
}

int server_disconnect(struct server *s, struct client *cli)
{
	int sock = cli->sock;
	close(sock);
	buffer_finalize(&cli->input);
	buffer_finalize(&cli->output);
	hashmap_remove(&s->clients, (void *)(intptr_t)sock);
	printf("cli %d disconnected\n", sock);
	return 0;
}

int epoll_toggle_write(int epoll, int sock, int on)
{
	struct epoll_event event;
	if (on) {
		event.events = EPOLLIN | EPOLLOUT;
	} else {
		event.events = EPOLLIN;
	}
	event.data.fd = sock;
	return epoll_ctl(epoll, EPOLL_CTL_MOD, sock, &event);
}

int server_read(struct server *s, struct client *cli)
{
	int n;
	char *pos;;
	if (buffer_reserve(&cli->input, MIN_READ) < 0) {
		return -1;
	}
	n = read(cli->sock, buffer_back(&cli->input), buffer_rem(&cli->input));
	if (n < 0) {
		return -1;
	}
	if (n == 0) {
		return server_disconnect(s, cli);
	}
	buffer_push(&cli->input, n);
	for (;;) {
		pos = memchr(buffer_front(&cli->input),
				'\n',
				buffer_len(&cli->input));
		if (!pos) {
			break;
		}
		n = pos - buffer_front(&cli->input) + 1;
		// TODO do something with the request, for now we just echo it back
		printf("read ");
		fwrite(buffer_front(&cli->input), 1, n, stdout);
		if (buffer_reserve(&cli->output, n) < 0) {
			return -1;
		}
		if (buffer_len(&cli->output) == 0) {
			if (epoll_toggle_write(s->epoll, cli->sock, 1) < 0) {
				return -1;
			}
		}
		memcpy(buffer_back(&cli->output), buffer_front(&cli->input), n);
		buffer_push(&cli->output, n);
		buffer_pop(&cli->input, n);
	}
	return 0;
}

int server_write(struct server *s, struct client *cli)
{
	int n;
	n = write(cli->sock, buffer_front(&cli->output), buffer_len(&cli->output));
	if (n < 0 && errno == EPIPE) {
		errno = 0;
		return server_disconnect(s, cli);
	}
	if (n < 0) {
		return -1;
	}
	buffer_pop(&cli->output, n);
	if (buffer_len(&cli->output) == 0) {
		if (epoll_toggle_write(s->epoll, cli->sock, 0) < 0) {
			return -1;
		}
	}
	return 0;
}

const int MAX_EVENTS = 32;

int with_client(struct server *s, int sock, int (*fn)(struct server *, struct client *))
{
	struct client *cli;
	if (hashmap_get(&s->clients, (void *)(intptr_t)sock, (void **)&cli) < 0) {
		return 0;
	}
	return fn(s, cli);
}

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
				continue;
			}
			if (events[i].events & EPOLLIN) {
				if (with_client(s, events[i].data.fd, server_read) < 0) {
					return -1;
				}
			}
			if (events[i].events & EPOLLOUT) {
				if (with_client(s, events[i].data.fd, server_write) < 0) {
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
