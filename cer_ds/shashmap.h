#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "growable_string.h"

enum {
	SHASHMAP_EMPTY_SLOT = ((size_t) -3),
	SHASHMAP_TOMBSTONE_SLOT,
	SHASHMAP_INVALID_SLOT,
};

typedef struct SHashMap {
	GString *key;
	size_t *key_hash;
	GString *value;
	size_t *link;
	size_t ntombstone;
	size_t len;
	size_t capacity;
} SHashMap;

bool shashmap_empty(const SHashMap *hm) {
	return hm->len == 0 || hm->len <= hm->ntombstone;
}

// TODO: find a better hash function
size_t shashmap_hash(const char *s, size_t len) {
	size_t hash = 1;
	for (size_t i = 0; i < len; i++) {
		hash *= s[i];
		hash ^= s[i];
	}

	return hash;
}

// TODO: save the old data if the realloc failed
bool shashmap_reserve(SHashMap *hm, size_t new_cap) {
	if (hm->capacity >= new_cap) {
		return true;
	}

	if (hm->capacity > 0) {
		hm->key = realloc(hm->key, sizeof(*hm->key) * new_cap);
		hm->key_hash = realloc(hm->key_hash, sizeof(*hm->key_hash) * new_cap);
		hm->value = realloc(hm->value, sizeof(*hm->value) * new_cap);
		hm->link = realloc(hm->link, sizeof(*hm->link) * new_cap);
	}
	else {
		hm->key = malloc(sizeof(*hm->key) * new_cap);
		hm->key_hash = malloc(sizeof(*hm->key_hash) * new_cap);
		hm->value = malloc(sizeof(*hm->value) * new_cap);
		hm->link = malloc(sizeof(*hm->link) * new_cap);
	}

	if (hm->key == NULL || hm->key_hash == NULL || hm->value == NULL || hm->link == NULL) {
		return false;
	}

	for (size_t i = hm->capacity; i < new_cap; i++) {
		hm->link[i] = SHASHMAP_EMPTY_SLOT;
	}
	hm->capacity = new_cap;

	return true;
}

bool shashmap_occupied_slot(SHashMap *hm, size_t slot_idx) {
	return hm->link[slot_idx] < SHASHMAP_EMPTY_SLOT;
}

bool shashmap_rehash(SHashMap *hm) {
	size_t new_cap = hm->capacity;
	int iter = 63;
	while (iter-- > 0 && new_cap <= 2 * (hm->len - hm->ntombstone)) {
		new_cap = new_cap * 2;
	}
	if (new_cap <= hm->capacity) {
		return false;
	}

	if (!shashmap_reserve(hm, new_cap)) {
		return false;
	}

	size_t *new_link = malloc(sizeof(*new_link) * new_cap);
	for (size_t i = 0; i < new_cap; i++) {
		new_link[i] = SHASHMAP_EMPTY_SLOT;
	}
	for (size_t slot_idx = 0; slot_idx < hm->capacity; slot_idx++) {
		if (shashmap_occupied_slot(hm, slot_idx)) {
			size_t idx = hm->link[slot_idx];
			size_t new_slot_idx = hm->key_hash[idx] % hm->capacity;
			while (new_link[new_slot_idx] < SHASHMAP_EMPTY_SLOT) {
				new_slot_idx = (new_slot_idx + 1) % new_cap;
			}
			new_link[new_slot_idx] = idx;
		}
	}

	free(hm->link);

	hm->link = new_link;

	return true;
}

size_t shashmap_insert(SHashMap *hm, GString *s, GString *v) {
	size_t threshold = 75;
	bool success = true;
	if (hm->capacity == 0) {
		success &= shashmap_reserve(hm, 1);
	}
	else if ((hm->len - hm->ntombstone) >= hm->capacity * threshold / 100) {
		success &= shashmap_rehash(hm);
	}

	if (!success) {
		return SHASHMAP_INVALID_SLOT;
	}

	size_t key_hash = shashmap_hash(s->ptr, s->len), starting = key_hash % hm->capacity;

	for (size_t i = 0; i < hm->capacity; i++) {
		size_t slot_idx = (starting + i) % hm->capacity;

		bool is_tombstone = hm->link[slot_idx] == SHASHMAP_TOMBSTONE_SLOT;
		bool is_empty = hm->link[slot_idx] == SHASHMAP_EMPTY_SLOT;
		if (is_tombstone || is_empty) {
			GString key = {0}, value = {0};
			gstr_append_fmt(&key, "%Sg", *s);
			gstr_append_fmt(&value, "%Sg", *v);
			hm->key[hm->len] = key;
			hm->key_hash[hm->len] = key_hash;
			hm->value[hm->len] = value;
			hm->link[slot_idx] = hm->len;
			hm->ntombstone -= is_tombstone;
			hm->len += is_empty;
			return slot_idx;
		}
		else {
			size_t idx = hm->link[slot_idx];
			if (s->len == hm->key[idx].len && memcmp(s->ptr, hm->key[idx].ptr, s->len) == 0) {
				return slot_idx;
			}
		}
	}

	return SHASHMAP_INVALID_SLOT;
}

size_t shashmap_find_cstr(SHashMap *hm, const char *key, size_t len) {
	if (hm->capacity == 0) {
		return SHASHMAP_INVALID_SLOT;
	}

	size_t key_hash = shashmap_hash(key, len), starting = key_hash % hm->capacity;

	for (size_t i = 0; i < hm->capacity; i++) {
		size_t slot_idx = (starting + i) % hm->capacity;

		if (hm->link[slot_idx] == SHASHMAP_EMPTY_SLOT) {
			return SHASHMAP_INVALID_SLOT;
		}
		else if (!shashmap_occupied_slot(hm, slot_idx)) {
			continue;
		}

		size_t idx = hm->link[slot_idx];
		if (len == hm->key[idx].len && memcmp(key, hm->key[idx].ptr, len) == 0) {
			return slot_idx;
		}
	}

	return SHASHMAP_INVALID_SLOT;
}

size_t shashmap_find(SHashMap *hm, GString *key) {
	return shashmap_find_cstr(hm, key->ptr, key->len);
}

bool shashmap_delete(SHashMap *hm, GString *key) {
	size_t slot_idx = shashmap_find(hm, key);
	if (slot_idx == SHASHMAP_INVALID_SLOT) {
		return false;
	}

	hm->link[slot_idx] = SHASHMAP_TOMBSTONE_SLOT;
	hm->ntombstone++;
	return true;
}

void shashmap_free(SHashMap *hm) {
	for (size_t slot_idx = 0; slot_idx < hm->capacity; slot_idx++) {
		if (shashmap_occupied_slot(hm, slot_idx)) {
			size_t idx = hm->link[slot_idx];
			gstr_free(&hm->key[idx]);
			gstr_free(&hm->value[idx]);
		}
	}

	free(hm->key);
	free(hm->key_hash);
	free(hm->value);
	free(hm->link);
}

#endif // HASHMAP_H
