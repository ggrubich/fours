#include <string.h>
#include <stdlib.h>

#include "buffer.h"

const size_t DEFAULT_CAP = 64;

void buffer_init(struct buffer *buf)
{
	buf->data = NULL;
	buf->cap = 0;
	buf->off = 0;
	buf->len = 0;
}

void buffer_finalize(struct buffer *buf)
{
	free(buf->data);
}

int buffer_reserve(struct buffer *buf, size_t size)
{
	char *tmp;
	int cap;
	if (buf->cap - buf->len < size) {
		cap = buf->cap;
		do {
			if (cap == 0) {
				cap = DEFAULT_CAP;
			} else {
				// overflow
				if (cap * 2 < cap) {
					return -1;
				}
				cap = cap * 2;
			}
		} while (cap - buf->len < size);
		tmp = malloc(cap);
		if (!tmp) {
			return -1;
		}
		memcpy(tmp, buf->data + buf->off, buf->len);
		free(buf->data);
		buf->data = tmp;
		buf->cap = cap;
		buf->off = 0;
	} else if (buf->cap - buf->off - buf->len < size) {
		memmove(buf->data, buf->data + buf->off, buf->len);
		buf->off = 0;
	}
	return 0;
}

char *buffer_front(struct buffer *buf)
{
	return buf->data + buf->off;
}

char *buffer_back(struct buffer *buf)
{
	return buffer_front(buf) + buf->len;
}

void buffer_pop(struct buffer *buf, size_t n)
{
	if (n > buf->len) {
		n = buf->len;
	}
	buf->off += n;
	buf->len -= n;
}

void buffer_push(struct buffer *buf, size_t n)
{
	if (n > buf->cap - buf->off - buf->len) {
		n = buf->cap - buf->off - buf->len;
	}
	buf->len += n;
}

size_t buffer_len(struct buffer *buf)
{
	return buf->len;
}

size_t buffer_rem(struct buffer *buf)
{
	return buf->cap - buf->off - buf->len;
}
