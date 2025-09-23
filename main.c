#include <signal.h>
#include "cerver.h"

#define PORT 12345

Cerver c = {0};
void cleanup(int code) {
	(void) code;
	if (close(c.server) == 0) {
		c.server = -1;
		printf("Shutdown server\n");
	}
}

int page404(Context *ctx) {
	char *path = (char*) shget(ctx->header, "path");
	debug("%s", path);
	char *accept = (char*) shget(ctx->header, "accept");
	debug("%s", accept);

	if (strstr(accept, "html") == NULL) { // TODO: find a better way to check if
		no_content(ctx, 404);			  // the request is looking for a html file
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
	FILE *f = fopen("index.html", "rb");
	if (f == NULL) {
		no_content(ctx, 404);
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
	const char *path = shget(ctx->header, "path");
	if (strncmp(path, "/", 1) == 0) {
		const char *host = ""; // shget(ctx->header, "host");
		const char *dest = "/homepage";
		char *url = NULL;
		strputfmt(&url, "%s%s%0", host, dest);

		redirect(ctx, 301, url);

		arrfree(url);
		return 0;
	}

	return page404(ctx);
}

int concat(Context *ctx) {
	char *first_arg = (char*) shget(ctx->form_value, "1");
	char *second_arg = (char*) shget(ctx->form_value, "2");
	html(ctx, 200, "%s%s", first_arg, second_arg);

	return 0;
}

int hello(Context *ctx) {
	char *name = (char*) shget(ctx->query_param, "name");
	html(ctx, 200, "Hello %s", name);
	return 0;
}

int sleep10(Context *ctx) {
	sleep(10);
	no_content(ctx, 200);
	return 0;
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
	run(&c, PORT);

	return 0;
}
