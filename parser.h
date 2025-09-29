#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

#ifndef CERVER_DEBUG
	#define CERVER_DEBUG 1
#endif
#define debug(fmt, ...) do {		\
		if (CERVER_DEBUG) {			\
			printf("[%s:%d] "fmt"\n", __FILE__, __LINE__, __VA_ARGS__);		\
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
	KV *request_header;
	KV *query_param;
	KV *form_value;
	MultipartForm *multipart_form;
	char *arena;

	int status_code;
	char *response_header; // TODO: consider using hashmap
	char *response_body;
} Context;

char **get_raw_request(char **plain_text, int client) {
	char buffer[1024];
	size_t bytes_read = 0;
	do {
		bytes_read = read(client, buffer, sizeof(buffer) - 1);
		if (bytes_read == 0) {
			break;
		}
		strputstr(plain_text, buffer, bytes_read);
	} while (bytes_read >= sizeof(buffer) - 1);
	if (*plain_text[0] == '\n') {
		return NULL;
	}
	arrput(*plain_text, '\0');
	// debug("PLAIN:\n%s", *plain_text);

	char **lines = NULL;
	char *iter = *plain_text;
	size_t newline_idx = strcspn(iter, "\n");
	while (iter[newline_idx] == '\n') {
		iter[newline_idx] = '\0';
		if (iter[newline_idx - 1] == '\r') {
			iter[newline_idx - 1] = '\0';
		}
		arrput(lines, iter);
		// debug("%s", iter);
		iter += newline_idx + 1;
		newline_idx = strcspn(iter, "\n");
	}
	arrput(lines, iter);

	return lines;
}

KV *parse_pairs(char *content) {
	KV *params = NULL;
	size_t text_len = strlen(content);
	size_t offset = 0;
	while (offset < text_len) {
		size_t ampersand_idx = strcspn(content + offset, "&");
		size_t equal_idx = strcspn(content + offset, "=");

		content[offset + ampersand_idx] = '\0';
		if (equal_idx < ampersand_idx) {
			content[offset + equal_idx] = '\0';
		}
		shput(params, content + offset, equal_idx >= ampersand_idx ? "" : content + equal_idx + 1);
		offset += ampersand_idx + 1;
	}

	return params;
}

size_t parse_header(KV **header, char **lines) {
	if (arrlenu(lines) == 0) {
		return 0;
	}

	char *status_line = lines[0];
	size_t space_idx = strcspn(status_line, " ");
	if (status_line[space_idx] != ' ') {
		return 0;
	}
	status_line[space_idx] = '\0';
	shput(*header, "method", status_line);
	space_idx += 1;
	status_line += space_idx;

	space_idx = strcspn(status_line, " ");
	if (status_line[space_idx] != ' ') {
		return 0;
	}
	status_line[space_idx] = '\0';
	size_t hash_idx = strcspn(status_line, "#");
	if (hash_idx < space_idx) {
		// TODO: parse anchor part
		// https://developer.mozilla.org/en-US/docs/Learn_web_development/Howto/Web_mechanics/What_is_a_URL#anchor
		status_line[hash_idx] = '\0';
	}
	size_t question_idx = strcspn(status_line, "?");
	if (question_idx < hash_idx) {
		status_line[question_idx] = '\0';
		shput(*header, "parameters", status_line + question_idx + 1);
	}
	shput(*header, "path", status_line);
	space_idx += 1;
	status_line += space_idx;

	space_idx = strcspn(status_line, " ");
	if (status_line[space_idx] != '\0') {
		return 0;
	}
	shput(*header, "protocol", status_line);

	for (size_t i = 1; i < arrlenu(lines); i++) {
		if (lines[i][0] == '\0') {
			return i;
		}
		size_t colon_idx = strcspn(lines[i], ":");
		if (lines[i][colon_idx] != ':' || lines[i][colon_idx + 1] != ' ') {
			return 0;
		}
		lines[i][colon_idx] = '\0';

		tolowerstr(lines[i]);
		shput(*header, lines[i], lines[i] + colon_idx + 2);
	}

	return 0;
}

bool is_content_disposition_delimiter(const char *s, const char *boundary) {
	static char dash_dash[] = "--";
	if (strstr(s, dash_dash) != s) {
		return false;
	}

	s += sizeof(dash_dash) - 1;
	size_t boundary_len = strlen(boundary);
	if (strncmp(s, boundary, boundary_len) != 0) {
		return false;
	}

	s += boundary_len;
	return *s == '\0' || strcmp(s, dash_dash) == 0;
}

