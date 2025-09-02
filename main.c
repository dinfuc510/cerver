#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"
#include "remimu.h"

#define PORT 12345
#define err(msg) do {		\
		perror(msg);		\
		exit(EXIT_FAILURE);	\
	} while(0)

typedef struct {
	const char *key;
   	const char *value;
} KV;

typedef struct {
	const char *key;
	const char **file_name;
   	const char **content;
} MultipartForm;

typedef struct {
	KV *header;
	KV *query_param;
	KV *form_value;
	MultipartForm *multipart_form;
} HttpRequest;

void hmprint(KV *hm) {
	for (size_t i = 0; i < shlenu(hm); i++) {
		printf("%s:%s\n", hm[i].key, hm[i].value);
	}
}

void mpf_print(MultipartForm *form) {
	for (size_t i = 0; i < shlenu(form); i++) {
		printf("[%s]:\n", form[i].key);
		for (size_t file_idx = 0; file_idx < arrlenu(form[i].file_name); file_idx++) {
			printf("%s:%s\n", form[i].file_name[file_idx], form[i].content[file_idx]);
		}
	}
}

char **get_raw_request(char **plain_text, int client) {
	char buffer[64];
	size_t bytes_read = 0;
	do {
		bytes_read = read(client, buffer, sizeof(buffer) - 1);
		if (bytes_read == 0) {
			break;
		}
		// printf("RAW: %.*s\n", bytes_read, buffer);
		stbds_arrmaybegrow(*plain_text, bytes_read);
		memmove(*plain_text + arrlen(*plain_text), buffer, bytes_read);
		arrsetlen(*plain_text, arrlen(*plain_text) + bytes_read);
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

KV *parse_params(char *content, char pair_separate, char kv_separate) {
	RegexToken token[64];
	int16_t token_size = 64;
	char pattern[] = "([^=&]+)(?:=([^=&]*))?";
	pattern[3] = pattern[15] = pair_separate;
	pattern[4] = pattern[16] = kv_separate; // fixme: hardcode

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

void tolowerstr(char *s) {
	while (*s != '\0') {
		if (*s >= 'A' && *s <= 'Z') {
			*s += ('a' - 'A');
		}
		s++;
	}
}

size_t parse_header(KV **header, char **lines) {
	RegexToken token[64];
	int16_t token_size = 64;
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

	token_size = 64;
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

HttpRequest *parse_request(int client, char **parena) {
	HttpRequest *req = calloc(1, sizeof(HttpRequest));
	char *arena = NULL;
	char **lines = get_raw_request(&arena, client);
	if (arrlen(arena) <= 1) {
		goto _return;
	}
	arrsetlen(arena, 0);

	shdefault(req->header, "");

	size_t i = parse_header(&req->header, lines);
	// hmprint(req->header);

	const char *method = shget(req->header, "method");
	if (strcmp(method, "GET") == 0) {
		char *params = (char*) shget(req->header, "parameters");
		if (params != NULL) {
			req->query_param = parse_params(params, '&', '=');
			// hmprint(req->query_param);
		}
	}
	else if (strcmp(method, "POST") == 0) {
		if (++i >= arrlenu(lines)) {
			goto _return;
		}

		const char *content_type = shget(req->header, "content-type");
		if (*content_type == '\0' || strncmp(content_type, "application/x-www-form-urlencoded", 33) == 0) {
			req->form_value = parse_params(lines[i], '&', '=');
			// hmprint(req->form_value);
		}
		if (strncmp(content_type, "application/json", 16) == 0) {
			for (; i < arrlenu(lines); i++) {
				printf("DEBUG: %s\n", lines[i]);
			}
		}
		else if (strncmp(content_type, "multipart/form-data", 19) == 0) {
			RegexToken token[256];
			int16_t token_count = 256;
			if (0 != regex_parse("Content-Disposition: [^;]+; name=\"([^\"]+)\"(?:; filename=\"([^\"]+)\")?", token, &token_count, 0)) {
				goto _return;
			}
			int64_t cap_pos[3];
			int64_t cap_span[3];
			memset(cap_pos, 0xFF, sizeof(cap_pos));
			memset(cap_span, 0xFF, sizeof(cap_span));

			while (i < arrlenu(lines)) {
				if (regex_match(token, lines[i], 0, 3, cap_pos, cap_span) > 0) {
					printf("DEBUG: %s\n", lines[i]);
					char *form_name = lines[i] + cap_pos[1], *file_name = NULL;
					form_name[cap_span[1]] = '\0';
					if (cap_pos[2] >= 0) {
						file_name = lines[i] + cap_pos[2];
						file_name[cap_span[2]] = '\0';
					}

					while (i < arrlenu(lines) && *lines[i] != '\0') {
						i++;
					}
					if (++i >= arrlenu(lines)) {
						break;
					}
					size_t old_plain_text_len = arrlen(arena);
					while (i + 1 < arrlenu(lines) && strncmp(lines[i + 1], "Content-Disposition", 19) != 0) {
						size_t line_len = strlen(lines[i]);
						stbds_arrmaybegrow(arena, line_len + 1);
						memcpy(arena + arrlenu(arena), lines[i], line_len);
						arrsetlen(arena, arrlen(arena) + line_len);
						arrput(arena, '\n');
						i++;
					}
					(void) arrpop(arena);
					arrput(arena, '\0');
					if (file_name != NULL) {
						MultipartForm *form = shgetp(req->multipart_form, form_name);
						if (form->key == NULL) {
							shputs(req->multipart_form, (MultipartForm) { .key = form_name });
							form = shgetp(req->multipart_form, form_name);
						}
						arrput(form->file_name, file_name);
						arrput(form->content, arena + old_plain_text_len);
					}
					else {
						shput(req->form_value, form_name, arena + old_plain_text_len);
					}
				}
				i++;
			}

			// hmprint(req->form_value);
			// mpf_print(req->multipart_form);
		}
		else {
			for (; i < arrlenu(lines); i++) {
				printf("DEBUG: %s\n", lines[i]);
			}
		}
	}

_return:
	arrfree(lines);
	*parena = arena;
	return req;
}

void arrputstr(char **arr, char *str) {
	size_t len = strlen(str);
	stbds_arrmaybegrow(*arr, len);
	memcpy(*arr + arrlenu(*arr), str, len);
	arrsetlen(*arr, arrlenu(*arr) + len);
}

void *handle(void *arg) {
	int client = *(int*) arg;
	free(arg);

	char *arena = NULL;
	HttpRequest *req = parse_request(client, &arena);
	char *res = NULL, *content = NULL;

	const char *method = shget(req->header, "method");
	const char *path = shget(req->header, "path");
	if (strcmp(path, "/hello") == 0 && strcmp(method, "GET") == 0) {
		arrputstr(&res, "HTTP/1.1 200 Ok\r\n");
		char *name = (char*) shget(req->query_param, "name");
		arrputstr(&content, "Hello ");
		arrputstr(&content, name != NULL ? name : "");
	}
	else {
		arrputstr(&res, "HTTP/1.1 414 Not found\r\n");
		arrputstr(&content, "Not found");
	}
	arrputstr(&content, "\r\n");
	arrput(content, 0);

	char content_length[64];
    snprintf(content_length, sizeof(content_length) - 1, "Content-Length: %ld\r\n\r\n", content != NULL ? arrlenu(content) - 1 : 0);
	arrputstr(&res, content_length);
	arrputstr(&res, content);

	printf("%ld %.*s\n", arrlenu(res), arrlenu(res), res);
	send(client, res, arrlenu(res), 0);

	arrfree(res);
	arrfree(content);

	shfree(req->header);
	shfree(req->query_param);
	shfree(req->form_value);
	for (size_t key_idx = 0; key_idx < shlenu(req->multipart_form); key_idx++) {
		arrfree(req->multipart_form[key_idx].file_name);
		arrfree(req->multipart_form[key_idx].content);
	}
	shfree(req->multipart_form);
	free(req);
	arrfree(arena);

	close(client);
	return 0;
}

volatile int server = -1;
void cleanup(int code) {
	(void) code;
	if (close(server) == 0) {
		server = -1;
		printf("Shutdown server\n");
	}
}

int main(void) {
	signal(SIGINT, cleanup);
	setbuf(stdout, NULL);

	struct sockaddr_in ser_addr = {
		.sin_family = AF_INET,
		.sin_addr = { htonl(INADDR_ANY) },
		.sin_port = htons(PORT)
	};

	server = socket(AF_INET, SOCK_STREAM, 0);
	if (server == -1) {
		err("socket");
	}

	int reuse = 1;
	if (setsockopt(server, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		err("reuse");
	}

	if (bind(server, (struct sockaddr*) &ser_addr, sizeof(ser_addr)) == -1) {
		err("bind");
	}

	int connection_backlog = 1;
	if (listen(server, connection_backlog) == -1) {
		err("listen");
	}

	unsigned char *s_addr = (unsigned char*) &ser_addr.sin_addr.s_addr;
	printf("Server run at %d.%d.%d.%d:%d\n", *s_addr, s_addr[1], s_addr[2], s_addr[3], ser_addr.sin_port);
	while (1) {
		struct sockaddr_in cli_addr;
		unsigned int cli_addr_size = sizeof(cli_addr);
		int client = accept(server, (struct sockaddr*) &cli_addr, &cli_addr_size);
		if (server == -1 || client == -1) {
			/* printf("ERROR: could not accpet connection\n"); */
			break;
		}

		s_addr = (unsigned char*) &cli_addr.sin_addr.s_addr;
		printf("Conncetion: %d.%d.%d.%d:%d\n", *s_addr, s_addr[1], s_addr[2], s_addr[3], cli_addr.sin_port);

		pthread_t t;
		int *client_copy = (int*) malloc(sizeof(int));
		*client_copy = client;
		pthread_create(&t, NULL, handle, client_copy);
		pthread_detach(t);
	}

	return 0;
}
