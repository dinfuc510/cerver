#include <errno.h>
#include <signal.h>
#include "cerver.h"

#define PORT 12345

static Cerver c = {0};
void cleanup(int code) {
	(void) code;
	if (close(c.server) == 0) {
		c.server = -1;
		printf("Shutdown server\n");
	}
}

int page404(Context *ctx) {
	Slice path = ctx->request->path;
	debug("%.*s", (int) path.len, path.ptr);
	Slice accept_header = request_header(ctx, "accept");
	if (accept_header.len > 0) {
		debug("%.*s", (int) accept_header.len, accept_header.ptr);
	}

	if (slice_equal_cstr(path, "/main.c")) {
		file(ctx, 200, "main.c");
		return CERVER_RESPONSE;
	}
	else if (slice_strstr(accept_header, "html") == NULL) { // TODO: find a better way to check if
		no_content(ctx, 404);			  					// the request is looking for a html file
		return CERVER_RESPONSE;
	}

	FILE *f = fopen("404.html", "rb");
	if (f == NULL) {
		no_content(ctx, 404);
		return CERVER_RESPONSE;
	}

	html(ctx, 404, "%F", f);

	fclose(f);
	return CERVER_RESPONSE;
}

int homepage(Context *ctx) {
	FILE *f = fopen("index.html", "rb");
	if (f == NULL) {
		no_content(ctx, 404);
		return CERVER_RESPONSE;
	}

	html(ctx, 200, "%F", f);

	fclose(f);
	return CERVER_RESPONSE;
}

int favicon(Context *ctx) {
	FILE *f = fopen("favicon.ico", "rb");
	if (f == NULL) {
		no_content(ctx, 404);
		return CERVER_RESPONSE;
	}

	stream(ctx, 200, "image/x-icon", f);

	fclose(f);
	return CERVER_RESPONSE;
}

int redirect_to(Context *ctx) {
	Slice path = ctx->request->path;
	if (slice_equal_cstr(path, "/")) {
		Slice host_header = {0}; // request_header(ctx, "host");
		const char *dest = "/homepage";
		GString url = {0};
		gstr_append_fmt_null(&url, "%Sl%s", host_header, dest);

		redirect(ctx, 301, url.ptr);

		free(url.ptr);
		return CERVER_RESPONSE;
	}

	return page404(ctx);
}

int concat(Context *ctx) {
	Slice first_arg = form_value(ctx, "1");
	Slice second_arg = form_value(ctx, "2");
	html(ctx, 200, "%Sl%Sl", first_arg, second_arg);

	return CERVER_RESPONSE;
}

int hello(Context *ctx) {
	Slice name = query_param(ctx, "name");
	html(ctx, 200, "Hello %Sl", name);

	return CERVER_RESPONSE;
}

int sleep10(Context *ctx) {
	sleep(10);
	no_content(ctx, 200);
	return CERVER_RESPONSE;
}

int download(Context *ctx) {
	html(ctx, 200,
			"<!DOCTYPE html>\r\n"
			"<html lang=\"en\">\r\n"
			"<head>\r\n"
				"<meta charset=\"utf-8\">\r\n"
			"</head>\r\n"
			"<body>\r\n"
				"<a>Source code</a>\r\n"
				"<form action=\"/main.c\">\r\n"
					"<input type=\"submit\" value=\"Download\" />\r\n"
				"</form>\r\n"
			"</body>\r\n"
			"</html>"
	);
	return CERVER_RESPONSE;
}

int upload(Context *ctx) {
	Slice name = form_value(ctx, "name");
	if (name.len == 0) {
		html(ctx, 400, "Missing `name` field");
		return CERVER_RESPONSE;
	}

	FormFile form = find_key_in_multipart_form(&ctx->request->multipart_form, slice_cstr("files"));
	size_t nfiles = form.npairs;
	GString msg = {0};

	const char *dir = "temp/";
	GString path = gstr_from_cstr(dir);
	for (size_t i = 0; i < nfiles; i++) {
		path.len = strlen(dir);
		gstr_append_fmt_null(&path, "%Sl", form.pairs->keys[i]);
		gstr_append_fmt(&msg, "%Sl: ", form.pairs->keys[i]);

		FILE *f = fopen(path.ptr, "rb");
		if (f != NULL) {
			gstr_append_fmt(&msg, "aready exists\n");
			fclose(f);
			continue;
		}
		f = fopen(path.ptr, "wb");
		if (f == NULL) {
			gstr_append_fmt(&msg, "%s\n", strerror(errno));
			continue;
		}

		size_t nbytes = fwrite(form.pairs->values[i].ptr, form.pairs->values[i].len, 1, f);
		debug("fwrite returned %ld", nbytes);

		gstr_append_fmt(&msg, "upload succesfully\n");

		fclose(f);
	}
	free(path.ptr);

	if (msg.len == 0) {
		no_content(ctx, 200);
		return CERVER_RESPONSE;
	}

	if (msg.len > 0) {
		msg.len -= 1;
	}

	html(ctx, 200, "%Sg", msg);

	free(msg.ptr);

	Slice expect_header = request_header(ctx, "expect");
	if (slice_equal_cstr(expect_header, "100-continue")) {
		trace_log;
		no_content(ctx, 200);
		send_response(ctx);
		return INTERNAL_RESPONSE;
	}
	return CERVER_RESPONSE;
}

int main(void) {
	signal(SIGINT, cleanup);

	register_route(&c, "GET:/", redirect_to);
	register_route(&c, "GET:/favicon.ico", favicon);
	register_route(&c, "GET:/homepage", homepage);
	register_route(&c, "GET:/hello", hello);
	register_route(&c, "GET:/sleep", sleep10);
	register_route(&c, "GET:/download", download);
	register_route(&c, "GET", page404);
	register_route(&c, "POST:/concat", concat);
	register_route(&c, "POST:/upload", upload);
	if (!run(&c, PORT)) {
		debug("%s", strerror(errno));
		return 1;
	}
	exit(1);

	return 0;
}

