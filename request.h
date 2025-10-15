#ifndef REQUEST_H
#define REQUEST_H
#include <stdio.h>
#include <stdlib.h>

#define NEWLINE 			"\r\n"
#define DASH_DASH			"--"
#define NEWLINE_DASH_DASH 	NEWLINE DASH_DASH

#define query_param(ctx, key) find_key_in_pairs(&(ctx)->request->query_parameters, slice_cstr(key))
#define form_value(ctx, key) find_key_in_pairs(&(ctx)->request->form_values, slice_cstr(key))
#define request_header(ctx, key) find_key_in_pairs(&(ctx)->request->headers, slice_cstr(key))

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
		append_pair(&pairs, key, val);

		content = slice_advanced(content, pde_idx + 1);
	}

	return pairs;
}

bool is_content_disposition_delimiter(Slice s, Slice boundary) {
	if (slice_strstr(s, NEWLINE_DASH_DASH) != s.ptr) {
		return false;
	}

	s = slice_advanced(s, sizeof(NEWLINE_DASH_DASH) - 1);
	if (strncmp(s.ptr, boundary.ptr, boundary.len) != 0) {
		return false;
	}

	s = slice_advanced(s, boundary.len);
	return slice_strstr(s, NEWLINE) == s.ptr || slice_strstr(s, DASH_DASH) == s.ptr;
}

bool append_form_file(MultipartForm *mtform, Slice key, Slice name, Slice content) {
	size_t key_idx = find_slice_in_slices(mtform->keys, mtform->nkeys, key);

	if (key_idx == mtform->nkeys) {
		if (mtform->nkeys >= mtform->capacity) {
			size_t new_cap = mtform->capacity * 2;
			if (new_cap <= mtform->nkeys) {
				new_cap = mtform->nkeys + 1;
			}
			if (new_cap <= mtform->nkeys) {
				return false;
			}

			Slice *new_keys = realloc(mtform->keys, new_cap*sizeof(Slice));
			if (new_keys == NULL) {
				return false;
			}
			mtform->keys = new_keys;

			FormFile *new_form_files = realloc(mtform->form_files, new_cap*sizeof(FormFile));
			if (new_form_files == NULL) {
				return false;
			}
			mtform->form_files = new_form_files;
		}
		mtform->keys[mtform->nkeys] = key;
		FormFile ff = {
			.pairs = calloc(1, sizeof(Pairs)),
		};
		if (ff.pairs == NULL) {
			return false;
		}
		mtform->form_files[mtform->nkeys] = ff;
		mtform->nkeys += 1;
	}
	append_pair(mtform->form_files[key_idx].pairs, name, content);
	mtform->form_files[key_idx].npairs += 1;
	return true;
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
				append_pair(&req->form_values, form_name, file_content);
			}
			form_name.len = 0;
		}
		const char *delimiter = slice_strstr(body, DASH_DASH);
		if (delimiter == NULL) {
			// trace_log;
			break;
		}
		body = slice_advanced(body, delimiter - body.ptr + strlen(DASH_DASH));
		if (slice_slice(body, boundary) != body.ptr) {
			// trace_log;
			continue;
		}
		body = slice_advanced(body, boundary.len);
		if (slice_strstr(body, DASH_DASH) == body.ptr) {
			// debug("%s", "end");
			break;
		}
		if (slice_strstr(body, NEWLINE) != body.ptr) {
			trace_log;
			break;
		}
		body = slice_advanced(body, strlen(NEWLINE));

		size_t crlf_idx = slice_cspn(body, NEWLINE);
		Slice line = { .ptr = body.ptr, .len = crlf_idx };
		const char *iter = slice_strstr(line, content_disposition);
		if (iter == NULL) {
			trace_log;
			break;
		}
		line = slice_advanced(line, iter - line.ptr + sizeof(content_disposition) - 1);

		size_t quote_idx = slice_cspn(line, "\"");
		if (line.ptr[quote_idx] != '"') {
			trace_log;
			break;
		}

		form_name = (Slice) { .ptr = line.ptr, .len = quote_idx };
		// debug("%.*s", (int) form_name.len, form_name.ptr);
		iter = slice_strstr(line, "; ");
		if (iter != NULL) {
			line = slice_advanced(line, iter - line.ptr + strlen("; "));

			iter = slice_strstr(line, filename);
			if (iter != line.ptr) {
				trace_log;
				break;
			}
			line = slice_advanced(line, iter - line.ptr + sizeof(filename) - 1);

			quote_idx = slice_cspn(line, "\"");
			if (line.ptr[quote_idx] != '"') {
				trace_log;
				break;
			}
			file_name = (Slice) { .ptr = line.ptr, .len = quote_idx };
			// debug("%.*s", (int) file_name.len, file_name.ptr);
		}
		line = slice_advanced(line, quote_idx + 1);
		if (line.len > 0) {
			trace_log;
			break;
		}
		const char *crlf_crlf = slice_strstr(body, NEWLINE NEWLINE);
		body = slice_advanced(body, crlf_crlf - body.ptr + strlen(NEWLINE)*2);
		file_content.ptr = body.ptr;

		const char *newline_dd = slice_strstr(body, NEWLINE_DASH_DASH);
		if (newline_dd == NULL) {
			trace_log;
			break;
		}
		body = slice_advanced(body, newline_dd - body.ptr);
		while (body.len > 0 && !is_content_disposition_delimiter(body, boundary)) {
			body = slice_advanced(body, strlen(NEWLINE_DASH_DASH));
			newline_dd = slice_strstr(body, NEWLINE_DASH_DASH);
			if (newline_dd == NULL) {
				trace_log;
				break;
			}
			body = slice_advanced(body, newline_dd - body.ptr);
		}
		file_content.len = body.ptr - file_content.ptr;
		body = slice_advanced(body, strlen(NEWLINE));
	}

	req->multipart_form.boundary = boundary;
}

