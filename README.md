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
    get(&c, "/", hello);
	if (!run(&c, 12345)) {
		debug("%s", strerror(errno));
		return 1;
	}

    return 0;
}
```

Build and run the above code

``` bash
cc main.c -o main && ./main
```

Open your browser and visit http://localhost:12345, you should see `Hello World`

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
    get(&c, "/favicon.ico", favicon);

    if (!run(&c, 12345)) {
		debug("%s", strerror(errno));
		return 1;
	}

    return 0;
}
```

### POST method

``` c
#include "cerver.h"

/* other functions */

int concat(Context *ctx) {
	Slice first_arg = form_value(ctx, "1");
	Slice second_arg = form_value(ctx, "2");
	html(ctx, 200, "%Sl%Sl", first_arg, second_arg);

	return 0;
}

int main() {
    Cerver c = {0};

    /* register routes */
    post(&c, "/concat", concat);

    if (!run(&c, 12345)) {
		debug("%s", strerror(errno));
		return 1;
	}

    return 0;
}
```

Run the above code and use `curl`

``` bash
curl -X POST "http://localhost:12345/concat" -d "1=Hello " -d "2=World!" --output -
```

It will return `Hello World!`

You can look at more [examples](main.c)
