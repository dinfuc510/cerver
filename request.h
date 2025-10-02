#include <stdio.h>
#include <stdlib.h>
#include "stb_ds.h"

#define NEWLINE "\r\n"
// #define ONLY_LF

Pairs parse_pairs(Slice content, const char *pair_delimiter, const char *delimiter) {
	Pairs pairs = {0};
	while (content.len > 0) {
		size_t pde_idx = slice_cspn(content, pair_delimiter);
		size_t de_idx = slice_cspn(content, delimiter);
		size_t key_len = pde_idx, val_len = 0;

		if (de_idx < pde_idx) {
			key_len = de_idx;
			val_len = pde_idx - key_len - 1;
		}
		Slice key = (Slice) { .ptr = content.ptr, .len = key_len };
		Slice val = (Slice) { .ptr = content.ptr + key_len + 1, .len = val_len };
		arrput(pairs.key, key);
		arrput(pairs.value, val);

		content = slice_advanced(content, pde_idx + 1);
	}

	return pairs;
}

bool is_cd_delimiter(Slice s, Slice boundary) {
	static char dash_dash[] = NEWLINE"--";
	if (slice_strstr(s, dash_dash) != s.ptr) {
		return false;
	}

	s = slice_advanced(s, sizeof(dash_dash) - 1);
	if (strncmp(s.ptr, boundary.ptr, boundary.len) != 0) {
		return false;
	}

	s = slice_advanced(s, boundary.len);
	return slice_strstr(s, NEWLINE) == s.ptr || slice_strstr(s, "--") == s.ptr;
}

void append_form_file(MultipartForm *mtform, Slice key, Slice name, Slice content) {
	size_t key_idx = slices_slice(mtform->keys, key);
	size_t len = arrlenu(mtform->keys);

	if (key_idx == len) {
		arrput(mtform->keys, key);
		FormFile ff = {0};
		arrput(mtform->form_files, ff);
	}
	arrput(mtform->form_files[key_idx].names, name);
	arrput(mtform->form_files[key_idx].contents, content);
}

void parse_multipart_form(Request *req, Slice boundary) {
	static char content_disposition[] = "Content-Disposition: form-data; name=\"";
	static char filename[] = "filename=\"";

	Slice form_name = {0}, file_name = {0}, file_content = {0};
	Slice body = req->body;
	while (body.len > 0) {
		if (form_name.len > 0) {
			if (file_name.len > 0) {
				// debug("%s", "multipart");
				append_form_file(&req->multipart_form, form_name, file_name, file_content);
				file_name.len = 0;
			}
			else {
				// debug("%s", "form value");
				arrput(req->form_values.key, form_name);
				arrput(req->form_values.value, file_content);
			}
			form_name.len = 0;
		}
		char *delimiter = slice_strstr(body, "--");
		if (delimiter == NULL) {
			debug("%s", "END");
			break;
		}
		body = slice_advanced(body, delimiter - body.ptr + strlen("--"));
		if (slice_slice(body, boundary) != body.ptr) {
			// debug("%s", "");
			continue;
		}
		body = slice_advanced(body, boundary.len);
		if (slice_strstr(body, "--") == body.ptr) {
			debug("%s", "");
			break;
		}
		if (slice_strstr(body, NEWLINE) != body.ptr) {
			debug("%s", "");
			break;
		}
		body = slice_advanced(body, strlen(NEWLINE));

		size_t crlf_idx = slice_cspn(body, NEWLINE);
#ifndef ONLY_LF
		if (body.ptr[crlf_idx + 1] != '\n') {
			debug("%s", "");
			break;
		}
#endif
		Slice line = { .ptr = body.ptr, .len = crlf_idx };
		char *iter = slice_strstr(line, content_disposition);
		if (iter == NULL) {
			debug("%s", "");
			break;
		}
		line = slice_advanced(line, iter - line.ptr + sizeof(content_disposition) - 1);

		size_t quote_idx = slice_cspn(line, "\"");
		if (line.ptr[quote_idx] != '"') {
			debug("%s", "");
			break;
		}

		form_name = (Slice) { .ptr = line.ptr, .len = quote_idx };
		iter = slice_strstr(line, "; ");
		if (iter != NULL) {
			line = slice_advanced(line, iter - line.ptr + strlen("; "));

			iter = slice_strstr(line, filename);
			if (iter != line.ptr) {
				debug("%s", "");
				break;
			}
			line = slice_advanced(line, iter - line.ptr + sizeof(filename) - 1);

			quote_idx = slice_cspn(line, "\"");
			if (line.ptr[quote_idx] != '"') {
				debug("%s", "");
				break;
			}
			file_name = (Slice) { .ptr = line.ptr, .len = quote_idx };
		}
		line = slice_advanced(line, quote_idx + 1);
		if (line.len > 0) {
			debug("%s", "");
			break;
		}
		char *crlf_crlf = slice_strstr(body, NEWLINE NEWLINE);
		body = slice_advanced(body, crlf_crlf - body.ptr + strlen(NEWLINE)*2);
		file_content.ptr = body.ptr;

		const char *newline_dd = NEWLINE "--";
		char *dash_dash = slice_strstr(body, newline_dd);
		if (dash_dash == NULL) {
			debug("%s", "");
			break;
		}
		body = slice_advanced(body, dash_dash - body.ptr);
		while (body.len > 0 && !is_cd_delimiter(body, boundary)) {
			body = slice_advanced(body, strlen(newline_dd));
			dash_dash = slice_strstr(body, newline_dd);
			if (dash_dash == NULL) {
				debug("%s", "end");
				break;
			}
			body = slice_advanced(body, dash_dash - body.ptr);
		}
		file_content.len = body.ptr - file_content.ptr;
		body = slice_advanced(body, strlen(NEWLINE));
	}

	req->multipart_form.boundary = boundary;
}