int parse_request(Request *req) {
	char *raw = req->arena.ptr;
	size_t raw_len = req->arena.len;

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
					if (isupper(raw[i])) {
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
					append_pair(&req->headers, key, val);

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
	if (state != HTTP_BODY) {
		fail = true;
		goto _return;
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
			if (strncmp(content_type.ptr + semiconlon_idx, "; ", 2) != 0 ||
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

	for (size_t i = 0; i < req->headers.len; i++) {
		Slice key = req->headers.keys[i];
		Slice value = req->headers.values[i];
		debug("%.*s:%.*s", (int) key.len, key.ptr, (int) value.len, value.ptr);
	}
	for (size_t i = 0; i < req->query_parameters.len; i++) {
		Slice key = req->query_parameters.keys[i];
		Slice value = req->query_parameters.values[i];
		debug("%.*s:%.*s", (int) key.len, key.ptr, (int) value.len, value.ptr);
	}
	for (size_t i = 0; i < req->form_values.len; i++) {
		Slice key = req->form_values.keys[i];
		Slice value = req->form_values.values[i];
		debug("%.*s:%.*s", (int) key.len, key.ptr, (int) value.len, value.ptr);
	}
	for (size_t i = 0; i < req->multipart_form.nkeys; i++) {
		Slice key = req->multipart_form.keys[i];
		debug("%.*s", (int) key.len, key.ptr);

		FormFile ff = req->multipart_form.form_files[i];
		for (size_t ffi = 0; ffi < ff.npairs; ffi++) {
			Slice name = ff.pairs->keys[ffi];
			Slice content = ff.pairs->values[ffi];
			debug("%.*s(%ld):%.*s", (int) name.len, name.ptr, content.len, (int) content.len, content.ptr);
		}
	}
	// debug("Body: %.*s", (int) req->body.len, req->body.ptr);
}

#endif // REQUEST_H
