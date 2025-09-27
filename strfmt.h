#include "stb_ds.h"

void tolowerstr(char *s) {
	while (*s != '\0') {
		if (*s >= 'A' && *s <= 'Z') {
			*s += ('a' - 'A');
		}
		s++;
	}
}

void strsetlen(char **s, size_t len) {
	if (*s == NULL) {
		return;
	}

	arrsetlen(*s, len == 0 ? 1 : len);
	if (len == 0) {
		(void) arrpop(*s);
	}
}

size_t strputstr(char **s, const char *cstr, size_t len) {
	if (len == 0) {
		len = strlen(cstr);
		if (len == 0) {
			return 0;
		}
	}

	stbds_arrmaybegrow(*s, len);
	memcpy(*s + arrlenu(*s), cstr, len);
	strsetlen(s, arrlenu(*s) + len);

	return len;
}

size_t strputu(char **s, size_t n) {
	if (n == 0) {
		arrput(*s, '0');
		return 1;
	}

	size_t nlen = 0;
	while (n > 0) {
		arrput(*s, (n % 10) + '0');
		nlen += 1;
		n /= 10;
	}

	char *old = *s + arrlen(*s) - nlen;
	for (size_t i = 0; i < nlen/2; i++) {
		char c = old[i];
		old[i] = old[nlen - 1 - i];
		old[nlen - 1 - i] = c;
	}

	return nlen;
}

size_t strput_httpstatus(char **s, int code) {
	switch (code) {
		case 100: {
			return strputstr(s, "HTTP/1.1 100 Continue\r\n", 23);
		}
		case 200: {
			return strputstr(s, "HTTP/1.1 200 OK\r\n", 17);
		}
		case 301: {
			return strputstr(s, "HTTP/1.1 301 Moved Permanently\r\n", 32);
		}
		case 400: {
			return strputstr(s, "HTTP/1.1 400 Bad Request\r\n", 26);
		}
		case 404: {
			return strputstr(s, "HTTP/1.1 404 Not Found\r\n", 24);
		}
	}

	printf("Unknown status code: %d\n", code);
	return 0;
}

// Flags
// %%: append '%'
// %0: append '\0'
// %s: char* (null-terminated string)
// %d: int
// %ld: size_t
// %S: char* (stb string)
// %F: FILE*
size_t vstrputfmt(char **s, const char *fmt, va_list arg) {
	size_t pos = strcspn(fmt, "%"), len = 0;
	while (fmt[pos] != '\0') {
		if (pos > 0) {
			len += strputstr(s, fmt, pos);
		}
		fmt += pos + 1;

		switch(*fmt) {
			case '%': {
				fmt++;
				arrput(*s, '%');
				len++;
				break;
			}
			case '0': {
				fmt++;
				arrput(*s, '\0');
				break;
			}
			case 's': {
				fmt++;
				char *cs = va_arg(arg, char*);
				if (cs != NULL) {
					len += strputstr(s, cs, 0);
				}
				break;
			}
			case 'l': {
				fmt++;
				if (*fmt == 'd') {
					fmt++;
					size_t n = va_arg(arg, size_t);
					len += strputu(s, n);
				}
				break;
			}
			case 'd': {
				fmt++;
				int n = va_arg(arg, int);
				if (n < 0) {
					arrput(*s, '-');
					len += 1;
					n = -n;
				}

				len += strputu(s, n);
				break;
			}
			case 'S': {
				fmt++;
				char *stbs = va_arg(arg, char*);
				if (stbs != NULL) {
					len += strputstr(s, stbs, arrlenu(stbs));
				}
				break;
			}
			case 'F': {
				fmt++;
				FILE *f = va_arg(arg, FILE*);
				long m = 0;
				if (f != NULL && fseek(f, 0, SEEK_END) >= 0 && (m = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) >= 0) {
					stbds_arrmaybegrow(*s, m);
					if (fread(*s + arrlen(*s), m, 1, f) == 1) {
						strsetlen(s, arrlen(*s) + m);
						len += m;
					}
				}
				break;
			}
		}

		pos = strcspn(fmt, "%");
	}
	len += strputstr(s, fmt, 0);

	return len;
}

size_t strputfmt(char **s, const char *fmt, ...) {
	va_list arg;
	va_start(arg, fmt);
	size_t len = vstrputfmt(s, fmt, arg);
	va_end(arg);

	return len;
}

