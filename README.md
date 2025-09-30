# Cerver

This project is for building RESTAPIs and web applications.

> [!WARNING]
> There are a lot of things to do, some features are not work as expect and the lack of documentation may caused confusion.

## How to use

### A simple application

``` c
#include "cerver.h"

int hello(Context *ctx) {
	html(ctx, 200, "<h1>Hello World</h1>", f);
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
