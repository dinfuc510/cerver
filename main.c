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
	KV *header;
	KV *query_param;
	KV *form_value;
	KV *multipart_form;
} HttpRequest;

void hmprint(KV *hm) {
	for (size_t i = 0; i < shlenu(hm); i++) {
		printf("%s:%s\n", hm[i].key, hm[i].value);
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

KV *parse_params(char *content, const char *pair_separate, const char *kv_separate) {
	KV *params = NULL;
	char *pair, *kv, *pair_saveptr, *kv_saveptr;
	for (pair = content;; pair = NULL) {
		char *token = strtok_r(pair, pair_separate, &pair_saveptr);
		if (token == NULL) {
			break;
		}

		char *subtoken[2] = {0};
		size_t sub_idx = 0;
		for (kv = token; sub_idx < 2; kv = NULL) {
			subtoken[sub_idx] = strtok_r(kv, kv_separate, &kv_saveptr);
			if (subtoken[sub_idx] == NULL) {
				break;
			}
			sub_idx++;
		}

		if (subtoken[0] != NULL && subtoken[1] != NULL) {
			shput(params, subtoken[0], subtoken[1]);
		}
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
	char *token, *saveptr = NULL;
	token = strtok_r(lines[0], " ", &saveptr);
	shput(*header, "method", token);
	token = strtok_r(NULL, " ", &saveptr);
	char *separate = strchr(token, '?');
	if (separate != NULL) {
		*separate = '\0';
		// TODO: parse anchor part
		// https://developer.mozilla.org/en-US/docs/Learn_web_development/Howto/Web_mechanics/What_is_a_URL#anchor
		shput(*header, "parameters", separate + 1);
	}
	shput(*header, "path", token);
	token = strtok_r(NULL, " ", &saveptr);
	shput(*header, "protocol", token);

	size_t i = 1;
	for (; i < arrlenu(lines) && *lines[i] != '\0'; i++, saveptr = NULL) {
		char *token = strtok_r(lines[i], ":", &saveptr);
		if (token == NULL) {
			continue;
		}

		tolowerstr(lines[i]);
		shput(*header, lines[i], saveptr + 1);
	}

	return i;
}

void *handle(void* arg) {
	int client = *(int*) arg;
	free(arg);

	char *plain_text = NULL;
	char **lines = get_raw_request(&plain_text, client);
	if (arrlen(plain_text) <= 1) {
		goto cleanup;
	}

	HttpRequest *req = calloc(1, sizeof(HttpRequest));
	shdefault(req->header, "");
	// shdefault(req->query_param, "");
	// shdefault(req->form_value, "");

	size_t i = parse_header(&req->header, lines);
	hmprint(req->header);

	const char *method = shget(req->header, "method");
	if (strcmp(method, "GET") == 0) {
		char *params = (char*) shget(req->header, "parameters");
		if (params != NULL) {
			req->query_param = parse_params(params, "&", "=");
			hmprint(req->query_param);
		}
	}
	else if (strcmp(method, "POST") == 0) {
		const char *content_type = shget(req->header, "content-type");
		if (content_type != NULL) {
			if (strncmp(content_type, "application/x-www-form-urlencoded", 33) == 0) {
				if (i + 1 < arrlenu(lines)) {
					req->form_value = parse_params(lines[++i], "&", "=");
					hmprint(req->form_value);
				}
			}
			if (strncmp(content_type, "application/json", 16) == 0) {
				if (i + 1 < arrlenu(lines)) {
					i++;
					for (; i < arrlenu(lines); i++) {
						printf("DEBUG: %s\n", lines[i]);
					}
				}
			}
			if (strncmp(content_type, "text/plain", 16) == 0) {
				if (i + 1 < arrlenu(lines)) {
					i++;
					for (; i < arrlenu(lines); i++) {
						printf("DEBUG: %s\n", lines[i]);
					}
				}
			}
			else if (strncmp(content_type, "multipart/form-data", 19) == 0) {
				RegexToken token[256];
				int16_t token_count = 256;
				if (0 != regex_parse("Content-Disposition: form-data; name=\"([^\"]+)\"(?:; file=\"([^\"]+)\")?", token, &token_count, 0)) {
					goto cleanup;
				}
				int64_t cap_pos[3];
				int64_t cap_span[3];
				memset(cap_pos, 0xFF, sizeof(cap_pos));
				memset(cap_span, 0xFF, sizeof(cap_span));

				char *content = NULL;
				while (i < arrlenu(lines)) {
					if (regex_match(token, lines[i], 0, 3, cap_pos, cap_span) > 0) {
						char *form_name = lines[i] + cap_pos[1], *file_name = NULL;
						form_name[cap_span[1]] = '\0';
						if (cap_pos[2] >= 0) {
							file_name = lines[i] + cap_pos[2];
							file_name[cap_span[2]] = '\0';
						}

						while (i < arrlenu(lines) && *lines[i] != '\0') {
							i++;
						}
						i++;
						while (i + 1 < arrlenu(lines) && strncmp(lines[i + 1], "Content-Disposition", 19) != 0) {
							size_t line_len = strlen(lines[i]);
							stbds_arrmaybegrow(content, line_len + 1);
							memcpy(content + arrlenu(content), lines[i], line_len);
							arrsetlen(content, arrlen(content) + line_len);
							arrput(content, '\n');
							i++;
						}
						(void) arrpop(content);
						arrput(content, '\0');
						if (file_name != NULL) {
							shput(req->multipart_form, file_name, content);
						}
						else {
							shput(req->form_value, form_name, content);
						}
						arrsetlen(content, 0);
					}
					i++;
				}
				arrfree(content);

				hmprint(req->form_value);
				hmprint(req->multipart_form);
			}
		}
	}

cleanup:
	shfree(req->header);
	shfree(req->query_param);
	shfree(req->form_value);
	free(req);

	arrfree(plain_text);
	arrfree(lines);

	char msg[] = "HTTP/1.1 200 Ok\r\nContent-Length: 5\r\n\r\nHello";
	send(client, msg, sizeof(msg), 0);
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