void parse_content_disposition(Context *ctx, char **lines, size_t i, const char *boundary) {
	static char content_disposition[] = "Content-Disposition: ";
	static char form_data[] = "form-data; ";
	static char name[] = "name=\"";
	static char filename[] = "filename=\"";

	while (i < arrlenu(lines)) {
		char *form_name = NULL, *file_name = NULL;
		char *iter = lines[i];
		if (strstr(iter, content_disposition) != iter) {
			i++;
			continue;
		}
		iter += sizeof(content_disposition) - 1;
		if (strstr(iter, form_data) != iter) {
			i++;
			continue;
		}
		iter += sizeof(form_data) - 1;
		if (strstr(iter, name) != iter) {
			i++;
			continue;
		}
		iter += sizeof(name) - 1;

		size_t quote_idx = strcspn(iter, "\"");
		form_name = strndup(iter, quote_idx);
		// debug("form name: %s", form_name);
		if (strstr(iter, "; ") != NULL) {
			iter = strstr(iter, "; ");
			iter += strlen("; ");

			if (strstr(iter, filename) != iter) {
				free(form_name);
				continue;
			}
			iter += sizeof(filename) - 1;

			quote_idx = strcspn(iter, "\"");
			if (iter[quote_idx] != '\"') {
				free(form_name);
				continue;
			}
			file_name = strndup(iter, quote_idx);
			// debug("file name: %s", file_name);
		}
		iter += quote_idx + 1;
		if (*iter != '\0') {
			i++;
			continue;
		}

		while (i < arrlenu(lines) && *lines[i] != '\0') {
			i++;
		}
		if (++i >= arrlenu(lines)) {
			break;
		}

		char *file_content = NULL;
		while (i < arrlenu(lines) && !is_content_disposition_delimiter(lines[i], boundary)) {
			strputstr(&file_content, lines[i], 0);
			arrput(file_content, '\n');
			i++;
		}
		(void) arrpop(file_content);
		arrput(file_content, '\0');
		strsetlen(&file_content, arrlenu(file_content) - 1);

		if (file_name != NULL) {
			MultipartForm *form = shgetp(ctx->multipart_form, form_name);
			if (form->key == NULL) {
				shputs(ctx->multipart_form, (MultipartForm) { .key = form_name });
				form = shgetp(ctx->multipart_form, form_name);
			}
			arrput(form->file_name, file_name);
			arrput(form->content, file_content);

			free(form_name);
		}
		else {
			// debug("{%s: %s}", form_name, file_content);
			shput(ctx->form_value, form_name, file_content);
		}
		i++;
	}
}

Context *parse_request(int client) {
	Context *ctx = calloc(1, sizeof(Context));
	char **lines = get_raw_request(&ctx->arena, client);
	if (lines == NULL || arrlen(ctx->arena) <= 1) {
		goto _return;
	}
	strsetlen(&ctx->arena, 0);

	shdefault(ctx->request_header, "");

	size_t i = parse_header(&ctx->request_header, lines);
	if (i == 0) {
		ctx->status_code = 400;
		return ctx;
	}

	const char *method = shget(ctx->request_header, "method");
	if (strcmp(method, "GET") == 0) {
		char *params = (char*) shget(ctx->request_header, "parameters");
		if (params != NULL) {
			ctx->query_param = parse_pairs(params);
		}
	}
	else if (strcmp(method, "POST") == 0) {
		if (++i >= arrlenu(lines)) {
			goto _return;
		}

		const char *content_type = shget(ctx->request_header, "content-type");
		if (*content_type == '\0' || strncmp(content_type, "application/x-www-form-urlencoded", 33) == 0) {
			ctx->form_value = parse_pairs(lines[i]);
		}
		else if (strncmp(content_type, "application/json", 16) == 0) {
			for (; i < arrlenu(lines); i++) {
				debug("%s", lines[i]);
			}
		}
		else if (strncmp(content_type, "multipart/form-data", 19) == 0) {
			char boundary[] = "boundary=";
			char *pbound = strstr(content_type, boundary);
			if (pbound == NULL) {
				goto _return;
			}
			sh_new_arena(ctx->multipart_form);
			parse_content_disposition(ctx, lines, i, pbound + sizeof(boundary) - 1);
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
	return ctx;
}
