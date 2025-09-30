# Cerver

This project is for building RESTAPIs and web applications.

> [!WARNING]
> There are a lot of things to do, some features are not work as expect and the lack of documentation may caused confusion.

## How to use

### A simple application

``` c
#include "cerver.h"

int hello(Context *ctx) {
	html(ctx, 200, "<h1>Hello World</h1>");
	return 0;
}

int main() {
    Cerver c = {0};
    register_route(&c, "GET:/", hello);
    run(&c, 12345);

    return 0;
}
```

Build and run the above code

``` bash
cc main.c -o main && ./main
```

Open your browser and visit http://localhost:12345

You should see `Hello World`

### Stream

``` c
#include "cerver.h"

/* other functions */

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

int main() {
    Cerver c = {0};
    /* register routes */

    register_route(&c, "GET:/favicon.ico", favicon);
    run(&c, 12345);

    return 0;
}
```

### Redirect

``` c
#include "cerver.h"

int page404(Context *ctx) {
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

int redirect_to(Context *ctx) {
	const char *path = shget(ctx->request_header, "path");
	if (strncmp(path, "/", 1) == 0) {
		const char *host = "";
		const char *dest = "/homepage";
		char *url = NULL;
		strputfmtn(&url, "%s%s", host, dest);

		redirect(ctx, 301, url);

		arrfree(url);
		return 0;
	}

	return page404(ctx);
}

int main() {
    Cerver c = {0};
	register_route(&c, "GET:/", redirect_to);
	register_route(&c, "GET:/homepage", homepage);

    run(&c, 12345);

    return 0;
}
```
