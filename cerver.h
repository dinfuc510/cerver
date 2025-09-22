#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"
#include "remimu.h"

#ifndef CERVER_DEBUG
	#define CERVER_DEBUG 1
#endif
#define debug(fmt, ...) do {		\
		if (CERVER_DEBUG) {			\
			printf("%s:%d "fmt"\n", __FILE__, __LINE__, __VA_ARGS__);		\
		}							\
	} while(0)

typedef struct {
	const char *key;
   	const char *value;
} KV;

typedef struct {
	const char *key;
	char **file_name;
   	const char **content;
} MultipartForm;

typedef struct {
	KV *header;
	KV *query_param;
	KV *form_value;
	MultipartForm *multipart_form;

	int status_code;
	char *response_header; // TODO: consider using hashmap
	char *response_body;
} Context;

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
		case 200: {
			return strputstr(s, "HTTP/1.1 200 OK\r\n", 17);
		}
		case 301: {
			return strputstr(s, "HTTP/1.1 301 Moved Permanently\r\n", 32);
		}
		case 404: {
			return strputstr(s, "HTTP/1.1 404 Not Found\r\n", 24);
		}
	}

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

char **get_raw_request(char **plain_text, int client) {
	char buffer[1024];
	size_t bytes_read = 0;
	do {
		bytes_read = read(client, buffer, sizeof(buffer) - 1);
		if (bytes_read == 0) {
			break;
		}
		// printf("RAW: %.*s\n", bytes_read, buffer);
		strputstr(plain_text, buffer, bytes_read);
	} while (bytes_read >= sizeof(buffer) - 1);
	arrput(*plain_text, '\0');

	char **lines = NULL;
	char *token, *saveptr = NULL, *iter;
	for (iter = *plain_text; (token = strtok_r(iter, "\n", &saveptr)) != NULL; iter = NULL) {
		if (token[saveptr - token - 2] == '\r') {
			token[saveptr - token - 2] = '\0';
		}
		arrput(lines, token);
	}

	return lines;
}

KV *parse_pairs(char *content) {
	RegexToken token[64];
	int16_t token_size = sizeof(token)/sizeof(*token);
	char pattern[] = "([^=&]+)(?:=([^=&]*))?";

	if (regex_parse(pattern, token, &token_size, 0) != 0) {
		return NULL;
	}

	KV *params = NULL;
	int64_t cap_pos[3];
	int64_t cap_span[3];
	memset(cap_pos, 0xFF, sizeof(cap_pos));
	memset(cap_span, 0xFF, sizeof(cap_span));

	size_t text_len = strlen(content);
	int64_t matchlen;
	size_t offset = 0;
	while (offset < text_len && (matchlen = regex_match(token, content, 0, 3, cap_pos, cap_span)) > 0) {
		if (cap_pos[1] < 0) {
			break;
		}
		for (int i = 1; i < 3; i++) {
			if (cap_pos[i] >= 0) {
				content[cap_pos[i] + cap_span[i]] = '\0';
			}
		}
		shput(params, content + cap_pos[1], cap_pos[2] < 0 ? "" : content + cap_pos[2]);
		content += cap_span[0] + 1;
		offset += cap_span[0] + 1;
	}

	return params;
}

