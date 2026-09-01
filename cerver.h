#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef unix
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
	#include <poll.h>
	#include <pthread.h>
#elif defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <winsock2.h>
#else
	_Static_assert(0 && "Unsupport platform");
#endif

#include "cer_ds.h"
#include "response.h"
#include "request.h"

#define POLLTIMEOUT (-1)
#ifndef POLLIN
	#define POLLIN (1)
#endif

typedef struct {
	Cerver *c;
	int client;
#ifdef _WIN32
	HANDLE iocp;
#endif
} ThreadInfo;

#ifdef unix
int get_socket_status(int fd) {
	struct pollfd pfd = {
		.fd = fd,
		.events = POLLIN,
	};
	int timeout = 2500;

#ifdef unix
	int nevents = poll(&pfd, 1, timeout);
#elif defined(_WIN32)
	int nevents = WSAPoll(&pfd, 1, timeout);
#endif
	if (nevents == 0) {
		return POLLTIMEOUT;
	}

	return pfd.revents;
}

GString get_raw_request(int client, int *error) {
	char buffer[4096];
	GString plain_text = {0};

	int status = get_socket_status(client);

	if (status == POLLTIMEOUT || !(status & POLLIN)) {
		*error = status == POLLTIMEOUT ? 408 : 400;
		return plain_text;
	}

	ssize_t bytes_read = recv(client, buffer, sizeof(buffer), 0);
	if (bytes_read <= 0) {
		*error = 400;
		return plain_text;
	}

	char *crlf_crlf = strstr(buffer, "\r\n\r\n");
	if (crlf_crlf == NULL) {
		*error = bytes_read == sizeof(buffer) ? 431 : 400;
		return plain_text;
	}
	size_t content_length = 0;
	if (!parse_content_length_header(buffer, bytes_read, &content_length)) {
		*error = 400;
		return plain_text;
	}

	size_t bytes_left = 0;	// maximum size of the request
	if (content_length > 0) {
		bytes_left = (crlf_crlf - buffer) + strlen("\r\n\r\n") + content_length - bytes_read;
	}
	gstr_append_cstr(&plain_text, buffer, bytes_read);

	while (bytes_left > 0 || bytes_read == sizeof(buffer)) {
		status = get_socket_status(client);
		if (status == POLLTIMEOUT || !(status & POLLIN)) {
			*error = status == POLLTIMEOUT ? 408 : 400;
			break;
		}
		bytes_read = recv(client, buffer, sizeof(buffer), 0);
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

#elif _WIN32
bool get_wsabuf(int fd, HANDLE iocp, WSABUF *wsabuf, DWORD *bytes_read) {
	int timeout = 2500;
	OVERLAPPED ov = {0};
	DWORD flags = 0;
	ULONG_PTR key = 0;

	if (WSARecv(fd, wsabuf, 1, NULL, &flags, &ov, NULL) == SOCKET_ERROR) {
		return POLLERR;
	}

	OVERLAPPED *completion_ov = NULL;
	return GetQueuedCompletionStatus(iocp, bytes_read, &key, &completion_ov, timeout);
}

GString get_raw_request(int fd, HANDLE iocp, int *error) {
	char buffer[4096];
	WSABUF wsabuf = {
		.buf = buffer,
		.len = 4096,
	};
	GString plain_text = {0};

	if (CreateIoCompletionPort((HANDLE) fd, iocp, (ULONG_PTR) fd, 0) == NULL) {
		*error = 400;
		return plain_text;
	}

	DWORD bytes_read = 0;
	if (!get_wsabuf(fd, iocp, &wsabuf, &bytes_read)) {
		*error = GetLastError() == WAIT_TIMEOUT ? 408 : 400;
		return plain_text;
	}

	char *crlf_crlf = strstr(buffer, "\r\n\r\n");
	if (crlf_crlf == NULL) {
		*error = bytes_read == sizeof(buffer) ? 431 : 400;
		return plain_text;
	}

	size_t content_length = 0;
	if (!parse_content_length_header(buffer, bytes_read, &content_length)) {
		*error = 400;
		return plain_text;
	}

	size_t bytes_left = 0;	// maximum size of the request
	if (content_length > 0) {
		bytes_left = (crlf_crlf - buffer) + strlen("\r\n\r\n") + content_length - bytes_read;
	}
	gstr_append_cstr(&plain_text, buffer, bytes_read);

	while (bytes_left > 0 || bytes_read == sizeof(buffer)) {
		if (!get_wsabuf(fd, iocp, &wsabuf, &bytes_read)) {
			*error = GetLastError() == WAIT_TIMEOUT ? 408 : 400;
			return plain_text;
		}

		if (bytes_read > bytes_left) {
			*error = 400;
			return plain_text;
		}

		gstr_append_cstr(&plain_text, buffer, bytes_read);
		bytes_left -= bytes_read;
	}
	if (plain_text.len == 0) {
		trace_log;
		*error = 400;
	}
	return plain_text;
}
#endif

Context *create_context(int client
#ifdef _WIN32
	, HANDLE iocp
#endif
	) {
	// TODO: check calloc failed
	Context *ctx = calloc(1, sizeof(Context));
	ctx->request = calloc(1, sizeof(Request));
	ctx->response = calloc(1, sizeof(Response));

	ctx->client = client;
#ifdef unix
	ctx->request->arena = get_raw_request(client, &ctx->status_code);
#elif defined(_WIN32)
	ctx->request->arena = get_raw_request(client, iocp, &ctx->status_code);
#endif
	int status_code = parse_request(ctx->request);
	if (ctx->status_code == 0 && status_code != 0) {
		ctx->status_code = status_code;
	}
	// TODO: handle if ctx->request->arena is empty

	return ctx;
}

void *handle(void *arg) {
	ThreadInfo *tinfo = (ThreadInfo*) arg;
	int client = tinfo->client;
	Cerver *c = tinfo->c;

#ifdef unix
	Context *ctx = create_context(client);
#elif defined(_WIN32)
	Context *ctx = create_context(client, tinfo->iocp);
#endif
	RouteNode *route = NULL;

	Slice method = ctx->request->method;
	Slice path = ctx->request->path;
	GString arena = {0};
	gstr_append_fmt_null(&arena, "%Sl:%Sl", method, path);

	if (ctx->status_code == 0) {
		route = find_dynamic_route(c->route, arena.ptr, &ctx->request->path_parameters);
	}
	if (route != NULL && route->callback != NULL) {
		((Callback) route->callback)(ctx);
	}
	else {
		arena.ptr[method.len] = '\0';
		route = find_route(c->route, arena.ptr);
		if (route != NULL && route->callback != NULL) {
			((Callback) route->callback)(ctx);
		}
		else {
			// TODO: handle this case
			trace_log;
		}
	}
	gstr_free(&arena);

	if (!send_response(ctx)) {
		debug("%s", "Failed to response: Broken pipe");
	}

	free_context(ctx);

#ifdef unix
	close(client);
#elif defined (_WIN32)
	closesocket(client);
#endif

	free(arg);

	return 0;
}

#define get(c, route, callback) register_route((c), "GET:"route, callback)
#define post(c, route, callback) register_route((c), "POST:"route, callback)
bool register_route(Cerver *c, const char *key, Callback callback) {
	if (callback == NULL) {
		return false;
	}

	RouteNode *route = add_route(c->route, key, callback);
	if (route == NULL) {
		return false;
	}
	if (c->route == NULL) {
		c->route = route;
	}
	return true;
}

bool run(Cerver *c, int port) {
	bool success = true;
#ifdef _WIN32
    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d)) {
		return false;
    }

	HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (iocp == NULL) {
		success = false;
		goto cleanup;
	}
#endif

	struct sockaddr_in ser_addr = {
		.sin_family = AF_INET,
		.sin_addr = { htonl(INADDR_ANY) },
		.sin_port = htons(port)
	};

	c->server = socket(AF_INET, SOCK_STREAM, 0);
	if (c->server == -1) {
		success = false;
		goto cleanup;
	}

#ifndef _WIN32
	int reuse = 1;
	if (setsockopt(c->server, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		return false;
	}
#endif

	if (bind(c->server, (struct sockaddr*) &ser_addr, sizeof(ser_addr)) == -1) {
		success = false;
		goto cleanup;
	}

	int connection_backlog = 10;
	if (listen(c->server, connection_backlog) == -1) {
		success = false;
		goto cleanup;
	}

	unsigned char *saddr = (unsigned char*) &ser_addr.sin_addr.s_addr;
	debug("Server run at %d.%d.%d.%d:%d", saddr[0], saddr[1], saddr[2], saddr[3], ser_addr.sin_port);
	while (1) {
		struct sockaddr_in cli_addr;
		unsigned int cli_addr_size = sizeof(cli_addr);
		int client = accept(c->server, (struct sockaddr*) &cli_addr, &cli_addr_size);
		if (c->server == -1 || client == -1) {
			/* printf("ERROR: could not accpet connection\n"); */
			break;
		}

		saddr = (unsigned char*) &cli_addr.sin_addr.s_addr;
		debug("Connection: %d.%d.%d.%d:%d", saddr[0], saddr[1], saddr[2], saddr[3], cli_addr.sin_port);

		ThreadInfo *tinfo = malloc(sizeof(ThreadInfo));
		tinfo->c = c;
		tinfo->client = client;

#ifdef unix
		pthread_t t;
		pthread_create(&t, NULL, handle, tinfo);
		pthread_detach(t);
#elif defined(_WIN32)
		tinfo->iocp = iocp;
		HANDLE t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle, tinfo, 0, NULL);
		CloseHandle(t);
#endif
	}

cleanup:
#ifdef _WIN32
	if (iocp != NULL) {
		CloseHandle(iocp);
	}
	WSACleanup();
#endif
	return success;
}


