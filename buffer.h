#pragma once

#include <stddef.h>

struct buffer {
	// private
	char *data;
	size_t cap;
	size_t off;
	size_t len;
};

void buffer_init(struct buffer *buf);

void buffer_finalize(struct buffer *buf);

int buffer_reserve(struct buffer *buf, size_t size);

char *buffer_front(struct buffer *buf);

char *buffer_back(struct buffer *buf);

void buffer_pop(struct buffer *buf, size_t n);

void buffer_push(struct buffer *buf, size_t n);

size_t buffer_len(struct buffer *buf);

size_t buffer_rem(struct buffer *buf);
