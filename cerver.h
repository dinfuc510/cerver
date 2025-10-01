#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#include "cer_ds.h"
#include "response.h"
#include "request.h"
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

char *get_raw_request(int client) {
	char buffer[1024];
	char *plain_text = NULL;
	ssize_t bytes_read = 0;
	do {
		bytes_read = read(client, buffer, sizeof(buffer));
		if (bytes_read == 0 || bytes_read == -1) {
			break;
		}
		strputstr(&plain_text, buffer, bytes_read);
	} while (bytes_read == sizeof(buffer));
	if (plain_text == NULL) {
		return NULL;
	}
	// debug("%ld", arrlenu(plain_text));
	return plain_text;
}

Context *create_context(int client) {
	Context *ctx = calloc(1, sizeof(Context));
	char *plain_text = get_raw_request(client);
	if (plain_text == NULL) {
		return NULL;
	}
	// debug("%.*s", (int) arrlenu(plain_text), plain_text);

	ctx->request = parse_request(&plain_text, arrlenu(plain_text));
	if (ctx->request == NULL) {
		return NULL;
	}
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

	char *arena = NULL;
	strputfmtn(&arena, "%Ls:%Ls", method, path);

	Route route = shgets(c->route, arena);
	int callback_res = 0;
	if (route.callback != NULL) {
		callback_res = route.callback(ctx);
	}
	else {
		arena[method.len] = '\0';
		route = shgets(c->route, arena);
		if (route.callback != NULL) {
			callback_res = route.callback(ctx);
		}
	}
	arrfree(arena);

	if (callback_res == CERVER_RESPONSE) {
		send_response(ctx);
	}

	free_context(ctx);

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


