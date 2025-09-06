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

int homepage(Context *ctx) {
	FILE *f = fopen("index.html", "rb");
	strputfmt(&ctx->response_body, "%F", f);
	if (f != NULL) {
		fclose(f);
	}

	return 200;
}

int concat(Context *ctx) {
	char *first_arg = (char*) shget(ctx->form_value, "1");
	char *second_arg = (char*) shget(ctx->form_value, "2");
	strputfmt(&ctx->response_body, "%s%s", first_arg, second_arg);

	return 200;
}

int hello(Context *ctx) {
	char *name = (char*) shget(ctx->query_param, "name");
	strputfmt(&ctx->response_body, "Hello %s", name);

	return 200;
}

int sleep10(Context *ctx) {
	sleep(10);
	return 200;
}

int page404(Context *ctx) {
	char *path = (char*) shget(ctx->header, "path");
	debug(path);

	FILE *f = fopen("404.html", "rb");
	strputfmt(&ctx->response_body, "%F", f);
	if (f != NULL) {
		fclose(f);
	}

	return 404;
}

int main(void) {
	signal(SIGINT, cleanup);

	register_route(&c, "GET:/", homepage);
	register_route(&c, "GET:/hello", hello);
	register_route(&c, "GET:/sleep", sleep10);
	register_route(&c, "GET", page404);
	register_route(&c, "POST:/concat", concat);
	run(&c, PORT);

	return 0;
}
