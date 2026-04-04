#ifndef RESPONSE_H
#define RESPONSE_H

#include <stdio.h>
#include <stdarg.h>
#include "mime.h"

size_t strput_httpstatus(GString *s, int code) {
	switch (code) {
		case 100: {
			return gstr_append_fmt(s, "HTTP/1.1 100 Continue\r\n");
		}
		case 200: {
			return gstr_append_fmt(s, "HTTP/1.1 200 OK\r\n");
		}
		case 301: {
			return gstr_append_fmt(s, "HTTP/1.1 301 Moved Permanently\r\n");
		}
		case 400: {
			return gstr_append_fmt(s, "HTTP/1.1 400 Bad Request\r\n");
		}
		case 404: {
			return gstr_append_fmt(s, "HTTP/1.1 404 Not Found\r\n");
		}
		case 411: {
			return gstr_append_fmt(s, "HTTP/1.1 411 Length Required\r\n");
		}
		case 431: {
			return gstr_append_fmt(s, "HTTP/1.1 431 Request Header Fields Too Large\r\n");
		}
	}

	debug("Unknown status code: %d\n", code);
	return 0;
}

void set_response_header(Context *ctx, const char *header, const char *fmt, ...) {
	if (header == NULL || *header == '\0') {
		return;
	}

	va_list arg;
	va_start(arg, fmt);

	GString *key = calloc(1, sizeof(GString));
	gstr_append_fmt(key, "%s", header);
	for (size_t i = 0; i < key->len; i++) {
		if (key->ptr[i] >= 'A' && key->ptr[i] <= 'Z') {
			key->ptr[i] += 'a' - 'A';
		}
	}
	SHashMap *headers = &ctx->response->headers;
	size_t slot_idx = shashmap_find(headers, key);
	if (slot_idx != SHASHMAP_INVALID_SLOT) {
		size_t header_idx = headers->link[slot_idx];
		GString *value = (GString*) headers->value[header_idx];
		gstr_clear(value);
		gstr_append_vfmt(value, fmt, arg);

		gstr_free(key);
		free(key);
		return;
	}

	GString *value = calloc(1, sizeof(GString));
	gstr_append_vfmt(value, fmt, arg);
	shashmap_insert(headers, &key, value);

	va_end(arg);
}

void html(Context *ctx, int status_code, const char *fmt, ...) {
	ctx->status_code = status_code;
	va_list arg;
	va_start(arg, fmt);

	gstr_clear(&ctx->response->body);
	gstr_append_vfmt(&ctx->response->body, fmt, arg);

	va_end(arg);
}

void blob(Context *ctx, int status_code, const char *content_type, const char *blob, size_t blob_len) {
	ctx->status_code = status_code;

	gstr_clear(&ctx->response->body);
	gstr_append_cstr(&ctx->response->body, blob, blob_len);

	ctx->response->headers.len = 0;
	set_response_header(ctx, "Content-Type", "%s", content_type);
}

void stream(Context *ctx, int status_code, const char *content_type, FILE *f) {
	ctx->status_code = status_code;

	ctx->response->body.len = 0;
	gstr_append_fmt(&ctx->response->body, "%F", f);

	ctx->response->headers.len = 0;
	set_response_header(ctx, "Content-Type", "%s", content_type);
}

void file(Context *ctx, int status_code, const char *filepath) {
	ctx->response->body.len = 0;
	ctx->response->headers.len = 0;

	FILE *f = fopen(filepath, "rb");
	if (f == NULL) {
		ctx->status_code = 404;
		return;
	}
	ctx->status_code = status_code;

	const char *filename = strrchr(filepath, '/');
	if (filename == NULL) {
		filename = filepath;
	}
	else {
		filename += 1;
	}

	gstr_append_fmt(&ctx->response->body, "%F", f);

	const char *content_type = find_mime((Slice) { .ptr = ctx->response->body.ptr, .len = ctx->response->body.len });
	set_response_header(ctx, "Content-Type", "%s", content_type);
	set_response_header(ctx, "Content-Disposition", "attachment; filename=\"%s\"", filename);

	fclose(f);
}

