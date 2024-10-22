#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef STC_DEF
#define STC_DEF static inline
#endif

#define MAX_ITER 100
#define STC_MAX_LEN 10*1024*1024

typedef struct Stc {
	char *buf;
	size_t len;
	size_t cap;
} Stc;

STC_DEF
void stc_set_len(Stc *stc, size_t len) {
	assert(len < stc->cap && "The new length larger than the capacity");
	stc->len = len;
	stc->buf[len] = '\0';
}

STC_DEF
void stc_free(Stc *stc) {
	if (stc->buf != NULL) {
		free(stc->buf);
	}
}

STC_DEF
int stc_compare(const Stc *stc1, const Stc *stc2) {
	size_t len = stc1->len < stc2->len ? stc1->len : stc2->len;
	int cmp = memcmp(stc1->buf, stc2->buf, len);
	if (cmp != 0) {
		return cmp;
	}
	return stc1->len < stc2->len ? -1: stc1->len != stc2->len;
}

size_t strlen_with_bound(const char* str) {
	if (str == NULL) {
		return 0;
	}

	size_t len = 0;
	while (*str++ != '\0' && len < STC_MAX_LEN) {
		++len;
	}
	return len;
}

STC_DEF
int stc_compare_str(const Stc *stc, const char *str) {
	size_t str_len = strlen_with_bound(str);
	size_t len = stc->len < str_len ? stc->len : str_len;
	int cmp = memcmp(stc->buf, str, len);
	if (cmp != 0) {
		return cmp;
	}
	return stc->len < str_len ? -1: stc->len != str_len;
}

STC_DEF
int stc_expand(Stc *stc, size_t len) {
	assert(STC_MAX_LEN - stc->cap >= len && "ERROR: reached STC_MAX_LEN");

	if (stc->len + len >= stc->cap) {
		if (stc->cap == 0) {
			stc->cap = 1;
		}
		int max_iter = MAX_ITER;
		while (stc->len + len >= stc->cap && max_iter--) {
			stc->cap <<= 1;
		}
		if (stc->len + len >= stc->cap) {
			return 0;
		}

		stc->buf = (char*) realloc(stc->buf, stc->cap);
		assert(stc->buf	!= NULL && "ERROR: out of memory");
	}
	return 1;
}

STC_DEF
void stc_insert_str_with_len(Stc *stc, const char *src, size_t len, size_t pos) {
	if (len == 0) {
		return;
	}
	if (pos > stc->len) {
		pos = stc->len;
	}
	if (!stc_expand(stc, len + pos)) {
		return;
	}

	memmove(stc->buf + pos + len, stc->buf + pos, stc->len - pos);
	memcpy(stc->buf + pos, src, len);
	stc_set_len(stc, stc->len + len);
}

STC_DEF
void stc_insert_str(Stc *stc, const char *src, size_t pos) {
	stc_insert_str_with_len(stc, src, strlen_with_bound(src), pos);
}

STC_DEF
void stc_push_back(Stc *stc, const char *src) {
	stc_insert_str_with_len(stc, src, strlen_with_bound(src), stc->len);
}

STC_DEF
void stc_from_file(Stc *stc, const char *fp) {
	FILE *f = fopen(fp, "r");
	if (f == NULL) {
		return;
	}

	char buff[1024];
	while (fgets(buff, 1023, f) != NULL) {
		stc_push_back(stc, buff);
	}

	fclose(f);
}

STC_DEF
void stc_debug(const Stc *stc) {
	printf("%s (len: %zd, CAPACITY: %zd)\n", stc->buf, stc->len, stc->cap);
}

