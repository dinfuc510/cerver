#ifndef CER_DS_H
#define CER_DS_H

#include <ctype.h>
#include "stb_ds.h"
#include "cer_ds/slice.h"
#include "cer_ds/pair.h"
#include "cer_ds/route.h"
#include "cer_ds/growable_string.h"

#ifndef CERVER_DEBUG
	#define CERVER_DEBUG 1
#endif
#define debug(fmt, ...) do {												\
		if (CERVER_DEBUG) {													\
			printf("[%s:%d] " fmt "\n", __FILE__, __LINE__, __VA_ARGS__);		\
		}																	\
	} while(0)

enum {
	HTTP_METHOD = 0,
	HTTP_PATH,
	HTTP_VERSION,
	HTTP_HEADER_KEY,
	HTTP_HEADER_VALUE,
	HTTP_BODY,
};

enum {
	CERVER_RESPONSE = 0,
	INTERNAL_RESPONSE,
};

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

typedef struct {
	int client;

	Request *request;

	int status_code;
	char *response_header; // TODO: consider using hashmap
	char *response_body;
} Context;

typedef int (*Callback)(Context*);
typedef struct {
	int server;
	Route *route;
} Cerver;

typedef struct {
	Cerver *c;
	int client;
} ThreadInfo;

FormFile find_key_in_multipart_form(MultipartForm *mtform, Slice key) {
	size_t needle_idx = find_slice_in_slices(mtform->keys, key);
	if (needle_idx < arrlenu(mtform->keys)) {
		return mtform->form_files[needle_idx];
	}

	return (FormFile) {0};
}

void free_request(Request *req) {
	free(req->headers.keys);
	free(req->headers.values);
	free(req->query_parameters.keys);
	free(req->query_parameters.values);
	free(req->form_values.keys);
	free(req->form_values.values);
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
	arrfree(ctx->response_header);
	arrfree(ctx->response_body);
	free(ctx);
}

#endif // CER_DS_H
