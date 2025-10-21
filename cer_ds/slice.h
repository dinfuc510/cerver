#ifndef CER_DS_SLICE_H
#define CER_DS_SLICE_H

#include <stdbool.h>

typedef struct {
	const char *ptr;
	size_t len;
} Slice;

#define slice_bytes(bytes) (Slice) { .ptr = bytes, .len = sizeof(bytes) - 1 }

bool slice_equal(Slice a, Slice b) {
	return a.len == b.len && strncmp(a.ptr, b.ptr, a.len) == 0;
}

bool slice_equal_cstr(Slice s, const char *cs) {
	size_t si = 0;
	while (si < s.len && *cs != '\0') {
		if (s.ptr[si] != *cs) {
			break;
		}
		si += 1;
		cs += 1;
	}

	return si == s.len && *cs == '\0';
}

Slice slice_cstr(const char *cstr) {
	return (Slice) { .ptr = cstr, .len = strlen(cstr) };
}

size_t slice_cspn(Slice s, const char *reject) {
	for (size_t i = 0; i < s.len; i++) {
		if (strchr(reject, s.ptr[i]) != NULL) {
			return i;
		}
	}

	return s.len;
}

const char *slice_slice(Slice s, Slice needle) {
	if (needle.len == 0) {
		return s.ptr;
	}

	ssize_t bad_chars[256];
	for (size_t i = 0; i < sizeof(bad_chars)/sizeof(bad_chars[0]); i++) {
		bad_chars[i] = -1;
	}
	for (size_t i = 0; i < needle.len; i++) {
		bad_chars[(unsigned char) needle.ptr[i]] = i;
	}

	size_t i = 0;
	while (i <= s.len - needle.len) {
		ssize_t mismatch_idx = needle.len - 1;
		while (mismatch_idx >= 0 && (s.ptr[i + mismatch_idx] == needle.ptr[mismatch_idx])) {
			mismatch_idx -= 1;
		}
		if (mismatch_idx < 0) {
			return s.ptr + i;
		}

		ssize_t bc = bad_chars[(unsigned char) s.ptr[i + mismatch_idx]];
		i += (mismatch_idx > bc) ? mismatch_idx - bc : 1;
	}

	return NULL;
}

size_t find_slice_in_slices(const Slice *s, size_t len, Slice key) {
	for (size_t i = 0; i < len; i++) {
		if (slice_equal(s[i], key)) {
			return i;
		}
	}

	return len;
}

const char *slice_strstr(Slice s, const char *needle) {
	return slice_slice(s, (Slice) { .ptr = needle, .len = strlen(needle) });
}

const char *slice_stristr(Slice s, const char *needle) {
	if (*needle == '\0') {
		return s.ptr;
	}

	size_t i = 0;
	char needle0 = tolower((unsigned char) needle[0]);
	while (i < s.len) {
		if (tolower((unsigned char) s.ptr[i]) == needle0) {
			size_t match_len = 0;
			while (needle[match_len] != '\0' && i < s.len && tolower((unsigned char) s.ptr[i]) == tolower((unsigned char) needle[match_len])) {
				i += 1;
				match_len += 1;
			}
			if (needle[match_len] == '\0') {
				return s.ptr + i - match_len;
			}
		}
		i += 1;
	}

	return NULL;
}

char *slice_strndup(Slice s, size_t len) {
	if (s.len < len) {
		len = s.len;
	}

	char *dup = malloc(len + 1);
	if (dup == NULL) {
		return NULL;
	}
	strncpy(dup, s.ptr, len);
	dup[len] = '\0';

	return dup;
}

Slice slice_advanced(Slice s, size_t len) {
	if (len > s.len) {
		len = s.len;
	}

	s.ptr += len;
	s.len -= len;
	return s;
}

#endif // CER_DS_SLICE_H
