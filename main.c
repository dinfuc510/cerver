#include <errno.h>
#include <signal.h>
#include "cerver.h"

#define PORT 12345

static Cerver c = {0};
void cleanup(int code) {
	(void) code;
#ifdef linux
	if (close(c.server) == 0) {
#else
	if (closesocket(c.server) == 0) {
#endif
		c.server = -1;
		printf("Shutdown server\n");
	}
}

int page404(Context *ctx) {
	Slice path = ctx->request->path;
	debug("%.*s", (int) path.len, path.ptr);
	Slice accept_header = request_header(ctx, "accept");
	if (!slice_empty(accept_header)) {
		debug("%.*s", (int) accept_header.len, accept_header.ptr);
	}

	if (slice_equal_cstr(path, "/main.c")) {				// we can use this for download file
		file(ctx, 200, "main.c");
		return 0;
	}
	else if (slice_strstr(accept_header, "html") == NULL) { // TODO: find a better way to check if
		no_content(ctx, 404);			  					// the request is looking for a html file
		return 0;
	}

	FILE *f = fopen("404.html", "rb");
	if (f == NULL) {
		no_content(ctx, 404);
		return 0;
	}

	html(ctx, 404, "%F", f);

	fclose(f);
	return 0;
}

int homepage(Context *ctx) {
	const char *index_html = "index.html";
	FILE *f = fopen(index_html, "rb");
	if (f == NULL) {
		html(ctx, 200, "Placing %s in this folder", index_html);
		return 0;
	}

	html(ctx, 200, "%F", f);

	fclose(f);
	return 0;
}

int favicon(Context *ctx) {
	FILE *f = fopen("favicon.ico", "rb");
	if (f == NULL) {
		no_content(ctx, 404);
		return 0;
	}

	stream(ctx, 200, "image/x-icon", f);

	fclose(f);
	return 0;
}

int redirect_to(Context *ctx) {
	Slice path = ctx->request->path;
	if (slice_equal_cstr(path, "/")) {
		Slice host_header = {0}; // request_header(ctx, "host");
		const char *dest = "/homepage";
		GString url = {0};
		gstr_append_fmt_null(&url, "%Sl%s", host_header, dest);

		redirect(ctx, 301, url.ptr);

		gstr_free(&url);
		return 0;
	}

	return page404(ctx);
}

int concat(Context *ctx) {
	Slice first_arg = form_value(ctx, "1");
	Slice second_arg = form_value(ctx, "2");
	html(ctx, 200, "%Sl%Sl", first_arg, second_arg);

	return 0;
}

int hello(Context *ctx) {
	Slice name = query_param(ctx, "name");
	html(ctx, 200, "Hello %Sl", name);

	return 0;
}

int sleep10(Context *ctx) {
#ifdef linux
	sleep(10);
#else
	Sleep(10000);
#endif
	no_content(ctx, 200);
	return 0;
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
	return 0;
}

int upload(Context *ctx) {
	Slice name = form_value(ctx, "name");
	if (slice_empty(name)) {
		html(ctx, 400, "Missing `name` field");
		return 0;
	}

	FormFile form = form_file(ctx, "files");
	size_t nfiles = form.npairs;
	GString msg = {0};

	const char dir[] = "temp/";
	GString path = gstr_from_cstr(dir);
	for (size_t i = 0; i < nfiles; i++) {
		path.len = sizeof(dir) - 1;
		gstr_append_fmt_null(&path, "%Sl", form.pairs->keys[i]);
		gstr_append_fmt(&msg, "%Sl: ", form.pairs->keys[i]);

		FILE *f = fopen(path.ptr, "rb");
		if (f != NULL) {
			gstr_append_fmt(&msg, "already exists\n");
			fclose(f);
			continue;
		}
		f = fopen(path.ptr, "wb");
		if (f == NULL) {
			gstr_append_fmt(&msg, "%s\n", strerror(errno));
			continue;
		}

		size_t nbytes = fwrite(form.pairs->values[i].ptr, form.pairs->values[i].len, 1, f);
		debug("fwrite returned %zd", nbytes);

		gstr_append_fmt(&msg, "upload succesfully\n");

		fclose(f);
	}
	gstr_free(&path);

	if (gstr_empty(msg)) {
		no_content(ctx, 200);
		return 0;
	}

	gstr_pop(&msg);
	html(ctx, 200, "%Sg", msg);

	gstr_free(&msg);

	return 0;
}

int main(void) {
	signal(SIGINT, cleanup);

	cerver_get(c, "/", redirect_to);
	cerver_get(c, "/favicon.ico", favicon);
	cerver_get(c, "/homepage", homepage);
	cerver_get(c, "/hello", hello);
	cerver_get(c, "/sleep", sleep10);
	cerver_get(c, "/download", download);
	register_route(&c, "GET", page404);
	cerver_post(c, "/concat", concat);
	cerver_post(c, "/upload", upload);
	if (!run(&c, PORT)) {
		debug("%s", strerror(errno));
		return 1;
	}

	return 0;
}

