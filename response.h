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
		value->len = 0;
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

	ctx->response->body.len = 0;
	gstr_append_vfmt(&ctx->response->body, fmt, arg);

	va_end(arg);
}

void blob(Context *ctx, int status_code, const char *content_type, const char *blob, size_t blob_len) {
	ctx->status_code = status_code;

	ctx->response->body.len = 0;
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

bool send_response(Context *ctx) {
	GString arena = {0};
	bool result = true;
	strput_httpstatus(&arena, ctx->status_code);

	if (ctx->response->headers.len > 0) {
		for (size_t slot_idx = 0; slot_idx < ctx->response->headers.capacity; slot_idx++) {
			if (shashmap_occupied_slot(&ctx->response->headers, slot_idx)) {
				size_t idx = ctx->response->headers.link[slot_idx];
				GString key = *ctx->response->headers.key[idx];
				GString value = *(GString*) ctx->response->headers.value[idx];
				gstr_append_fmt(&arena, "%Sg: %Sg\r\n", key, value);
			}
		}
	}

	gstr_append_fmt(&arena, "Content-Length: %ld\r\n\r\n", ctx->response->body.len);
	if (ctx->response->body.len > 0) {
		gstr_append_fmt(&arena, "%Sg\r\n", ctx->response->body);
	}

	size_t bytes_sent = 0;
	// debug("Response:\n[%.*s]", (int) arena.len, arena.ptr);
	while (bytes_sent < arena.len) {
		ssize_t sent = send(ctx->client, arena.ptr + bytes_sent, arena.len - bytes_sent, 0);
		if (sent == -1) {
			// debug("Only sent %ld bytes because of the error", arena.len - bytes_left);
			result = false;
			break;
		}
		bytes_sent += sent;
	}

	gstr_free(&arena);
	return result;
}

#endif // RESPONSE_H
