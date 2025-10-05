#ifndef CER_DS_PAIR_H
#define CER_DS_PAIR_H

#include "slice.h"

typedef struct {
	Slice *keys;
	Slice *values;
	size_t len;
	size_t capacity;
} Pairs;

Slice find_key_in_pairs(Pairs *pairs, Slice key) {
	for (size_t i = 0; i < pairs->len; i++) {
		if (slice_equal(pairs->keys[i], key)) {
			return pairs->values[i];
		}
	}

	return (Slice) {0};
}

bool append_pair(Pairs *pairs, Slice key, Slice value) {
	if (pairs->len >= pairs->capacity) {
		size_t new_cap = pairs->len*2;
		if (pairs->len >= new_cap) {
			new_cap = pairs->len + 1;
		}
		if (pairs->len >= new_cap) {
			return false;
		}

		Slice *new_keys = realloc(pairs->keys, new_cap*sizeof(Slice));
		if (new_keys == NULL) {
			return false;
		}
		pairs->keys = new_keys;
		Slice *new_values = realloc(pairs->values, new_cap*sizeof(Slice));
		if (new_values == NULL) {
			return false;
		}
		pairs->values = new_values;

		pairs->capacity = new_cap;
	}

	pairs->keys[pairs->len] = key;
	pairs->values[pairs->len] = value;
	pairs->len++;
	return true;
}

#endif // CER_DS_PAIR_H
