#ifndef CER_DS_SLICE_H
#define CER_DS_SLICE_H

#include <stdbool.h>

typedef struct {
	const char *ptr;
	size_t len;
} Slice;

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

char *slice_slice(Slice s, Slice needle) {
	size_t si = 0, match_len = 0;
	while (match_len < needle.len && si < s.len) {
		if (s.ptr[si] != needle.ptr[si]) {
			match_len = 0;
		}
		else {
			match_len += 1;
		}
		si += 1;
	}

	return match_len == needle.len ? (char*) s.ptr + si - match_len : NULL;
}

size_t find_slice_in_slices(Slice *s, Slice key) {
	size_t slen = arrlenu(s);
	for (size_t i = 0; i < slen; i++) {
		if (slice_equal(s[i], key)) {
			return i;
		}
	}

	return slen;
}

char *slice_strstr(Slice s, const char *needle) {
	size_t si = 0, match_len = 0, needle_len = strlen(needle);
	while (match_len < needle_len && si < s.len) {
		if (s.ptr[si] != needle[match_len]) {
			match_len = 0;
		}
		if (s.ptr[si] == needle[match_len]) {
			match_len += 1;
		}
		si += 1;
	}

	return match_len == needle_len ? (char*) s.ptr + si - match_len : NULL;
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
