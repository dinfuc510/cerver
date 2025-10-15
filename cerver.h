#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#include "cer_ds.h"
#include "response.h"
#include "request.h"

GString get_raw_request(int client, int *error) {
	char buffer[4096];
	GString plain_text = {0};
	ssize_t bytes_read = read(client, buffer, sizeof(buffer));
	if (bytes_read <= 0) {
		*error = 400;
		return plain_text;
	}

	char *crlf_crlf = strstr(buffer, "\r\n\r\n");
	if (crlf_crlf == NULL) {
		*error = 431;
		return plain_text;
	}
	static const char content_length_header[] = "\r\ncontent-length: ";
	const char *content_length_ptr = slice_stristr((Slice) { .ptr = buffer, .len = bytes_read }, content_length_header);
	size_t content_length = 0;
	if (content_length_ptr != NULL && content_length_ptr <= crlf_crlf) {
		content_length_ptr += strlen(content_length_header);

		while (*content_length_ptr != '\r') {
			if (!isdigit(*content_length_ptr)) {
				*error = 400;
				return plain_text;
			}

			content_length = content_length * 10 + (*content_length_ptr - '0');
			content_length_ptr += 1;
		}
		if (content_length_ptr[1] != '\n') {
			return plain_text;
		}
	}

	size_t bytes_left = 0;	// maximum size of the request
	if (content_length > 0) {
		bytes_left = (crlf_crlf - buffer) + strlen("\r\n\r\n") + content_length - bytes_read;
	}
	gstr_append_cstr(&plain_text, buffer, bytes_read);

	while (bytes_left > 0 || bytes_read == sizeof(buffer)) {
		bytes_read = read(client, buffer, sizeof(buffer));
		if (bytes_read == 0) {
			break;
		}
		if (bytes_read == -1 || bytes_read > (ptrdiff_t) bytes_left) {
			*error = 400;
			return plain_text;
		}
		gstr_append_cstr(&plain_text, buffer, bytes_read);
		bytes_left -= bytes_read;
	}
	if (plain_text.len == 0) {
		*error = 400;
	}
	return plain_text;
}

Context *create_context(int client) {
	// TODO: check calloc failed
	Context *ctx = calloc(1, sizeof(Context));
	ctx->request = calloc(1, sizeof(Request));
	ctx->response = calloc(1, sizeof(Response));

	int error = 0;
	ctx->request->arena = get_raw_request(client, &error);
	if (error != 0) {
		ctx->status_code = error;
		return ctx;
	}
	// debug("%.*s", (int) ctx->request->arena.len, ctx->request->arena.ptr);

	ctx->status_code = parse_request(ctx->request);
	ctx->client = client;
	return ctx;
}

void *handle(void *arg) {
	ThreadInfo *tinfo = (ThreadInfo*) arg;
	int client = tinfo->client;
	Cerver *c = tinfo->c;

	Context *ctx = create_context(client);
	Slice method = ctx->request->method;
	Slice path = ctx->request->path;

	GString arena = {0};
	gstr_append_fmt_null(&arena, "%Sl:%Sl", method, path);

	Route *route = find_route(c->route, arena.ptr);
	int callback_res = 0;
	if (route != NULL) {
		callback_res = ((Callback) route->callback)(ctx);
	}
	else {
		arena.ptr[method.len] = '\0';
		route = find_route(c->route, arena.ptr);
		if (route != NULL) {
			callback_res = ((Callback) route->callback)(ctx);
		}
	}
	free(arena.ptr);

	if (callback_res == CERVER_RESPONSE) {
		send_response(ctx);
	}

	free_context(ctx);

	close(client);
	free(arg);

	return 0;
}

bool register_route(Cerver *c, const char *key, Callback callback) {
	if (c->route != NULL) {
		add_route(c->route, key, callback);
	}
	else {
		c->route = add_route(NULL, key, callback);
	}
	return true;
}

bool run(Cerver *c, int port) {
	struct sockaddr_in ser_addr = {
		.sin_family = AF_INET,
		.sin_addr = { htonl(INADDR_ANY) },
		.sin_port = htons(port)
	};

	c->server = socket(AF_INET, SOCK_STREAM, 0);
	if (c->server == -1) {
		return false;
	}

	int reuse = 1;
	if (setsockopt(c->server, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		return false;
	}

	if (bind(c->server, (struct sockaddr*) &ser_addr, sizeof(ser_addr)) == -1) {
		return false;
	}

	int connection_backlog = 10;
	if (listen(c->server, connection_backlog) == -1) {
		return false;
	}

	unsigned char *s_addr = (unsigned char*) &ser_addr.sin_addr.s_addr;
	debug("Server run at %d.%d.%d.%d:%d", *s_addr, s_addr[1], s_addr[2], s_addr[3], ser_addr.sin_port);
	while (1) {
		struct sockaddr_in cli_addr;
		unsigned int cli_addr_size = sizeof(cli_addr);
		int client = accept(c->server, (struct sockaddr*) &cli_addr, &cli_addr_size);
		if (c->server == -1 || client == -1) {
			/* printf("ERROR: could not accpet connection\n"); */
			break;
		}

		s_addr = (unsigned char*) &cli_addr.sin_addr.s_addr;
		debug("Connection: %d.%d.%d.%d:%d", *s_addr, s_addr[1], s_addr[2], s_addr[3], cli_addr.sin_port);

		ThreadInfo *tinfo = malloc(sizeof(ThreadInfo));
		tinfo->c = c;
		tinfo->client = client;

		pthread_t t;
		pthread_create(&t, NULL, handle, tinfo);
		pthread_detach(t);
	}

	// free_routes(c->route);

	return true;
}


