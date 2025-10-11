#ifndef CER_DS_GROWABLE_STRING_H
#define CER_DS_GROWABLE_STRING_H

#include <stdbool.h>
#include <string.h>
#include "slice.h"

#define MAX_CSTRING_LEN 10240

typedef struct {
	char *ptr;
	size_t len;
	size_t capacity;
} GString;

bool gstr_reserve(GString *gs, size_t additional) {
	size_t new_len = gs->len + additional;
	if (new_len < gs->capacity) {
		return true;
	}

	size_t new_cap = gs->capacity * 2;
	if (new_cap <= new_len) {
		new_cap = new_len + 1;
	}
	if (new_cap <= new_len) {
		return false;
	}

	char *ptr = realloc(gs->ptr, new_cap);
	if (ptr == NULL) {
		return false;
	}

	gs->ptr = ptr;
	gs->capacity = new_cap;

	return true;
}

size_t cstrlen(const char *cstr) {
	size_t len = 0;
	while (len < MAX_CSTRING_LEN && *cstr != '\0') {
		len += 1;
		cstr += 1;
	}
	return len;
}

GString gstr_from_cstr(const char *cstr) {
	size_t len = cstrlen(cstr);
	GString gs = {0};
	gs.ptr = malloc(len);
	if (gs.ptr == NULL) {
		return gs;
	}

	memcpy(gs.ptr, cstr, len);
	gs.len = gs.capacity = len;

	return gs;
}

size_t gstr_append_cstr(GString *gs, const char *cstr, size_t len) {
	if (len == 0) {
		len = cstrlen(cstr);
	}

	if (!gstr_reserve(gs, len)) {
		return 0;
	}
	memcpy(gs->ptr + gs->len, cstr, len);
	gs->len += len;

	return len;
}

size_t uint_len(size_t n) {
	if (n == 0) {
		return 1;
	}

	size_t res = 0;
	while (n > 0) {
		n /= 10;
		res += 1;
	}

	return res;
}

size_t gstr_append_uint(GString *gs, size_t n) {
	if (n == 0) {
		if (!gstr_reserve(gs, 1)) {
			return false;
		}

		gs->ptr[gs->len++] = '0';
		return true;
	}

	size_t ndigits = uint_len(n);
	if (!gstr_reserve(gs, ndigits)) {
		return 0;
	}

	while (n > 0) {
		gs->ptr[gs->len++] = (n % 10) + '0';
		n /= 10;
	}

	char *ptr = gs->ptr + gs->len - ndigits;
	for (size_t i = 0; i < ndigits/2; i++) {
		char c = ptr[i];
		ptr[i] = ptr[ndigits - 1 - i];
		ptr[ndigits - 1 - i] = c;
	}

	return ndigits;
}

size_t gstr_append_int(GString *gs, int n) {
	bool is_neg = false;
	if (n < 0) {
		n = -n;
		is_neg = true;
	}
	size_t ndigits = uint_len(n);
	if (!gstr_reserve(gs, ndigits + is_neg)) {
		return false;
	}
	if (is_neg) {
		gs->ptr[gs->len++] = '-';
	}

	return gstr_append_uint(gs, n) + is_neg;
}

size_t gstr_append_null(GString *gs) {
	if (!gstr_reserve(gs, 1)) {
		return 0;
	}
	gs->ptr[gs->len] = '\0';

	return 0;
}

size_t gstr_append_vfmt(GString *gs, const char *fmt, va_list arg) {
	size_t pos = strcspn(fmt, "%"), len = 0;
	while (fmt[pos] != '\0') {
		if (pos > 0) {
			len += gstr_append_cstr(gs, fmt, pos);
		}
		fmt += pos + 1;

		switch(*fmt) {
			case '%': {
				fmt++;
				gstr_append_cstr(gs, "%", 1);
				len++;
				break;
			}
			case 's': {
				fmt++;
				char *cs = va_arg(arg, char*);
				if (cs != NULL) {
					len += gstr_append_cstr(gs, cs, 0);
				}
				break;
			}
			case 'l': {
				fmt++;
				if (*fmt == 'd') {
					fmt++;
					size_t n = va_arg(arg, size_t);
					len += gstr_append_uint(gs, n);
				}
				break;
			}
			case 'd': {
				fmt++;
				int n = va_arg(arg, int);
				len += gstr_append_int(gs, n);
				break;
			}
			case 'S': {
				fmt++;
				if (*fmt == 'l') {
					fmt++;
					Slice sl = va_arg(arg, Slice);
					if (sl.ptr != NULL && sl.len > 0) {
						len += gstr_append_cstr(gs, sl.ptr, sl.len);
					}
				}
				else if (*fmt == 'g') {
					fmt++;
					GString gstr = va_arg(arg, GString);
					if (gstr.ptr != NULL && gstr.len > 0) {
						len += gstr_append_cstr(gs, gstr.ptr, gstr.len);
					}
				}
				break;
			}
			case 'F': {
				fmt++;
				FILE *f = va_arg(arg, FILE*);
				long m = 0;
				if (f != NULL && fseek(f, 0, SEEK_END) >= 0 && (m = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) >= 0 &&
						gstr_reserve(gs, m) && fread(gs->ptr + gs->len, m, 1, f) == 1) {
					gs->len += m;
					len += m;
				}
				break;
			}
		}

		pos = strcspn(fmt, "%");
	}
	len += gstr_append_cstr(gs, fmt, 0);

	return len;
}

size_t gstr_append_fmt(GString *gs, const char *fmt, ...) {
	va_list arg;
	va_start(arg, fmt);
	size_t len = gstr_append_vfmt(gs, fmt, arg);
	va_end(arg);

	return len;
}

size_t gstr_append_fmt_null(GString *gs, const char *fmt, ...) {
	va_list arg;
	va_start(arg, fmt);
	size_t len = gstr_append_vfmt(gs, fmt, arg);
	gstr_append_null(gs);
	va_end(arg);

	return len;
}

#endif // CER_DS_GROWABLE_STRING_H
