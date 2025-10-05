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

	if (slice_strstr(accept_header, "html") == NULL) { 		// TODO: find a better way to check if
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
		char *url = NULL;
		strputfmtn(&url, "%Ls%s", host_header, dest);

		redirect(ctx, 301, url);

		arrfree(url);
		return CERVER_RESPONSE;
	}

	return page404(ctx);
}

int concat(Context *ctx) {
	Slice first_arg = form_value(ctx, "1");
	Slice second_arg = form_value(ctx, "2");
	html(ctx, 200, "%Ls%Ls", first_arg, second_arg);

	return CERVER_RESPONSE;
}

int hello(Context *ctx) {
	Slice name = query_param(ctx, "name");
	html(ctx, 200, "Hello %Ls", name);

	return CERVER_RESPONSE;
}

int sleep10(Context *ctx) {
	sleep(10);
	no_content(ctx, 200);
	return CERVER_RESPONSE;
}

int upload(Context *ctx) {
	Slice expect_header = request_header(ctx, "expect");
	if (slice_equal_cstr(expect_header, "100-continue")) {
		no_content(ctx, 100);
		send_response(ctx);

		Context *continue_ctx = create_context(ctx->client);
		free_context(continue_ctx);
		return INTERNAL_RESPONSE;
	}

	Slice name = find_key_in_pairs(&ctx->request->form_values, slice_cstr("name"));
	if (name.len == 0) {
		html(ctx, 400, "Missing `name` field");
		return CERVER_RESPONSE;
	}

	FormFile form = find_key_in_multipart_form(&ctx->request->multipart_form, slice_cstr("files"));
	char *msg = NULL;
	size_t nfiles = arrlenu(form.names);

	const char *dir = "temp/";
	char *path = NULL;
	strputstr(&path, dir, strlen(dir));
	for (size_t i = 0; i < nfiles; i++) {
		strsetlen(&path, strlen(dir));
		strputfmtn(&path, "%Ls", form.names[i]);
		strputfmt(&msg, "%Ls: ", form.names[i]);

		FILE *f = fopen(path, "rb");
		if (f != NULL) {
			strputfmt(&msg, "aready exists\n");
			fclose(f);
			continue;
		}
		f = fopen(path, "wb");
		if (f == NULL) {
			strputfmt(&msg, "%s\n", strerror(errno));
			continue;
		}

		size_t nbytes = fwrite(form.contents[i].ptr, form.contents[i].len, 1, f);
		debug("fwrite returned %ld", nbytes);

		strputfmt(&msg, "upload succesfully\n");

		fclose(f);
	}
	arrfree(path);

	if (msg == NULL) {
		no_content(ctx, 200);
		return CERVER_RESPONSE;
	}

	(void) arrpop(msg);
	arrput(msg, '\0');

	html(ctx, 200, msg);

	arrfree(msg);
	return CERVER_RESPONSE;
}

int main(void) {
	signal(SIGINT, cleanup);

	register_route(&c, "GET:/", redirect_to);
	register_route(&c, "GET:/favicon.ico", favicon);
	register_route(&c, "GET:/homepage", homepage);
	register_route(&c, "GET:/hello", hello);
	register_route(&c, "GET:/sleep", sleep10);
	register_route(&c, "GET", page404);
	register_route(&c, "POST:/concat", concat);
	register_route(&c, "POST:/upload", upload);
	if (!run(&c, PORT)) {
		debug("%s", strerror(errno));
		return 1;
	}

	return 0;
}

