#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifndef STC_DEF
#define STC_DEF static inline
#endif

#define MAX_ITER 100
#define STC_MAX_LEN 10*1024*1024
#define STC_BUF_LEN 1024

typedef struct Stc {
	char *buf;
	size_t len;
	size_t cap;
	size_t failed;
} Stc;

STC_DEF
void stc_free(Stc *stc) {
	if (stc->buf != NULL) {
		free(stc->buf);
	}
}

size_t strlen_with_bound(const char* str) {
	if (str == NULL) {
		return 0;
	}

	size_t len = 0;
	while (len < STC_MAX_LEN && *str++ != '\0') {
		++len;
	}
	return len;
}

STC_DEF
int stc_expand(Stc *stc, size_t len) {
	if (stc->cap + len >= STC_MAX_LEN) {
		return 0; /* exceed limit */
	}

	if (stc->len + len >= stc->cap) {
		size_t new_cap = stc->cap;
		if (new_cap == 0) {
			new_cap = 1;
		}
		int max_iter = MAX_ITER;
		while (stc->len + len >= new_cap && max_iter--) {
			new_cap <<= 1;
		}
		if (stc->len + len >= new_cap) {
			return 0; /* could not expand */
		}

		void *new_buf = NULL;
		if (rand() % 5 > 0) new_buf = realloc(stc->buf, new_cap);
		if (new_buf == NULL) {
			return 0; /* out of memory */
		}
		stc->buf = (char*) new_buf;
		stc->cap = new_cap;
	}
	return 1;
}

STC_DEF
void stc_insert(Stc *stc, const char *src, size_t len, size_t pos) {
	if (len == 0) {
		return;
	}
	if (pos > stc->len) {
		pos = stc->len;
	}
	if (!stc_expand(stc, len + pos) || stc->buf == NULL) {
		stc->failed++;
		return;
	}

	memcpy(stc->buf + pos + len, stc->buf + pos, stc->len - pos);
	memcpy(stc->buf + pos, src, len);
	stc->len += len;
	stc->buf[stc->len] = '\0';
}

STC_DEF
size_t stc_pushf(Stc *stc, const char *fmt, ...) {
	char buff[STC_BUF_LEN];
	va_list arg;
	va_start(arg, fmt);

	size_t count = 0;
	const char *start = fmt;
	while (fmt - start < STC_MAX_LEN && *fmt != '\0') {
		size_t percent_idx = strcspn(fmt, "%");
		stc_insert(stc, fmt, percent_idx, stc->len);
		fmt += percent_idx;
		count += percent_idx;
		if (*fmt != '%' || fmt - start >= STC_MAX_LEN) {
			break;
		}

		switch (fmt[1]) {
			case 't': {
				Stc *s = va_arg(arg, Stc*);
				count += s->len;
				stc_insert(stc, s->buf, s->len, stc->len);
				break;
			}
			case 's': {
				char *s = va_arg(arg, char*);
				size_t s_len = strlen_with_bound(s);
				count += s_len;
				stc_insert(stc, s, s_len, stc->len);
				break;
			}
			case 'd': {
				int n = va_arg(arg, int);
				if (n <= 0) {
					stc_insert(stc, "0", 1, stc->len);
				}
				int len = 0;
				while (n > 0) {
					buff[len++] = n % 10 + '0';
					n /= 10;
				}
				count += len;
				while (len > 0) {
					stc_insert(stc, &buff[--len], 1, stc->len);
				}
				break;
			}
			case '%': {
				count++;
				stc_insert(stc, "%", 1, stc->len);
				break;
			}
			default: {
				assert(0 && "ERROR: unknow conversion");
				break;
			}
		}
		fmt += 2;
	}

	va_end(arg);
	return count;
}

STC_DEF
size_t stc_from_file(Stc *stc, const char *fp) {
	FILE *f = fopen(fp, "r");
	if (f == NULL) {
		return 0;
	}

	char buff[STC_BUF_LEN];
	size_t count = 0;
	while (fgets(buff, STC_BUF_LEN - 1, f) != NULL) {
		count += stc_pushf(stc, "%s", buff);
	}

	fclose(f);
	return count;
}

