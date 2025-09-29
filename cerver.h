#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <pthread.h>

#include "strfmt.h"
#include "parser.h"
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

typedef struct {
	const char *key;
	int (*callback)(Context*);
} Route;

typedef struct {
	int server;
	Route *route;
} Cerver;

typedef struct {
	Cerver *c;
	int client;
} ThreadInfo;

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

void *handle(void *arg) {
	ThreadInfo *tinfo = (ThreadInfo*) arg;
	int client = tinfo->client;
	Cerver *c = tinfo->c;

	Context *ctx = parse_request(client);
	const char *method = shget(ctx->request_header, "method");
	const char *path = shget(ctx->request_header, "path");

	char *arena = NULL;
	strputfmtn(&arena, "%s:%s", method, path);

	Route *route = shgetp_null(c->route, arena);
	if (route != NULL) {
		(void) route->callback(ctx);
	}
	else {
		strsetlen(&arena, strlen(method));
		arrput(arena, '\0');

		route = shgetp_null(c->route, arena);
		if (route != NULL) {
			(void) route->callback(ctx);
		}
	}

	strsetlen(&arena, 0);
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
		ssize_t sent = send(client, arena, bytes_left, 0);
		if (sent == -1) {
			debug("Only sent %ld bytes because of the error", arrlenu(arena) - bytes_left);
			break;
		}
		bytes_left -= sent;
	}

	arrfree(arena);
	arrfree(ctx->response_header);
	arrfree(ctx->response_body);
	shfree(ctx->request_header);
	shfree(ctx->query_param);

	for (size_t key_idx = 0; key_idx < shlenu(ctx->form_value); key_idx++) {
		free((char*) ctx->form_value[key_idx].key);
		arrfree(ctx->form_value[key_idx].value);
	}
	shfree(ctx->form_value);

	for (size_t key_idx = 0; key_idx < shlenu(ctx->multipart_form); key_idx++) {
		MultipartForm mtform = ctx->multipart_form[key_idx];
		for (size_t file_idx = 0; file_idx < arrlenu(mtform.file_name); file_idx++) {
			free(mtform.file_name[file_idx]);
		}
		for (size_t content_idx = 0; content_idx < arrlenu(mtform.content); content_idx++) {
			arrfree(mtform.content[content_idx]);
		}
		arrfree(mtform.file_name);
		arrfree(mtform.content);
	}
	shfree(ctx->multipart_form);
	arrfree(ctx->arena);
	free(ctx);

	close(client);
	free(arg);

	return 0;
}

bool register_route(Cerver *c, const char *key, int (*callback)(Context*)) {
	Route route = {
		.key = key,
		.callback = callback,
	};

	shputs(c->route, route);
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

	int connection_backlog = 1;
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

		pthread_t t;
		ThreadInfo *tinfo = malloc(sizeof(ThreadInfo));
		tinfo->c = c;
		tinfo->client = client;

		pthread_create(&t, NULL, handle, tinfo);
		pthread_detach(t);
	}

	return true;
}