void redirect(Context *ctx, int status_code, const char *url) {
	ctx->status_code = status_code;
	ctx->response->headers.len = 0;
	set_response_header(ctx, "Location", url);
}

void no_content(Context *ctx, int status_code) {
	ctx->status_code = status_code;
}

bool send_cstr(int fd, const char *cstr, size_t len) {
	if (len == 0) {
		while (cstr[len] != '\0') {
			len++;
		}
	}

	size_t bytes_sent = 0;
	while (bytes_sent < len) {
		ssize_t sent = send(fd, cstr + bytes_sent, len - bytes_sent, 0);
		if (sent == -1) {
			return false;
		}

		bytes_sent += sent;
	}

	return true;
}

bool send_vfmt(int fd, const char *fmt, va_list arg) {
	size_t pos = strcspn(fmt, "%");
	bool success = true;
	GString arena = {0};

	while (fmt[pos] != '\0') {
		if (pos > 0) {
			success &= send_cstr(fd, fmt, pos);
		}
		fmt += pos + 1;

		switch(*fmt) {
			case '%': {
				fmt++;
				success &= send_cstr(fd, "%", 1);
				break;
			}
			case 's': {
				fmt++;
				char *cs = va_arg(arg, char*);
				if (cs != NULL) {
					success &= send_cstr(fd, cs, 0);
				}
				break;
			}
			case 'l': {
				fmt++;
				if (*fmt == 'd') {
					fmt++;
					size_t n = va_arg(arg, size_t);
					gstr_clear(&arena);
					gstr_append_uint(&arena, n);
					success &= send_cstr(fd, arena.ptr, arena.len);
				}
				break;
			}
			case 'd': {
				fmt++;
				int n = va_arg(arg, int);
				gstr_clear(&arena);
				gstr_append_int(&arena, n);
				success &= send_cstr(fd, arena.ptr, arena.len);
				break;
			}
			case 'S': {
				fmt++;
				if (*fmt == 'l') {
					fmt++;
					Slice sl = va_arg(arg, Slice);
					if (sl.ptr != NULL && sl.len > 0) {
						success &= send_cstr(fd, sl.ptr, sl.len);
					}
				}
				else if (*fmt == 'g') {
					fmt++;
					GString gstr = va_arg(arg, GString);
					if (gstr.ptr != NULL && gstr.len > 0) {
						success &= send_cstr(fd, gstr.ptr, gstr.len);
					}
				}
				break;
			}
		}

		if (!success) {
			return false;
		}
		pos = strcspn(fmt, "%");
	}
	gstr_free(&arena);

	success &= send_cstr(fd, fmt, 0);

	return success;
}

bool send_fmt(int fd, const char *fmt, ...) {
	va_list arg;
	va_start(arg, fmt);
	bool success = send_vfmt(fd, fmt, arg);
	va_end(arg);

	return success;
}

bool send_response(Context *ctx) {
	bool success = true;
	GString arena = {0};
	strput_httpstatus(&arena, ctx->status_code);
	success &= send_fmt(ctx->client, "%Sg", arena);

	if (!shashmap_empty(&ctx->response->headers)) {
		for (size_t slot_idx = 0; slot_idx < ctx->response->headers.capacity; slot_idx++) {
			if (shashmap_occupied_slot(&ctx->response->headers, slot_idx)) {
				size_t idx = ctx->response->headers.link[slot_idx];
				GString key = *ctx->response->headers.key[idx];
				GString value = *(GString*) ctx->response->headers.value[idx];
				success &= send_fmt(ctx->client, "%Sg: %Sg\r\n", key, value);
			}
		}
	}

	gstr_clear(&arena);
	gstr_append_fmt(&arena, "%ld", ctx->response->body.len);
	success &= send_fmt(ctx->client, "Content-Length: %Sg\r\n\r\n", arena);
	if (ctx->response->body.len > 0) {
		success &= send_fmt(ctx->client, "%Sg\r\n", ctx->response->body);
	}

	gstr_free(&arena);

	return success;
}

#endif // RESPONSE_H
