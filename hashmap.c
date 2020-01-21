#include <stdlib.h>
#include <string.h>

#include "hashmap.h"

/* This module implements a hashmap. The hasmap uses open addressing
 * with Robin Hood hashing.
 */

static unsigned long hash(unsigned char *data, size_t len)
{
	unsigned long hash = 5381;
	size_t i;
	for (i = 0; i < len; ++i) {
		hash = (hash * 33) ^ data[i];
	}
	return hash;
}

int hashmap_ptr_equals(void *k1, void *k2)
{
	return k1 == k2;
}

unsigned long hashmap_ptr_hash(void *k)
{
	return hash((unsigned char *)&k, sizeof(k));
}

int hashmap_string_equals(void *k1, void *k2)
{
	return strcmp((char *)k1, (char *)k2) == 0;
}

unsigned long hashmap_string_hash(void *k)
{
	return hash(k, strlen((char *)k));
}

const size_t STARTING_SIZE = 8;
const double MAX_LOAD = 0.8;

struct bucket {
	int taken;
	size_t psl;
	void *key;
	void *value;
};

static void ignore(void *x) {}

void hashmap_init(struct hashmap *h,
		int (*equals)(void *, void *),
		unsigned long (*hash)(void *),
		void (*free_key)(void *),
		void (*free_value)(void *))
{
	h->buckets = NULL;
	h->nbuckets = 0;
	h->nentries = 0;
	h->equals = equals;
	h->hash = hash;
	h->free_key = free_key ? free_key : ignore;
	h->free_value = free_value ? free_value : ignore;
}

void hashmap_finalize(struct hashmap *h)
{
	size_t i;
	for (i = 0; i < h->nbuckets; ++i) {
		if (h->buckets[i].taken) {
			h->free_key(h->buckets[i].key);
			h->free_value(h->buckets[i].value);
		}
	}
	free(h->buckets);
}

static int hashmap_resize(struct hashmap *h, size_t size)
{
	struct hashmap tmp = *h;
	size_t i;
	struct bucket *bucket;
	if (size < h->nentries) {
		return 0;
	}
	tmp.buckets = malloc(size * sizeof(*tmp.buckets));
	tmp.nbuckets = size;
	tmp.nentries = 0;
	if (!tmp.buckets) {
		return -1;
	}
	for (i = 0; i < tmp.nbuckets; ++i) {
		tmp.buckets[i].taken = 0;
	}
	for (i = 0; i < h->nbuckets; ++i) {
		bucket = &h->buckets[i];
		if (bucket->taken) {
			hashmap_insert(&tmp, bucket->key, bucket->value);
		}
	}
	free(h->buckets);
	*h = tmp;
	return 0;
}

int hashmap_insert(struct hashmap *h, void *key, void *value)
{
	size_t i;
	struct bucket tmp, new, *cur;
	if (h->nbuckets == 0) {
		if (hashmap_resize(h, STARTING_SIZE) < 0) {
			return -1;
		}
	}
	if (((double)(h->nentries + 1) / (double)h->nbuckets) > MAX_LOAD) {
		if (hashmap_resize(h, h->nbuckets * 2)) {
			return -1;
		}
	}
	new.taken = 1;
	new.psl = 0;
	new.key = key;
	new.value = value;
	for (i = h->hash(key) % h->nbuckets; ; i = (i+1) % h->nbuckets) {
		cur = &h->buckets[i];
		if (!cur->taken) {
			*cur = new;
			++h->nentries;
			return 0;
		}
		if (h->equals(cur->key, new.key)) {
			h->free_key(cur->key);
			h->free_value(cur->value);
			cur->key = new.key;
			cur->value = new.value;
			return 0;
		}
		if (new.psl > cur->psl) {
			tmp = new;
			new = *cur;
			*cur = tmp;
		}
		++new.psl;
	}
}

static struct bucket *find_bucket(struct hashmap *h, void *key)
{
	struct bucket *bucket;
	size_t psl;
	size_t i;
	if (h->nentries == 0) {
		return NULL;
	}
	psl = 0;
	for (i = h->hash(key) % h->nbuckets; ; i = (i+1) % h->nbuckets) {
		bucket = &h->buckets[i];
		if (!bucket->taken || bucket->psl < psl) {
			return NULL;
		}
		if (h->equals(bucket->key, key)) {
			return bucket;
		}
		++psl;
	}
}

void hashmap_remove(struct hashmap *h, void *key)
{
	struct bucket *bucket = find_bucket(h, key);
	struct bucket *next;
	if (!bucket) {
		return;
	}
	bucket->taken = 0;
	h->free_key(bucket->key);
	h->free_value(bucket->value);
	--h->nentries;
	for (;;) {
		next = &h->buckets[(bucket - h->buckets + 1) % h->nbuckets];
		if (!next->taken || next->psl == 0) {
			break;
		}
		*bucket = *next;
		--bucket->psl;
		next->taken = 0;
		bucket = next;
	}
}

int hashmap_get(struct hashmap *h, void *key, void **valueptr)
{
	struct bucket *bucket = find_bucket(h, key);
	if (!bucket) {
		return -1;
	}
	*valueptr = bucket->value;
	return 0;
}

int hashmap_contains(struct hashmap *h, void *key)
{
	void *tmp;
	return hashmap_get(h, key, &tmp) == 0;
}
