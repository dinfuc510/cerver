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

char *homepage(HttpRequest *req) {
	char *res = NULL, *content = NULL;
	strputfmt(&res, "HTTP/1.1 200 Ok\r\n");
	FILE *f = fopen("index.html", "rb");
	strputfmt(&content, "%F\r\n", f);
	if (f != NULL) {
		fclose(f);
	}

	strputfmt(&res, "Content-Length: %ld\r\n\r\n%S", arrlenu(content), content);

	arrfree(content);
	return res;
}

char *concat(HttpRequest *req) {
	char *res = NULL, *content = NULL;

	strputfmt(&res, "HTTP/1.1 200 Ok\r\n");
	char *first_arg = (char*) shget(req->form_value, "1");
	char *second_arg = (char*) shget(req->form_value, "2");
	strputfmt(&content, "%s%s\r\n", first_arg, second_arg);

	strputfmt(&res, "Content-Length: %ld\r\n\r\n%S", arrlenu(content), content);

	arrfree(content);
	return res;
}

char *hello(HttpRequest *req) {
	char *res = NULL, *content = NULL;
	strputfmt(&res, "HTTP/1.1 200 Ok\r\n");

	char *name = (char*) shget(req->query_param, "name");
	strputfmt(&content, "Hello %s\r\n", name);

	strputfmt(&res, "Content-Length: %ld\r\n\r\n%S", arrlenu(content), content);

	arrfree(content);
	return res;
}

char *sleep10(HttpRequest *req) {
	sleep(10);

	char *res = NULL;
	strputfmt(&res, "HTTP/1.1 200 Ok\r\n");
	strputfmt(&res, "Content-Length: 0\r\n\r\n");

	return res;
}

int main(void) {
	signal(SIGINT, cleanup);

	register_route(&c, "GET:/", homepage);
	register_route(&c, "GET:/hello", hello);
	register_route(&c, "GET:/sleep", sleep10);
	register_route(&c, "POST:/concat", concat);
	run(&c, PORT);

	return 0;
}