size_t parse_header(KV **header, char **lines) {
	RegexToken token[64];
	int16_t token_size = sizeof(token)/sizeof(*token);
	if (regex_parse("(GET|POST) (\/[^ #]*(?:\#[a-zA-Z0-9\/]*)?) (HTTP.*)", token, &token_size, 0) != 0) {
		printf("HERE\n");
		return 0;
	}

	int64_t cap_pos[4];
	int64_t cap_span[4];
	memset(cap_pos, 0xFF, sizeof(cap_pos));
	memset(cap_span, 0xFF, sizeof(cap_span));
	if (regex_match(token, lines[0], 0, 4, cap_pos, cap_span) == 0) {
		return 0;
	}
	for (int cap_idx = 1; cap_idx < 4; cap_idx++) {
		if (cap_pos[cap_idx] < 0) {
			return 0;
		}
		lines[0][cap_pos[cap_idx] + cap_span[cap_idx]] = '\0';
	}

	shput(*header, "method", lines[0] + cap_pos[1]);
	char *separate = strchr(lines[0] + cap_pos[2], '?');
	if (separate != NULL) {
		*separate = '\0';
		// TODO: parse anchor part
		// https://developer.mozilla.org/en-US/docs/Learn_web_development/Howto/Web_mechanics/What_is_a_URL#anchor
		shput(*header, "parameters", separate + 1);
	}
	shput(*header, "path", lines[0] + cap_pos[2]);
	shput(*header, "protocol", lines[0] + cap_pos[3]);

	token_size = sizeof(token)/sizeof(*token);
	if (regex_parse("([^:]+): ([^\n]+)", token, &token_size, 0) != 0) {
		printf("HERE4\n");
		return 0;
	}
	size_t i = 1;
	for (; i < arrlenu(lines); i++) {
		if (regex_match(token, lines[i], 0, 3, cap_pos, cap_span) == 0) {
			break;
		}
		for (int cap_idx = 1; cap_idx < 3; cap_idx++) {
			if (cap_pos[cap_idx] < 0) {
				return i;
			}
			lines[i][cap_pos[cap_idx] + cap_span[cap_idx]] = '\0';
		}
		tolowerstr(lines[i] + cap_pos[1]);
		shput(*header, lines[i] + cap_pos[1], lines[i] + cap_pos[2]);
	}

	return i;
}

Context *parse_request(int client, char **parena) {
	Context *ctx = calloc(1, sizeof(Context));
	char *arena = NULL;
	char **lines = get_raw_request(&arena, client);
	if (arrlen(arena) <= 1) {
		goto _return;
	}
	strsetlen(&arena, 0);

	shdefault(ctx->header, "");

	size_t i = parse_header(&ctx->header, lines);

	const char *method = shget(ctx->header, "method");
	if (strcmp(method, "GET") == 0) {
		char *params = (char*) shget(ctx->header, "parameters");
		if (params != NULL) {
			ctx->query_param = parse_pairs(params);
		}
	}
	else if (strcmp(method, "POST") == 0) {
		if (++i >= arrlenu(lines)) {
			goto _return;
		}

		const char *content_type = shget(ctx->header, "content-type");
		if (*content_type == '\0' || strncmp(content_type, "application/x-www-form-urlencoded", 33) == 0) {
			ctx->form_value = parse_pairs(lines[i]);
		}
		else if (strncmp(content_type, "application/json", 16) == 0) {
			for (; i < arrlenu(lines); i++) {
				debug("%s", lines[i]);
			}
		}
		else if (strncmp(content_type, "multipart/form-data", 19) == 0) {
			sh_new_arena(ctx->multipart_form);

			RegexToken token[256];
			int16_t token_count = 256;
			if (0 != regex_parse("Content-Disposition: [^;]+; name=\"([^\"]+)\"(?:; filename=\"([^\"]+)\")?", token, &token_count, 0)) {
				goto _return;
			}
			int64_t cap_pos[3];
			int64_t cap_span[3];
			memset(cap_pos, 0xFF, sizeof(cap_pos));
			memset(cap_span, 0xFF, sizeof(cap_span));

			char *form_name = NULL;
			while (i < arrlenu(lines)) {
				if (regex_match(token, lines[i], 0, 3, cap_pos, cap_span) > 0) {
					strsetlen(&form_name, 0);
					strputstr(&form_name, lines[i] + cap_pos[1], cap_span[1]);
					arrput(form_name, '\0');

					char *file_name = NULL;
					if (cap_pos[2] >= 0) {
						file_name = strndup(lines[i] + cap_pos[2], cap_span[2]);
					}

					while (i < arrlenu(lines) && *lines[i] != '\0') {
						i++;
					}
					if (++i >= arrlenu(lines)) {
						break;
					}
					size_t old_plain_text_len = arrlen(arena);
					while (i + 1 < arrlenu(lines) && strncmp(lines[i + 1], "Content-Disposition", 19) != 0) {
						strputstr(&arena, lines[i], 0);
						arrput(arena, '\n');
						i++;
					}
					(void) arrpop(arena);
					arrput(arena, '\0');
					if (file_name != NULL) {
						MultipartForm *form = shgetp(ctx->multipart_form, form_name);
						if (form->key == NULL) {
							shputs(ctx->multipart_form, (MultipartForm) { .key = form_name });
							form = shgetp(ctx->multipart_form, form_name);
						}
						arrput(form->file_name, file_name);
						arrput(form->content, arena + old_plain_text_len);
					}
					else {
						shput(ctx->form_value, form_name, arena + old_plain_text_len);
					}
				}
				i++;
			}
			arrfree(form_name);

		}
		else {
			debug("%s", content_type);
			for (; i < arrlenu(lines); i++) {
				debug("%s", lines[i]);
			}
		}
	}

_return:
	arrfree(lines);
	*parena = arena;
	return ctx;
}