int parse_request(Request *req, size_t raw_len) {
	char *raw = req->arena;

	int state = HTTP_METHOD;
	Slice slice = { .ptr = raw };
	Slice key = {0}, val = {0};
	bool fail = false;
	Slice content_type = {0};

	for (size_t i = 0; i < raw_len; i++) {
		switch (state) {
			case HTTP_METHOD: {
				if (raw[i] == ' ') {
					req->method = slice;
					slice = (Slice) { .ptr = raw + i + 1 };
					state = HTTP_PATH;
				}
				else {
					slice.len += 1;
				}
				break;
			}
			case HTTP_PATH: {
				if (raw[i] == ' ') {
					req->path = slice;
					slice = (Slice) { .ptr = raw + i + 1 };
					state = HTTP_VERSION;
				}
				else {
					slice.len += 1;
				}
				break;
			}
			case HTTP_VERSION: {
				if (raw[i] == '\r') {
					if (raw[i + 1] != '\n') {
						fail = true;
						goto _return;
					}
					req->http_version = slice;
					key = (Slice) { .ptr = raw + i + 2 };
					state = HTTP_HEADER_KEY;
				}
#ifdef ONLY_LF
				else if (raw[i] == '\n') {
					req->http_version = slice;
					key = (Slice) { .ptr = raw + i + 1 };
					state = HTTP_HEADER_KEY;
				}
#endif
				else {
					slice.len += 1;
				}
				break;
			}
			case HTTP_HEADER_KEY: {
				if (raw[i] == ':') {
					if (raw[i + 1] != ' ') {
						fail = true;
						goto _return;
					}
					val = (Slice) { .ptr = raw + i + 2 };
					state = HTTP_HEADER_VALUE;
				}
				else {
					if (raw[i] >= 'A' && raw[i] <= 'Z') {
						raw[i] += 'a' - 'A';
					}
					key.len += 1;
				}
				break;
			}
			case HTTP_HEADER_VALUE: {
				if (raw[i] == '\r') {
					if (raw[i + 1] != '\n') {
						fail = true;
						goto _return;
					}
					key.len -= 1;
					val.len -= 1;
					arrput(req->headers.key, key);
					arrput(req->headers.value, val);

					if (slice_equal_cstr(key, "content-type")) {
						content_type = val;
					}
					key = (Slice) { .ptr = raw + i + 2 };
					state = HTTP_HEADER_KEY;
					if (raw[i + 2] == '\r' && raw[i + 3] == '\n') {
						i += 3;
						state = HTTP_BODY;
					}
				}
#ifdef ONLY_LF
				else if (raw[i] == '\n') {
					val.len -= 1;
					Header header = (Header) { .key = key, .value = val };
					arrput(req->headers, header);

					if (slice_equal_cstr(key, "content-length")) {
						for (size_t vi = 0; vi < val.len; vi++) {
							if (val.ptr[vi] < '0' || val.ptr[vi] > '9') {
								return NULL;
							}

							content_length = content_length*10 + (val.ptr[vi] - '0');
						}
					}
					else if (slice_equal_cstr(key, "content-type")) {
						content_type = val;
					}

					key = (Slice) { .ptr = raw + i + 1 };
					state = HTTP_HEADER_KEY;
					if (raw[i + 1] == '\n') {
						i += 1;
						state = HTTP_BODY;
					}
				}
#endif
				else {
					val.len += 1;
				}
				break;
			}
			case HTTP_BODY: {
				slice = (Slice) { .ptr = raw + i, .len = raw_len - i - 1 };
				req->body = slice;
				i = raw_len - 1;
				break;
			}
		}
	}

	size_t hash_idx = slice_cspn(req->path, "#");
	if (req->path.ptr[hash_idx] == '#') {
		req->path.len = hash_idx;
	}

	Slice query_param = {0};
	size_t question_idx = slice_cspn(req->path, "?");
	if (req->path.ptr[question_idx] == '?') {
		query_param = (Slice) { .ptr = req->path.ptr + question_idx + 1, req->path.len - question_idx };
		req->path.len = question_idx;
	}

	if (slice_equal_cstr(req->method, "GET") && query_param.len > 0) {
		req->query_parameters = parse_pairs(query_param, "&", "=");
	}
	else if (slice_equal_cstr(req->method, "POST")) {
		if (slice_equal_cstr(content_type, "application/x-www-form-urlencoded")) {
			req->form_values = parse_pairs(req->body, "&", "=");
		}
		else if (content_type.ptr != NULL && strncmp(content_type.ptr, "multipart/form-data", 19) == 0) {
			size_t semiconlon_idx = slice_cspn(content_type, ";");
			size_t equal_idx = slice_cspn(content_type, "=");
			if (content_type.ptr[semiconlon_idx] != ';' || content_type.ptr[semiconlon_idx + 1] != ' ' ||
				content_type.ptr[equal_idx] != '=' || equal_idx < semiconlon_idx) {
				fail = true;
				goto _return;
			}

			Slice boundary = (Slice) { .ptr = content_type.ptr + equal_idx + 1, .len = content_type.len - equal_idx - 1 };
			parse_multipart_form(req, boundary);
		}
	}

_return:
	return fail ? 400 : 0;
}

