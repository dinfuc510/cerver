#include "stb_ds.h"

#ifndef CERVER_DEBUG
	#define CERVER_DEBUG 1
#endif
#define debug(fmt, ...) do {		\
		if (CERVER_DEBUG) {			\
			printf("[%s:%d] "fmt"\n", __FILE__, __LINE__, __VA_ARGS__);		\
		}							\
	} while(0)

enum {
	HTTP_METHOD = 0,
	HTTP_PATH,
	HTTP_VERSION,
	HTTP_HEADER_KEY,
	HTTP_HEADER_VALUE,
	HTTP_BODY,
};

typedef struct {
	const char *ptr;
	size_t len;
} Slice;

typedef struct {
	Slice *key;
	Slice *value;
} Pairs;
typedef Pairs Header;
typedef Pairs QueryParameter;
typedef Pairs FormValue;

typedef struct {
	Slice *names;
	Slice *contents;
} FormFile;

typedef struct {
	Slice boundary;
	Slice *keys;
	FormFile *form_files;
} MultipartForm;

typedef struct {
	Slice method;
	Slice path;
	Slice http_version;

	Header headers;
	Slice body;

	QueryParameter query_parameters;
	FormValue form_values;
	MultipartForm multipart_form;

	char *arena;
} Request;

enum {
	CERVER_RESPONSE = 0,
	INTERNAL_RESPONSE,
};

typedef struct {
	int client;

	Request *request;

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

bool slice_equal(Slice a, Slice b) {
	return a.len == b.len && strncmp(a.ptr, b.ptr, a.len) == 0;
}

bool slice_equal_cstr(Slice s, const char *cs) {
	size_t si = 0;
	while (si < s.len && *cs != '\0') {
		if (s.ptr[si] != *cs) {
			break;
		}
		si += 1;
		cs += 1;
	}

	return si == s.len && *cs == '\0';
}

Slice slice_cstr(const char *cstr) {
	return (Slice) { .ptr = cstr, .len = strlen(cstr) };
}

size_t slice_cspn(Slice s, const char *reject) {
	for (size_t i = 0; i < s.len; i++) {
		if (strchr(reject, s.ptr[i]) != NULL) {
			return i;
		}
	}

	return s.len;
}

char *slice_slice(Slice s, Slice needle) {
	size_t si = 0, match_len = 0;
	while (match_len < needle.len && si < s.len) {
		if (s.ptr[si] != needle.ptr[si]) {
			match_len = 0;
		}
		else {
			match_len += 1;
		}
		si += 1;
	}

	return match_len == needle.len ? (char*) s.ptr + si - match_len : NULL;
}

size_t slices_slice(Slice *s, Slice needle) {
	size_t slen = arrlenu(s);
	for (size_t i = 0; i < slen; i++) {
		if (slice_equal(s[i], needle)) {
			return i;
		}
	}

	return slen;
}

Slice pair_slice(Pairs *pairs, Slice needle) {
	size_t needle_idx = slices_slice(pairs->key, needle);
	if (needle_idx < arrlenu(pairs->key)) {
		return pairs->value[needle_idx];
	}

	return (Slice) {0};
}

FormFile formfile_slice(MultipartForm *mtform, Slice needle) {
	size_t needle_idx = slices_slice(mtform->keys, needle);
	if (needle_idx < arrlenu(mtform->keys)) {
		return mtform->form_files[needle_idx];
	}

	return (FormFile) {0};
}

char *slice_strstr(Slice s, const char *needle) {
	size_t si = 0, match_len = 0, needle_len = strlen(needle);
	while (match_len < needle_len && si < s.len) {
		if (s.ptr[si] != needle[match_len]) {
			match_len = 0;
		}
		if (s.ptr[si] == needle[match_len]) {
			match_len += 1;
		}
		si += 1;
	}

	return match_len == needle_len ? (char*) s.ptr + si - match_len : NULL;
}

Slice slice_advanced(Slice s, size_t len) {
	if (len > s.len) {
		len = s.len;
	}

	s.ptr += len;
	s.len -= len;
	return s;
}

void free_request(Request *req) {
	arrfree(req->headers.key);
	arrfree(req->headers.value);
	arrfree(req->query_parameters.key);
	arrfree(req->query_parameters.value);
	arrfree(req->form_values.key);
	arrfree(req->form_values.value);
	arrfree(req->multipart_form.keys);
	for (size_t i = 0; i < arrlenu(req->multipart_form.form_files); i++) {
		FormFile ff = req->multipart_form.form_files[i];
		arrfree(ff.names);
		arrfree(ff.contents);
	}
	arrfree(req->multipart_form.form_files);
	arrfree(req->arena);
	free(req);
}

void free_context(Context *ctx) {
	free_request(ctx->request);
	arrfree(ctx->response_body);
	free(ctx);
}
