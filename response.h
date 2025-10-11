#ifndef RESPONSE_H
#define RESPONSE_H

#include <stdio.h>
#include <stdarg.h>

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

	printf("Unknown status code: %d\n", code);
	return 0;
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

	GString content_type_header = {0};
	gstr_append_fmt(&content_type_header, "Content-Type: %s", content_type);
	ctx->response->headers.len = 0;
	gstr_append_fmt(&ctx->response->headers, "%Sg", content_type_header);

	free(content_type_header.ptr);
}

void stream(Context *ctx, int status_code, const char *content_type, FILE *f) {
	ctx->status_code = status_code;

	ctx->response->body.len = 0;
	gstr_append_fmt(&ctx->response->body, "%F", f);

	GString content_type_header = {0};
	gstr_append_fmt(&content_type_header, "Content-Type: %s", content_type);
	ctx->response->headers.len = 0;
	gstr_append_fmt(&ctx->response->headers, "%Sg", content_type_header);

	free(content_type_header.ptr);
}

void redirect(Context *ctx, int status_code, const char *url) {
	ctx->status_code = status_code;
	ctx->response->headers.len = 0;
	gstr_append_fmt(&ctx->response->headers, "Location: %s", url);
}

void no_content(Context *ctx, int status_code) {
	ctx->status_code = status_code;
}

bool send_response(Context *ctx) {
	GString arena = {0};
	bool result = true;
	strput_httpstatus(&arena, ctx->status_code);

	if (ctx->response->headers.len > 0) {
		gstr_append_fmt(&arena, "%Sg\r\n", ctx->response->headers);
	}
	if (ctx->response->body.len > 0) {
		gstr_append_fmt(&ctx->response->body, "\r\n");
	}
	gstr_append_fmt(&arena, "Content-Length: %ld\r\n\r\n%Sg", ctx->response->body.len, ctx->response->body);

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

	free(arena.ptr);
	return result;
}

#endif // RESPONSE_H