void print_request(Request *req) {
	if (req == NULL) {
		return;
	}

	debug("Method: %.*s", (int) req->method.len, req->method.ptr);
	debug("Path: %.*s", (int) req->path.len, req->path.ptr);
	debug("HTTP version: %.*s", (int) req->http_version.len, req->http_version.ptr);

	for (size_t i = 0; i < arrlenu(req->headers.key); i++) {
		Slice key = req->headers.key[i];
		Slice value = req->headers.value[i];
		debug("%.*s:%.*s", (int) key.len, key.ptr, (int) value.len, value.ptr);
	}
	for (size_t i = 0; i < arrlenu(req->query_parameters.key); i++) {
		Slice key = req->query_parameters.key[i];
		Slice value = req->query_parameters.value[i];
		debug("%.*s:%.*s", (int) key.len, key.ptr, (int) value.len, value.ptr);
	}
	for (size_t i = 0; i < arrlenu(req->form_values.key); i++) {
		Slice key = req->form_values.key[i];
		Slice value = req->form_values.value[i];
		debug("%.*s:%.*s", (int) key.len, key.ptr, (int) value.len, value.ptr);
	}
	for (size_t i = 0; i < arrlenu(req->multipart_form.keys); i++) {
		Slice key = req->multipart_form.keys[i];
		debug("%.*s", (int) key.len, key.ptr);

		FormFile ff = req->multipart_form.form_files[i];
		for (size_t ffi = 0; ffi < arrlenu(ff.names); ffi++) {
			Slice name = ff.names[ffi];
			Slice content = ff.contents[ffi];
			debug("%.*s(%ld):%.*s", (int) name.len, name.ptr, content.len, (int) content.len, content.ptr);
		}
	}
	// debug("Body: %.*s", (int) req->body.len, req->body.ptr);
}
