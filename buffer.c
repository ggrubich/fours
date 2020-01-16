#include <string.h>
#include <stdlib.h>

#include "buffer.h"

static size_t min(size_t a, size_t b)
{
	return a < b ? a : b;
}

static size_t max(size_t a, size_t b)
{
	return a > b ? a : b;
}

void buffer_init(struct buffer *buf)
{
	buf->data = NULL;
	buf->cap = 0;
	buf->head = 0;
	buf->tail = 0;
}

void buffer_finalize(struct buffer *buf)
{
	free(buf->data);
}

size_t buffer_len(struct buffer *buf)
{
	if (buf->head <= buf->tail) {
		return buf->tail - buf->head;
	} else {
		return (buf->cap - buf->head) + buf->tail;
	}
}

char buffer_get(struct buffer *buf, size_t i)
{
	return buf->data[(buf->head + i) % buf->cap];
}

int buffer_reserve(struct buffer *buf, size_t size)
{
	char *tmp;
	size_t ncap;
	// we reserve size+1 instead of size to avoid a situation where
	// (tail + size) % cap == head
	// which would indicate that the buffer is empty.
	// one additional slot makes sure this will never happen.
	size = size + 1;
	if (buf->cap - buffer_len(buf) >= size) {
		return 0;
	}
	ncap = max(2 * buf->cap, buffer_len(buf) + size);
	tmp = realloc(buf->data, ncap);
	if (!tmp) {
		return -1;
	}
	if (buf->tail < buf->head) {
		// the new buffer is at least two times larger,
		// so this will always fit
		memcpy(tmp + buf->cap, tmp, buf->tail);
		buf->tail = buf->cap + buf->tail;
	}
	buf->data = tmp;
	buf->cap = ncap;
	return 0;
}

int buffer_push(struct buffer *buf, char *src, size_t len)
{
	size_t first, second;
	if (buffer_reserve(buf, len) < 0) {
		return -1;
	}
	first = min(len, buf->cap - buf->tail);
	second = len - first;
	memcpy(buf->data + buf->tail, src, first);
	memcpy(buf->data, src + first, second);
	buf->tail = (buf->tail + len) % buf->cap;
	return 0;
}

int buffer_peek(struct buffer *buf, char *dst, size_t len)
{
	size_t first, second;
	if (buffer_len(buf) < len) {
		return -1;
	}
	if (dst) {
		first = min(len, buf->cap - buf->head);
		second = len - first;
		memcpy(dst, buf->data + buf->head, first);
		memcpy(dst + first, buf->data, second);
	}
	return 0;
}

int buffer_pop(struct buffer *buf, char *dst, size_t len)
{
	if (buffer_peek(buf, dst, len) < 0) {
		return -1;
	}
	buf->head = (buf->head + len) % buf->cap;
	return 0;
}

int buffer_append(struct buffer *buf, struct buffer *src, size_t len)
{
	size_t first, second;
	if (buffer_len(src) < len || buffer_reserve(buf, len) < 0) {
		return -1;
	}
	first = min(len, src->cap - src->head);
	second = len - first;
	buffer_push(buf, src->data + src->head, first);
	buffer_push(buf, src->data, second);
	src->head = (src->head + len) % src->cap;
	return 0;
}