void html(Context *ctx, int status_code, const char *fmt, ...) {
	ctx->status_code = status_code;
	va_list arg;
	va_start(arg, fmt);

	strsetlen(&ctx->response_body, 0);
	vstrputfmt(&ctx->response_body, fmt, arg);

	va_end(arg);
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

	char *hm_arena = NULL;
	Context *ctx = parse_request(client, &hm_arena);
	const char *method = shget(ctx->header, "method");
	const char *path = shget(ctx->header, "path");

	char *arena = NULL;
	strputfmt(&arena, "%s:%s%0", method, path);

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

	strputfmt(&ctx->response_body, "\r\n");
	if (ctx->response_header != NULL) {
		strputfmt(&arena, "%S\r\n", ctx->response_header);
	}
	strputfmt(&arena, "Content-Length: %ld\r\n\r\n%S", arrlenu(ctx->response_body), ctx->response_body);

	size_t bytes_left = arrlenu(arena);
	while (bytes_left > 0) {
		ssize_t sent = send(client, arena, bytes_left, 0);
		if (sent == -1) {
			debug("Only sent %ld bytes because of the error", arrlenu(arena) - bytes_left);
		}
		bytes_left -= sent;
	}

	arrfree(arena);
	arrfree(ctx->response_header);
	arrfree(ctx->response_body);
	shfree(ctx->header);
	shfree(ctx->query_param);
	shfree(ctx->form_value);
	for (size_t key_idx = 0; key_idx < shlenu(ctx->multipart_form); key_idx++) {
		MultipartForm mtform = ctx->multipart_form[key_idx];
		for (size_t file_idx = 0; file_idx < arrlenu(mtform.file_name); file_idx++) {
			free(mtform.file_name[file_idx]);
		}
		arrfree(mtform.file_name);
		arrfree(mtform.content);
	}
	shfree(ctx->multipart_form);
	free(ctx);
	arrfree(hm_arena);

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
	printf("Server run at %d.%d.%d.%d:%d\n", *s_addr, s_addr[1], s_addr[2], s_addr[3], ser_addr.sin_port);
	while (1) {
		struct sockaddr_in cli_addr;
		unsigned int cli_addr_size = sizeof(cli_addr);
		int client = accept(c->server, (struct sockaddr*) &cli_addr, &cli_addr_size);
		if (c->server == -1 || client == -1) {
			/* printf("ERROR: could not accpet connection\n"); */
			break;
		}

		s_addr = (unsigned char*) &cli_addr.sin_addr.s_addr;
		printf("Conncetion: %d.%d.%d.%d:%d\n", *s_addr, s_addr[1], s_addr[2], s_addr[3], cli_addr.sin_port);

		pthread_t t;
		ThreadInfo *tinfo = malloc(sizeof(ThreadInfo));
		tinfo->c = c;
		tinfo->client = client;

		pthread_create(&t, NULL, handle, tinfo);
		pthread_detach(t);
	}

	return true;
}
