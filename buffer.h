#pragma once

#include <stddef.h>

struct buffer {
	// those fields should be considered private

	char *data;
	// cap is the number of bytes allocated for data.
	size_t cap;
	// head is the index of the first byte present
	// and tail is the index to which the next byte should be written.
	// head == tail implies that the buffer is empty.
	size_t head;
	size_t tail;
};

/* Initializes the buffer.
 */
void buffer_init(struct buffer *buf);

/* Frees all resources allocated by the buffer.
 */
void buffer_finalize(struct buffer *buf);

/* Returns the number of bytes in the buffer.
 */
size_t buffer_len(struct buffer *buf);

/* Returns the ith byte in the buffer.
 */
char buffer_get(struct buffer *buf, size_t i);

/* Ensures that the buffer will be able to store size additional bytes
 * without resizing.
 *
 * Returns 0 on success and -1 on failure.
 */
int buffer_reserve(struct buffer *buf, size_t size);

/* Adds len bytes from src to the back of the buffer. The buffer will
 * be resized if necessary.
 *
 * If the resizing fails, buffer_push will return -1 without modifying
 * the buffer. Otherwise 0 will be returned.
 */
int buffer_push(struct buffer *buf, char *src, size_t len);

/* Copies len bytes from the front of the buffer to dst.
 *
 * If length of the buffer is lower than the requested number of bytes,
 * buffer_pop will return -1. Otherwise 0 will be returned.
 */
int buffer_peek(struct buffer *buf, char *dst, size_t len);

/* Removes len bytes from the front of the buffer and stores them in dst.
 * If dst is NULL buffer_pop will just remove the bytes.
 *
 * If length of the buffer is lower than the requested number of bytes,
 * buffer_pop will return -1 without modifying the buffer.
 * Otherwise 0 will be returned.
 */
int buffer_pop(struct buffer *buf, char *dst, size_t len);

/* Removes len bytes from the front of src and adds them to the back of buf.
 * buf will be resized if necessary.
 *
 * If length of src is lower than requested number of bytes or if resizing
 * of buf fails, buffer_append will return -1 without modifying the buffers.
 * Otherwise 0 will be returned.
 */
int buffer_append(struct buffer *buf, struct buffer *src, size_t len);
