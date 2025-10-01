#include <stdio.h>
#include <stdarg.h>
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
			case 'L': {
				fmt++;
				if (*fmt == 's') {
					fmt++;
					Slice sl = va_arg(arg, Slice);
					if (sl.ptr != NULL && sl.len > 0) {
						strputstr(s, sl.ptr, sl.len);
					}
				}
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

size_t strputfmtn(char **s, const char *fmt, ...) {
	va_list arg;
	va_start(arg, fmt);
	size_t len = vstrputfmt(s, fmt, arg);
	arrput(*s, '\0');
	va_end(arg);

	return len + 1;
}

void html(Context *ctx, int status_code, const char *fmt, ...) {
	ctx->status_code = status_code;
	va_list arg;
	va_start(arg, fmt);

	strsetlen(&ctx->response_body, 0);
	vstrputfmt(&ctx->response_body, fmt, arg);

	va_end(arg);
}

void blob(Context *ctx, int status_code, const char *content_type, const char *blob, size_t blob_len) {
	ctx->status_code = status_code;

	strsetlen(&ctx->response_body, 0);
	strputstr(&ctx->response_body, blob, blob_len);

	char *content_type_header = NULL;
	strputfmt(&content_type_header, "Content-Type: %s", content_type);
	strsetlen(&ctx->response_header, 0);
	strputstr(&ctx->response_header, content_type_header, arrlenu(content_type_header));

	arrfree(content_type_header);
}

void stream(Context *ctx, int status_code, const char *content_type, FILE *f) {
	ctx->status_code = status_code;

	strsetlen(&ctx->response_body, 0);
	strputfmt(&ctx->response_body, "%F", f);

	char *content_type_header = NULL;
	strputfmt(&content_type_header, "Content-Type: %s", content_type);
	strsetlen(&ctx->response_header, 0);
	strputstr(&ctx->response_header, content_type_header, arrlenu(content_type_header));

	arrfree(content_type_header);
}

void redirect(Context *ctx, int status_code, const char *url) {
	ctx->status_code = status_code;
	strsetlen(&ctx->response_header, 0);
	strputfmt(&ctx->response_header, "Location: %s", url);
}

void no_content(Context *ctx, int status_code) {
	ctx->status_code = status_code;
}

bool send_response(Context *ctx) {
	char *arena = NULL;
	bool result = true;
	strput_httpstatus(&arena, ctx->status_code);

	if (arrlenu(ctx->response_header) > 0) {
		strputfmt(&arena, "%S\r\n", ctx->response_header);
	}
	if (arrlenu(ctx->response_body) > 0) {
		strputfmt(&ctx->response_body, "\r\n");
		strputfmt(&arena, "Content-Length: %ld\r\n\r\n%S", arrlenu(ctx->response_body), ctx->response_body);
	}

	size_t bytes_left = arrlenu(arena);
	// debug("Response:\n%.*s", (int) bytes_left, arena);
	while (bytes_left > 0) {
		ssize_t sent = send(ctx->client, arena, bytes_left, 0);
		if (sent == -1) {
			// debug("Only sent %ld bytes because of the error", arrlenu(arena) - bytes_left);
			result = false;
			break;
		}
		bytes_left -= sent;
	}

	arrfree(arena);
	return result;
}

