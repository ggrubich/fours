#pragma once

/* Comparator and hash function that treat their arguments as numeric values.
 */
int hashmap_ptr_equals(void *k1, void *k2);
unsigned long hashmap_ptr_hash(void *k);

/* Comparator and hash function that treat their arguments
 * as null-delimited strnigs.
 */
int hashmap_string_equals(void *k1, void *k2);
unsigned long hashmap_string_hash(void *k);

struct hashmap {
	// these sould be treated as private
	struct bucket *buckets;
	size_t nbuckets;
	size_t nentries;
	int (*equals)(void *, void *);
	unsigned long (*hash)(void *);
	void (*free_key)(void *);
	void (*free_value)(void *);
};

/* Initalizes a hashmap. Assumes that h is already allocated.
 * The following arguments are required:
 * equals - Function used to compare hashmap keys. Should return nonzero value
 *     if keys are equal and zero otherise.
 * hash - Function used to hash keys.
 * free_key, free_value - Destructors for keys and values. Passing NULL
 *     here is equivalent to passing a function that does nothing.
 */
void hashmap_init(struct hashmap *h,
		int (*equals)(void *, void *),
		unsigned long (*hash)(void *),
		void (*free_key)(void *),
		void (*free_value)(void *));

/* Releases all resources associated with the hashmap.
 */
void hashmap_finalize(struct hashmap *h);

/* Adds the key-value pair to the hasmap. Returns 0 on success and -1 on failure.
 * If the insertion succeeds, the hashmap will take ownership of key and value
 * and will free them using supplied destructors once the key is removed from
 * the map or when the map is finalized.
 */
int hashmap_insert(struct hashmap *h, void *key, void *value);

/* Removes key from the hashmap. If the key is not present does nothing.
 */
void hashmap_remove(struct hashmap *h, void *key);

/* Retrieves value associated with the key. Returns 0 if value was found
 * and -1 otherwise.
 */
int hashmap_get(struct hashmap *h, void *key, void **valueptr);
